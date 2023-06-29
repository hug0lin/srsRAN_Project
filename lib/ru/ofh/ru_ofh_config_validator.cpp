/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsran/ofh/ofh_constants.h"
#include "srsran/ru/ru_ofh_configuration.h"

using namespace srsran;

static bool check_compression_params(const ofh::ru_compression_params& params)
{
  if (!(params.type == ofh::compression_type::none) && !(params.type == ofh::compression_type::BFP)) {
    fmt::print("Compression method not supported. Valid values [none,bfp].\n");
    return false;
  }

  if (params.type == ofh::compression_type::BFP && !(params.data_width == 8) && !(params.data_width == 9) &&
      !(params.data_width == 12) && !(params.data_width == 14) && !(params.data_width == 16)) {
    fmt::print("BFP compression bit width not supported. Valid values [8,9,12,14,16].\n");

    return false;
  }

  return true;
}

static bool check_dl_eaxc_if_broadcast_is_enabled(const ru_ofh_configuration& config)
{
  for (const auto& sector : config.sector_configs) {
    if (!config.is_downlink_broadcast_enabled) {
      continue;
    }

    // When broadcast flag is enabled, two downlink eAxCs are supported.
    if (config.is_downlink_broadcast_enabled && sector.dl_eaxc.size() == 2) {
      continue;
    }

    fmt::print("Invalid downlink eAxC identifier configuration, broadcast flag is {} and there are {} downlink eAxC\n",
               (config.is_downlink_broadcast_enabled) ? "enabled" : "disabled",
               sector.dl_eaxc.size());
    return false;
  }

  return true;
}

static bool check_eaxc_id(unsigned eaxc)
{
  bool result = eaxc < ofh::MAX_SUPPORTED_EAXC_ID_VALUE;
  if (!result) {
    fmt::print("eAxC id={} not supported. Valid values [0-{}]\n", eaxc, ofh::MAX_SUPPORTED_EAXC_ID_VALUE - 1U);
  }

  return result;
}

static bool check_eaxcs_id(const ru_ofh_configuration& config)
{
  for (const auto& sector : config.sector_configs) {
    // Check PRACH eAxC.
    for (auto eaxc : sector.prach_eaxc) {
      if (!check_eaxc_id(eaxc)) {
        return false;
      }
    }

    // Check uplink eAxCs.
    for (auto eaxc : sector.ul_eaxc) {
      if (!check_eaxc_id(eaxc)) {
        return false;
      }
    }

    // Check downlink eAxCs.
    for (auto eaxc : sector.dl_eaxc) {
      if (!check_eaxc_id(eaxc)) {
        return false;
      }
    }
  }

  return true;
}

bool srsran::is_valid_ru_ofh_config(const ru_ofh_configuration& config)
{
  if (!check_compression_params(config.ul_compression_params)) {
    return false;
  }

  if (!check_compression_params(config.dl_compression_params)) {
    return false;
  }

  if (!check_compression_params(config.prach_compression_params)) {
    return false;
  }

  if (!check_dl_eaxc_if_broadcast_is_enabled(config)) {
    return false;
  }

  if (!check_eaxcs_id(config)) {
    return false;
  }

  return true;
}
