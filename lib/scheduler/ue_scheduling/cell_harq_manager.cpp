/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "cell_harq_manager.h"

using namespace srsran;
using namespace harq_utils;

template <bool IsDl>
cell_harq_repository<IsDl>::cell_harq_repository(unsigned               max_ues,
                                                 unsigned               max_ack_wait_timeout,
                                                 harq_timeout_notifier& timeout_notifier_,
                                                 srslog::basic_logger&  logger_) :
  max_ack_wait_in_slots(max_ack_wait_timeout), timeout_notifier(timeout_notifier_), logger(logger_)
{
  harqs.resize(MAX_NOF_HARQS * max_ues);
  free_harqs.resize(MAX_NOF_HARQS * max_ues);
  for (unsigned i = 0; i != free_harqs.size(); ++i) {
    free_harqs[i] = free_harqs.size() - i - 1;
  }

  // Reserve space in advance for UEs.
  ues.resize(max_ues);
  for (unsigned i = 0; i != max_ues; i++) {
    ues[i].free_harq_ids.reserve(MAX_NOF_HARQS);
    ues[i].harqs.resize(MAX_NOF_HARQS, INVALID_HARQ_REF_INDEX);
  }

  const unsigned RING_SIZE = 40; // TODO: use function to define this value.
  harq_timeout_wheel.resize(RING_SIZE);
}

template <bool IsDl>
void cell_harq_repository<IsDl>::slot_indication(slot_point sl_tx)
{
  // Handle HARQs that timed out.
  auto& harqs_timing_out = harq_timeout_wheel[sl_tx.to_uint() % harq_timeout_wheel.size()];
  for (harq_type& h : harqs_timing_out) {
    handle_harq_ack_timeout(h, sl_tx);
  }
}

template <bool IsDl>
void cell_harq_repository<IsDl>::handle_harq_ack_timeout(harq_type& h, slot_point sl_tx)
{
  srsran_sanity_check(h.status == harq_state_t::waiting_ack, "HARQ process in wrong state");

  if (max_ack_wait_in_slots != 1) {
    // Only in non-NTN case, we log a warning.
    if (h.ack_on_timeout) {
      // Case: Not all HARQ-ACKs were received, but at least one positive ACK was received.
      logger.debug("ue={} h_id={}: Setting {} HARQ to \"ACKed\" state. Cause: HARQ-ACK wait timeout ({} slots) was "
                   "reached with still missing PUCCH HARQ-ACKs. However, one positive ACK was received.",
                   h.ue_idx,
                   h.h_id,
                   IsDl ? "DL" : "UL",
                   h.slot_ack_timeout - h.slot_ack);
    } else {
      // At least one of the expected ACKs went missing and we haven't received any positive ACK.
      logger.warning(
          "ue={} h_id={}: Discarding {} HARQ. Cause: HARQ-ACK wait timeout ({} slots) was reached, but there are still "
          "missing HARQ-ACKs and none of the received ones are positive.",
          h.ue_idx,
          h.h_id,
          IsDl ? "DL" : "UL",
          h.slot_ack_timeout - h.slot_ack);
    }

    // Report timeout with NACK.
    timeout_notifier.on_harq_timeout(h.ue_idx, IsDl, h.ack_on_timeout);
  }

  // Deallocate HARQ.
  dealloc_harq(h);
}

template <bool IsDl>
unsigned cell_harq_repository<IsDl>::get_harq_ref_idx(const harq_type& h) const
{
  return &h - harqs.data();
}

template <bool IsDl>
typename cell_harq_repository<IsDl>::harq_type* cell_harq_repository<IsDl>::alloc_harq(du_ue_index_t ue_idx,
                                                                                       slot_point    sl_tx,
                                                                                       slot_point    sl_ack,
                                                                                       unsigned      max_nof_harq_retxs)
{
  ue_harq_entity_impl& ue_harq_entity = ues[ue_idx];
  if (free_harqs.empty() or ue_harq_entity.free_harq_ids.empty()) {
    return nullptr;
  }

  // Allocation of free HARQ-id for the UE.
  const harq_id_t h_id = ue_harq_entity.free_harq_ids.back();
  ue_harq_entity.free_harq_ids.pop_back();

  // Allocation of DL HARQ process for the UE.
  unsigned harq_ref_idx = free_harqs.back();
  free_harqs.pop_back();
  ue_harq_entity.harqs[h_id] = harq_ref_idx;
  harq_type& h               = harqs[harq_ref_idx];

  // Set allocated HARQ common params.
  h.ue_idx             = ue_idx;
  h.h_id               = h_id;
  h.status             = harq_state_t::waiting_ack;
  h.slot_tx            = sl_tx;
  h.slot_ack           = sl_ack;
  h.nof_retxs          = 0;
  h.ndi                = !h.ndi;
  h.max_nof_harq_retxs = max_nof_harq_retxs;
  h.ack_on_timeout     = false;
  h.retxs_cancelled    = false;

  // Add HARQ to the timeout list.
  h.slot_ack_timeout = sl_ack + max_ack_wait_in_slots;
  harq_timeout_wheel[h.slot_ack_timeout.to_uint() % harq_timeout_wheel.size()].push_front(&h);

  return &h;
}

