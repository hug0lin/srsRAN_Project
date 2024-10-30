/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsran/mac/config/mac_cell_group_config_factory.h"
#include "srsran/mac/bsr_format.h"
#include "srsran/mac/mac_cell_group_config.h"
#include "srsran/mac/phr_config.h"
#include "srsran/ran/sr_configuration.h"
#include "srsran/ran/time_alignment_config.h"

using namespace srsran;

mac_cell_group_config
config_helpers::make_initial_mac_cell_group_config(const srs_du::mac_cell_group_params& mcg_params)
{
  mac_cell_group_config        mcg_cfg{};
  scheduling_request_to_addmod sr_0{.sr_id          = scheduling_request_id::SR_ID_MIN,
                                    .prohibit_timer = mcg_params.sr_prohibit_timer,
                                    .max_tx         = mcg_params.max_tx};
  mcg_cfg.scheduling_request_config.emplace_back(sr_0);

  mcg_cfg.bsr_cfg.emplace();
  mcg_cfg.bsr_cfg.value().periodic_timer = mcg_params.periodic_timer;
  mcg_cfg.bsr_cfg.value().retx_timer     = mcg_params.retx_timer;
  if (mcg_params.lc_sr_delay_timer.has_value()) {
    mcg_cfg.bsr_cfg.value().lc_sr_delay_timer = mcg_params.lc_sr_delay_timer;
  }

  mcg_cfg.tag_config.emplace_back(
      time_alignment_group{.tag_id = time_alignment_group::id_t{0}, .ta_timer = time_alignment_timer::infinity});

  mcg_cfg.phr_cfg.emplace();
  mcg_cfg.phr_cfg.value().periodic_timer         = phr_periodic_timer::sf10;
  mcg_cfg.phr_cfg.value().prohibit_timer         = mcg_params.phr_prohib_timer;
  mcg_cfg.phr_cfg.value().power_factor_change    = phr_tx_power_factor_change::db1;
  mcg_cfg.phr_cfg.value().multiple_phr           = false;
  mcg_cfg.phr_cfg.value().dummy                  = false;
  mcg_cfg.phr_cfg.value().phr_type_to_other_cell = false;
  mcg_cfg.phr_cfg.value().phr_mode               = phr_mode_other_cg::real;

  mcg_cfg.skip_uplink_tx_dynamic = false;

  return mcg_cfg;
}
