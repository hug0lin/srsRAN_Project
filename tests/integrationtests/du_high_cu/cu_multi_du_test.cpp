/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "du_high_cu_test_simulator.h"
#include "srsran/du/du_cell_config_helpers.h"
#include <gtest/gtest.h>

using namespace srsran;

namespace {

du_high_cu_cp_test_simulator_config create_test_sim_config(unsigned nof_dus)
{
  du_high_cu_cp_test_simulator_config config;
  for (unsigned i = 0; i < nof_dus; i++) {
    cell_config_builder_params params;
    params.pci = i;
    config.dus.push_back({config_helpers::make_default_du_cell_config(params)});
  }
  return config;
}

class cu_multi_du_test : public du_high_cu_test_simulator, public ::testing::Test
{
public:
  cu_multi_du_test() : du_high_cu_test_simulator(create_test_sim_config(nof_dus)) {}

  constexpr static unsigned nof_dus = 2;
};
constexpr unsigned cu_multi_du_test::nof_dus;

} // namespace

TEST_F(cu_multi_du_test, f1_setup_multiple_dus)
{
  ASSERT_EQ(this->cu_cp_inst->get_connected_dus().get_nof_dus(), 0);

  this->start_dus();

  ASSERT_EQ(this->cu_cp_inst->get_connected_dus().get_nof_dus(), cu_multi_du_test::nof_dus);

  for (unsigned i = 0; i < cu_multi_du_test::nof_dus; i++) {
    ASSERT_EQ(f1c_gw.get_last_cu_cp_rx_pdus(i).size(), 1);
    ASSERT_EQ(f1c_gw.get_last_cu_cp_tx_pdus(i).size(), 1);

    // F1 Setup sent to CU-CP.
    const f1ap_message du_msg = f1c_gw.get_last_cu_cp_rx_pdus(i)[0];
    ASSERT_EQ(du_msg.pdu.type().value, asn1::f1ap::f1ap_pdu_c::types_opts::init_msg);
    ASSERT_EQ(du_msg.pdu.init_msg().value.type().value,
              asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::f1_setup_request);

    // F1 Setup Response sent back to DU.
    const f1ap_message cu_msg = f1c_gw.get_last_cu_cp_tx_pdus(i)[0];
    ASSERT_EQ(cu_msg.pdu.type().value, asn1::f1ap::f1ap_pdu_c::types_opts::successful_outcome);
    ASSERT_EQ(cu_msg.pdu.successful_outcome().value.type().value,
              asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::f1_setup_resp);
  }
}

TEST_F(cu_multi_du_test, multi_du_ues)
{
  this->start_dus();

  // Add one UE to each DU.
  for (unsigned i = 0; i < cu_multi_du_test::nof_dus; i++) {
    ASSERT_TRUE(add_ue(i, to_rnti(0x4601)));
  }

  ASSERT_EQ(this->cu_cp_inst->get_connected_dus().get_nof_ues(), 2);
}