template <bool IsDl>
void cell_harq_repository<IsDl>::dealloc_harq(harq_type& h)
{
  if (h.status == harq_state_t::empty) {
    // No-op
    return;
  }
  ue_harq_entity_impl& ue_harq_entity = ues[h.ue_idx];

  // Mark HARQ-Id as available.
  ue_harq_entity.harqs[h.h_id] = INVALID_HARQ_REF_INDEX;
  ue_harq_entity.free_harq_ids.push_back(h.h_id);

  // Push HARQ resource back to cell free list.
  free_harqs.push_back(get_harq_ref_idx(h));

  if (h.status == harq_state_t::waiting_ack) {
    // Remove the HARQ from the timeout list.
    harq_timeout_wheel[h.slot_ack_timeout.to_uint() % harq_timeout_wheel.size()].pop(&h);
  } else {
    // Remove the HARQ from the pending Retx list.
    harq_pending_retx_list.pop(&h);
  }

  // Update HARQ process state.
  h.status = harq_state_t::empty;
}

template <bool IsDl>
void cell_harq_repository<IsDl>::handle_ack(harq_type& h, bool ack)
{
  if (not ack and h.nof_retxs >= h.max_nof_harq_retxs) {
    if (h.retxs_cancelled) {
      logger.info(
          "ue={} h_id={}: Discarding {} HARQ process TB with tbs={}. Cause: Retxs for this HARQ process were cancelled",
          h.ue_idx,
          h.h_id,
          IsDl ? "DL" : "UL",
          h.prev_tx_params.tbs_bytes);
    } else {
      logger.info(
          "ue={} h_id={}: Discarding {} HARQ process TB with tbs={}. Cause: Maximum number of reTxs {} exceeded",
          h.ue_idx,
          h.h_id,
          IsDl ? "DL" : "UL",
          h.prev_tx_params.tbs_bytes,
          h.max_nof_harq_retxs);
    }
  }

  if (ack or h.nof_retxs >= h.max_nof_harq_retxs) {
    // If the HARQ process is ACKed or the maximum number of retransmissions has been reached, we can deallocate the
    // HARQ process.
    dealloc_harq(h);
  } else {
    set_pending_retx(h);
  }
}

template <bool IsDl>
void cell_harq_repository<IsDl>::set_pending_retx(harq_type& h)
{
  srsran_sanity_check(h.status != harq_state_t::empty, "HARQ process in wrong state");
  if (h.status == harq_state_t::pending_retx) {
    // No-op
    return;
  }

  // Remove the HARQ from the timeout list.
  harq_timeout_wheel[h.slot_ack_timeout.to_uint() % harq_timeout_wheel.size()].pop(&h);

  // Add HARQ to pending Retx list.
  harq_pending_retx_list.push_front(&h);

  // Update HARQ process state.
  h.status = harq_state_t::pending_retx;
}

template <bool IsDl>
void cell_harq_repository<IsDl>::reserve_ue_harqs(du_ue_index_t ue_idx, unsigned nof_harqs)
{
  ues[ue_idx].free_harq_ids.resize(nof_harqs);
  for (unsigned count = 0; count != nof_harqs; count++) {
    harq_id_t h_id                   = to_harq_id(nof_harqs - count - 1);
    ues[ue_idx].free_harq_ids[count] = h_id;
  }
}

template <bool IsDl>
void cell_harq_repository<IsDl>::destroy_ue_harqs(du_ue_index_t ue_idx)
{
  // Return back to the pool all HARQ processes allocated by the UE.
  for (unsigned h_idx : ues[ue_idx].harqs) {
    if (h_idx != INVALID_HARQ_REF_INDEX) {
      dealloc_harq(harqs[h_idx]);
    }
  }
  ues[ue_idx].free_harq_ids.clear();
}

