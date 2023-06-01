/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "pdu_session_resource_release_routine.h"

using namespace srsran;
using namespace srsran::srs_cu_cp;
using namespace asn1::rrc_nr;

pdu_session_resource_release_routine::pdu_session_resource_release_routine(
    const cu_cp_pdu_session_resource_release_command& release_cmd_,
    du_processor_e1ap_control_notifier&               e1ap_ctrl_notif_,
    du_processor_f1ap_ue_context_notifier&            f1ap_ue_ctxt_notif_,
    up_resource_manager&                              rrc_ue_up_resource_manager_,
    srslog::basic_logger&                             logger_) :
  release_cmd(release_cmd_),
  e1ap_ctrl_notifier(e1ap_ctrl_notif_),
  f1ap_ue_ctxt_notifier(f1ap_ue_ctxt_notif_),
  rrc_ue_up_resource_manager(rrc_ue_up_resource_manager_),
  logger(logger_)
{
}

void pdu_session_resource_release_routine::operator()(
    coro_context<async_task<cu_cp_pdu_session_resource_release_response>>& ctx)
{
  CORO_BEGIN(ctx);

  logger.debug("ue={}: \"{}\" initialized.", release_cmd.ue_index, name());

  // Release DRB resources at DU
  {
    // prepare UeContextModificationRequest and call F1 notifier
    ue_context_mod_request.ue_index = release_cmd.ue_index;
    for (const auto& pdu_session_res_to_release : release_cmd.pdu_session_res_to_release_list_rel_cmd) {
      const auto& session_context =
          rrc_ue_up_resource_manager.get_pdu_session_context(pdu_session_res_to_release.pdu_session_id);
      for (const auto& drb : session_context.drbs) {
        ue_context_mod_request.drbs_to_be_released_list.push_back(drb.first);
      }
    }

    CORO_AWAIT_VALUE(ue_context_modification_response,
                     f1ap_ue_ctxt_notifier.on_ue_context_modification_request(ue_context_mod_request));

    // Handle UE Context Modification Response
    if (not ue_context_modification_response.success) {
      logger.error("ue={}: \"{}\" failed to modify UE context at DU.", release_cmd.ue_index, name());
    }
  }

  // Inform CU-UP about the release of a bearer
  {
    // prepare BearerContextModificationRequest and call e1 notifier
    bearer_context_modification_request.ue_index = release_cmd.ue_index;
    for (const auto& pdu_session_res_to_release : release_cmd.pdu_session_res_to_release_list_rel_cmd) {
      e1ap_ng_ran_bearer_context_mod_request bearer_context_mod_request;
      bearer_context_mod_request.pdu_session_res_to_rem_list.push_back(pdu_session_res_to_release.pdu_session_id);
      bearer_context_modification_request.ng_ran_bearer_context_mod_request = bearer_context_mod_request;
    }

    // call E1AP procedure and wait for BearerContextModificationResponse
    CORO_AWAIT_VALUE(bearer_context_modification_response,
                     e1ap_ctrl_notifier.on_bearer_context_modification_request(bearer_context_modification_request));

    // Handle BearerContextModificationResponse
    if (not bearer_context_modification_response.success) {
      logger.error("ue={}: \"{}\" failed to release bearer at CU-UP.", release_cmd.ue_index, name());
    }
  }

  // we are done
  CORO_RETURN(generate_pdu_session_resource_release_response());
}

cu_cp_pdu_session_resource_release_response
pdu_session_resource_release_routine::generate_pdu_session_resource_release_response()
{
  for (const auto& setup_item : release_cmd.pdu_session_res_to_release_list_rel_cmd) {
    cu_cp_pdu_session_res_released_item_rel_res item;
    item.pdu_session_id = setup_item.pdu_session_id;

    // TODO: Add pdu_session_res_release_resp_transfer

    response_msg.pdu_session_res_released_list_rel_res.emplace(setup_item.pdu_session_id, item);
  }

  return response_msg;
}
