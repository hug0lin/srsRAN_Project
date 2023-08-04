/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "../../../../lib/ofh/serdes/ofh_cplane_message_builder_dynamic_compression_impl.h"
#include <gtest/gtest.h>

using namespace srsran;
using namespace ofh;

static constexpr unsigned UD_COMP_HEADER_BYTE = 6U;

TEST(ofh_control_plane_packet_builder_dynamic_compression_impl_test, downlink_packet_sets_udcomprhdr_to_0)
{
  std::vector<uint8_t> packet = {
      0x90, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x00};

  std::vector<uint8_t> result_packet(packet.size(), 0);

  cplane_section_type1_parameters  packet_params;
  cplane_radio_application_header& header = packet_params.radio_hdr;
  header.direction                        = data_direction::downlink;
  header.filter_index                     = filter_index_type::standard_channel_filter;
  header.slot                             = slot_point(0, 0, 0);
  header.start_symbol                     = 0;

  cplane_common_section_0_1_3_5_fields& section = packet_params.section_fields.common_fields;
  section.section_id                            = 0;
  section.prb_start                             = 0;
  section.nof_prb                               = 0;
  section.re_mask                               = 0xfff;
  section.nof_symbols                           = 14;
  packet_params.comp_params                     = {compression_type::BFP, 9};

  cplane_message_builder_dynamic_compression_impl builder;

  unsigned nof_bytes = builder.build_dl_ul_radio_channel_message(result_packet, packet_params);

  ASSERT_EQ(packet, result_packet);
  ASSERT_EQ(nof_bytes, packet.size());
  // Check that the udCompHdr is 0.
  ASSERT_EQ(0, result_packet[UD_COMP_HEADER_BYTE]);
}

TEST(ofh_control_plane_packet_builder_dynamic_compression_impl_test, uplink_packet_codifies_ud_comp_header)
{
  std::vector<uint8_t> packet = {
      0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x91, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x00};

  std::vector<uint8_t> result_packet(packet.size(), 0);

  cplane_section_type1_parameters  packet_params;
  cplane_radio_application_header& header = packet_params.radio_hdr;
  header.direction                        = data_direction::uplink;
  header.filter_index                     = filter_index_type::standard_channel_filter;
  header.slot                             = slot_point(0, 0, 0);
  header.start_symbol                     = 0;

  cplane_common_section_0_1_3_5_fields& section = packet_params.section_fields.common_fields;
  section.section_id                            = 0;
  section.prb_start                             = 0;
  section.nof_prb                               = 0;
  section.re_mask                               = 0xfff;
  section.nof_symbols                           = 14;
  packet_params.comp_params                     = {compression_type::BFP, 9};

  cplane_message_builder_dynamic_compression_impl builder;

  unsigned nof_bytes = builder.build_dl_ul_radio_channel_message(result_packet, packet_params);

  ASSERT_EQ(packet, result_packet);
  ASSERT_EQ(nof_bytes, packet.size());
  // Check that the udCompHdr is 0.
  ASSERT_EQ(packet_params.comp_params.data_width, result_packet[UD_COMP_HEADER_BYTE] >> 4);
  ASSERT_EQ(packet_params.comp_params.type, to_compression_type(result_packet[UD_COMP_HEADER_BYTE] & 0xf));
}