template <bool IsDl>
void cell_harq_repository<IsDl>::cancel_retxs(harq_type& h)
{
  if (h.status == harq_state_t::empty) {
    return;
  }
  h.max_nof_harq_retxs = h.nof_retxs;
  h.retxs_cancelled    = true;
}

template <bool IsDl>
unsigned cell_harq_repository<IsDl>::find_ue_harq_in_state(du_ue_index_t ue_idx, harq_utils::harq_state_t state) const
{
  for (unsigned h_ref_idx : ues[ue_idx].harqs) {
    if (h_ref_idx != INVALID_HARQ_REF_INDEX) {
      const harq_type& h = harqs[h_ref_idx];
      if (h.status == state) {
        return h_ref_idx;
      }
    }
  }
  return INVALID_HARQ_REF_INDEX;
}

template struct harq_utils::cell_harq_repository<true>;
template struct harq_utils::cell_harq_repository<false>;

// Cell HARQ manager.

cell_harq_manager::cell_harq_manager(unsigned                               max_ues,
                                     std::unique_ptr<harq_timeout_notifier> notifier,
                                     unsigned                               max_ack_wait_timeout) :
  timeout_notifier(std::move(notifier)),
  logger(srslog::fetch_basic_logger("SCHED")),
  dl(max_ues, max_ack_wait_timeout, *timeout_notifier, logger),
  ul(max_ues, max_ack_wait_timeout, *timeout_notifier, logger)
{
}

void cell_harq_manager::slot_indication(slot_point sl_tx)
{
  last_sl_tx = sl_tx;
  dl.slot_indication(sl_tx);
  ul.slot_indication(sl_tx);
}

bool cell_harq_manager::contains(du_ue_index_t ue_idx) const
{
  return dl.ues[ue_idx].free_harq_ids.size() != 0;
}

unique_ue_harq_entity
cell_harq_manager::add_ue(du_ue_index_t ue_idx, rnti_t crnti, unsigned nof_dl_harq_procs, unsigned nof_ul_harq_procs)
{
  srsran_assert(nof_dl_harq_procs > 0, "Invalid number of HARQs");
  srsran_assert(nof_ul_harq_procs > 0, "Invalid number of HARQs");
  srsran_assert(not contains(ue_idx), "Creating UE with duplicate ue_index");
  dl.reserve_ue_harqs(ue_idx, nof_dl_harq_procs);
  ul.reserve_ue_harqs(ue_idx, nof_ul_harq_procs);
  return {this, ue_idx, crnti};
}

void cell_harq_manager::destroy_ue(du_ue_index_t ue_idx)
{
  dl.destroy_ue_harqs(ue_idx);
  ul.destroy_ue_harqs(ue_idx);
}

harq_utils::dl_harq_process_impl* cell_harq_manager::new_dl_tx(du_ue_index_t ue_idx,
                                                               slot_point    pdsch_slot,
                                                               unsigned      k1,
                                                               unsigned      max_harq_nof_retxs,
                                                               uint8_t       harq_bit_idx)
{
  dl_harq_process_impl* h = dl.alloc_harq(ue_idx, pdsch_slot, pdsch_slot + k1, max_harq_nof_retxs);
  if (h == nullptr) {
    return nullptr;
  }

  // Save DL-specific parameters.
  h->prev_tx_params       = {};
  h->harq_bit_idx         = harq_bit_idx;
  h->pucch_ack_to_receive = 0;
  h->chosen_ack           = mac_harq_ack_report_status::dtx;
  h->last_pucch_snr       = std::nullopt;

  return h;
}

harq_utils::ul_harq_process_impl*
cell_harq_manager::new_ul_tx(du_ue_index_t ue_idx, slot_point pusch_slot, unsigned max_harq_nof_retxs)
{
  ul_harq_process_impl* h = ul.alloc_harq(ue_idx, pusch_slot, pusch_slot, max_harq_nof_retxs);
  if (h == nullptr) {
    return nullptr;
  }

  // Save UL-specific parameters.
  h->prev_tx_params = {};

  return h;
}

dl_harq_process_impl::status_update cell_harq_manager::dl_ack_info(harq_utils::dl_harq_process_impl& h,
                                                                   mac_harq_ack_report_status        ack,
                                                                   std::optional<float>              pucch_snr)
{
  using status_update = dl_harq_process_impl::status_update;

  if (h.status != harq_state_t::waiting_ack) {
    // If the HARQ process is not expecting an HARQ-ACK, it means that it has already been ACKed/NACKed.
    logger.warning("ue={} h_id={}: ACK arrived for inactive DL HARQ", h.ue_idx, h.h_id);
    return status_update::error;
  }

  if (ack != mac_harq_ack_report_status::dtx and
      (not h.last_pucch_snr.has_value() or (pucch_snr.has_value() and h.last_pucch_snr.value() < pucch_snr.value()))) {
    // Case: If there was no previous HARQ-ACK decoded or the previous HARQ-ACK had lower SNR, this HARQ-ACK is chosen.
    h.chosen_ack     = ack;
    h.last_pucch_snr = pucch_snr;
  }

  if (h.pucch_ack_to_receive <= 1) {
    // Case: This is the last HARQ-ACK that is expected for this HARQ process.

    // Update HARQ state
    bool final_ack = h.chosen_ack == mac_harq_ack_report_status::ack;
    dl.handle_ack(h, final_ack);

    return final_ack ? status_update::acked : status_update::nacked;
  }

  // Case: This is not the last PUCCH HARQ-ACK that is expected for this HARQ process.
  h.pucch_ack_to_receive--;
  h.ack_on_timeout = h.chosen_ack == mac_harq_ack_report_status::ack;
  // We reduce the HARQ process timeout to receive the next HARQ-ACK. This is done because the two HARQ-ACKs should
  // arrive almost simultaneously, and in case the second goes missing, we don't want to block the HARQ for too long.
  dl.harq_timeout_wheel[h.slot_ack_timeout.to_uint() % dl.harq_timeout_wheel.size()].pop(&h);
  h.slot_ack_timeout = last_sl_tx + SHORT_ACK_TIMEOUT_DTX;
  dl.harq_timeout_wheel[h.slot_ack_timeout.to_uint() % dl.harq_timeout_wheel.size()].push_front(&h);

  return status_update::no_update;
}

int cell_harq_manager::ul_crc_info(harq_utils::ul_harq_process_impl& h, bool ack)
{
  if (h.status != harq_state_t::waiting_ack) {
    // HARQ is not expecting CRC info.
    logger.warning("ue={} h_id={}: CRC arrived for UL HARQ not expecting it", h.ue_idx, h.h_id);
    return -1;
  }

  ul.handle_ack(h, ack);

  return ack ? (int)h.prev_tx_params.tbs_bytes : 0;
}

dl_harq_process_view::status_update dl_harq_process_view::dl_ack_info(mac_harq_ack_report_status ack,
                                                                      std::optional<float>       pucch_snr)
{
  return cell_harq_mng->dl_ack_info(cell_harq_mng->dl.harqs[harq_ref_idx], ack, pucch_snr);
}

int ul_harq_process_view::ul_crc_info(bool ack)
{
  return cell_harq_mng->ul_crc_info(cell_harq_mng->ul.harqs[harq_ref_idx], ack);
}

// UE HARQ entity.

unique_ue_harq_entity::unique_ue_harq_entity(unique_ue_harq_entity&& other) noexcept :
  cell_harq_mgr(other.cell_harq_mgr), ue_index(other.ue_index)
{
  other.cell_harq_mgr = nullptr;
  other.ue_index      = INVALID_DU_UE_INDEX;
}

unique_ue_harq_entity& unique_ue_harq_entity::operator=(unique_ue_harq_entity&& other) noexcept
{
  if (cell_harq_mgr != nullptr) {
    cell_harq_mgr->destroy_ue(ue_index);
  }
  cell_harq_mgr       = other.cell_harq_mgr;
  ue_index            = other.ue_index;
  other.cell_harq_mgr = nullptr;
  other.ue_index      = INVALID_DU_UE_INDEX;
  return *this;
}

unique_ue_harq_entity::~unique_ue_harq_entity()
{
  if (cell_harq_mgr != nullptr) {
    cell_harq_mgr->destroy_ue(ue_index);
  }
}

void unique_ue_harq_entity::reset()
{
  if (cell_harq_mgr != nullptr) {
    cell_harq_mgr->destroy_ue(ue_index);
    cell_harq_mgr = nullptr;
  }
}

std::optional<dl_harq_process_view>
unique_ue_harq_entity::alloc_dl_harq(slot_point sl_tx, unsigned k1, unsigned max_harq_nof_retxs, unsigned harq_bit_idx)
{
  dl_harq_process_impl* h = cell_harq_mgr->new_dl_tx(ue_index, sl_tx, k1, max_harq_nof_retxs, harq_bit_idx);
  if (h == nullptr) {
    return std::nullopt;
  }
  return dl_harq_process_view(*cell_harq_mgr, cell_harq_mgr->dl.get_harq_ref_idx(*h));
}

std::optional<ul_harq_process_view> unique_ue_harq_entity::alloc_ul_harq(slot_point sl_tx, unsigned max_harq_nof_retxs)
{
  ul_harq_process_impl* h = cell_harq_mgr->new_ul_tx(ue_index, sl_tx, max_harq_nof_retxs);
  if (h == nullptr) {
    return std::nullopt;
  }
  return ul_harq_process_view(*cell_harq_mgr, cell_harq_mgr->ul.get_harq_ref_idx(*h));
}

std::optional<dl_harq_process_view> unique_ue_harq_entity::find_pending_dl_retx()
{
  unsigned h_ref_idx = cell_harq_mgr->dl.find_ue_harq_in_state(ue_index, harq_state_t::pending_retx);
  if (h_ref_idx == INVALID_HARQ_REF_INDEX) {
    return std::nullopt;
  }
  return dl_harq_process_view(*cell_harq_mgr, h_ref_idx);
}

std::optional<ul_harq_process_view> unique_ue_harq_entity::find_pending_ul_retx()
{
  unsigned h_ref_idx = cell_harq_mgr->ul.find_ue_harq_in_state(ue_index, harq_state_t::pending_retx);
  if (h_ref_idx == INVALID_HARQ_REF_INDEX) {
    return std::nullopt;
  }
  return ul_harq_process_view(*cell_harq_mgr, h_ref_idx);
}

std::optional<dl_harq_process_view> unique_ue_harq_entity::find_dl_harq_waiting_ack()
{
  unsigned h_ref_idx = cell_harq_mgr->dl.find_ue_harq_in_state(ue_index, harq_state_t::waiting_ack);
  if (h_ref_idx == INVALID_HARQ_REF_INDEX) {
    return std::nullopt;
  }
  return dl_harq_process_view(*cell_harq_mgr, h_ref_idx);
}

std::optional<ul_harq_process_view> unique_ue_harq_entity::find_ul_harq_waiting_ack()
{
  unsigned h_ref_idx = cell_harq_mgr->ul.find_ue_harq_in_state(ue_index, harq_state_t::waiting_ack);
  if (h_ref_idx == INVALID_HARQ_REF_INDEX) {
    return std::nullopt;
  }
  return ul_harq_process_view(*cell_harq_mgr, h_ref_idx);
}

std::optional<dl_harq_process_view> unique_ue_harq_entity::find_dl_harq(slot_point uci_slot, uint8_t harq_bit_idx)
{
  const std::vector<unsigned>& dl_harqs = cell_harq_mgr->dl.ues[ue_index].harqs;
  for (unsigned h_ref_idx : dl_harqs) {
    if (h_ref_idx != INVALID_HARQ_REF_INDEX) {
      const dl_harq_process_impl& h = cell_harq_mgr->dl.harqs[h_ref_idx];
      if (h.status == harq_utils::harq_state_t::waiting_ack and h.slot_ack == uci_slot and
          h.harq_bit_idx == harq_bit_idx) {
        return dl_harq_process_view(*cell_harq_mgr, h_ref_idx);
      }
    }
  }
  return std::nullopt;
}

std::optional<ul_harq_process_view> unique_ue_harq_entity::find_ul_harq(slot_point pusch_slot)
{
  const std::vector<unsigned>& ul_harqs = cell_harq_mgr->ul.ues[ue_index].harqs;
  for (unsigned h_ref_idx : ul_harqs) {
    if (h_ref_idx != INVALID_HARQ_REF_INDEX) {
      const ul_harq_process_impl& h = cell_harq_mgr->ul.harqs[h_ref_idx];
      if (h.status == harq_utils::harq_state_t::waiting_ack and h.slot_tx == pusch_slot) {
        return ul_harq_process_view(*cell_harq_mgr, h_ref_idx);
      }
    }
  }
  return std::nullopt;
}
