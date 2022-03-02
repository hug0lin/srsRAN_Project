
#include "../../resource_grid_test_doubles.h"
#include "../signal_processors/dmrs_pbch_processor_doubles.h"
#include "../signal_processors/pss_processor_doubles.h"
#include "../signal_processors/sss_processor_doubles.h"
#include "pbch_encoder_doubles.h"
#include "pbch_modulator_doubles.h"
#include "srsgnb/phy/upper/channel_processors/ssb_processor.h"
#include "srsgnb/srsvec/compare.h"
#include "srsgnb/support/math_utils.h"
#include <cassert>
#include <random>

using namespace srsgnb;

namespace srsgnb {
struct ssb_processor_config {
  std::unique_ptr<pbch_encoder>        encoder;
  std::unique_ptr<pbch_modulator>      modulator;
  std::unique_ptr<dmrs_pbch_processor> dmrs;
  std::unique_ptr<pss_processor>       pss;
  std::unique_ptr<sss_processor>       sss;
};

std::unique_ptr<ssb_processor> create_ssb_processor(ssb_processor_config& config);
} // namespace srsgnb

static std::mt19937 rgen(0);

int main()
{
  std::uniform_int_distribution<unsigned> sfn_dist(0, 1023);
  std::uniform_int_distribution<unsigned> pci_dist(0, phys_cell_id::NOF_NID - 1);
  std::uniform_int_distribution<unsigned> bit_dist(0, 1);
  std::uniform_int_distribution<unsigned> port_dist(0, 63);

  resource_grid_dummy grid;

  pbch_encoder_spy*        encoder   = new pbch_encoder_spy;
  pbch_modulator_spy*      modulator = new pbch_modulator_spy;
  dmrs_pbch_processor_spy* dmrs      = new dmrs_pbch_processor_spy;
  pss_processor_spy*       pss       = new pss_processor_spy;
  sss_processor_spy*       sss       = new sss_processor_spy;

  ssb_processor_config config = {};
  config.encoder              = std::unique_ptr<pbch_encoder>(encoder);
  config.modulator            = std::unique_ptr<pbch_modulator>(modulator);
  config.dmrs                 = std::unique_ptr<dmrs_pbch_processor>(dmrs);
  config.pss                  = std::unique_ptr<pss_processor>(pss);
  config.sss                  = std::unique_ptr<sss_processor>(sss);

  std::unique_ptr<ssb_processor> pbch = create_ssb_processor(config);

  // Iterate all possible SSB pattern cases
  for (const ssb_pattern_case& pattern_case :
       {ssb_pattern_case::A, ssb_pattern_case::B, ssb_pattern_case::C, ssb_pattern_case::D, ssb_pattern_case::E}) {
    // Iterate possible L_max
    for (unsigned L_max : {4, 8, 64}) {
      // Iterate possible beta PSS
      for (float beta_pss : {0.0F, -3.0F}) {
        // Select numerology from case
        unsigned numerology = 0; // 15 kHz
        switch (pattern_case) {
          case ssb_pattern_case::A:
            // 15 kHz
            break;
          case ssb_pattern_case::B:
          case ssb_pattern_case::C:
            numerology = 1; // 30 kHz
            break;
          case ssb_pattern_case::D:
            numerology = 3; // 120 kHz
            break;
          case ssb_pattern_case::E:
            numerology = 4; // 240 kHz
            break;
        }

        // Skip invalid pattern and L_max combinations
        if (pattern_case != ssb_pattern_case::E && pattern_case != ssb_pattern_case::D && L_max == 64) {
          continue;
        }

        // Iterate over all possible SS/PBCH block candidates
        for (unsigned ssb_idx = 0; ssb_idx < L_max; ++ssb_idx) {
          unsigned ssb_offset_pointA     = 0;
          unsigned ssb_subcarrier_offset = 0;

          // Deduce derivative variables
          unsigned ssb_first_symbol_burst = ssb_get_l_first(pattern_case, ssb_idx);
          unsigned nslots_in_subframe     = 1 << numerology;
          unsigned slot_in_burst          = ssb_first_symbol_burst / NSYMB_PER_SLOT_NORM;
          unsigned subframe_in_burst      = slot_in_burst / nslots_in_subframe;
          unsigned slot_in_subframe       = slot_in_burst % nslots_in_subframe;

          // Deduce derivative assertion values
          unsigned ssb_first_subcarrier  = ssb_get_k_first(numerology, ssb_offset_pointA, ssb_subcarrier_offset);
          unsigned ssb_first_symbol_slot = ssb_first_symbol_burst % NSYMB_PER_SLOT_NORM;

          // Iterate half frames
          for (unsigned subframe : {0 + subframe_in_burst, 5 + subframe_in_burst}) {
            // Generate PBCH PDU
            ssb_processor::pdu_t pdu  = {};
            pdu.slot.numerology       = numerology;
            pdu.slot.frame            = sfn_dist(rgen);
            pdu.slot.subframe         = subframe;
            pdu.slot.slot             = slot_in_subframe;
            pdu.phys_cell_id          = pci_dist(rgen);
            pdu.beta_pss              = beta_pss;
            pdu.ssb_idx               = ssb_idx;
            pdu.L_max                 = L_max;
            pdu.ssb_subcarrier_offset = ssb_offset_pointA;
            pdu.ssb_offset_pointA     = 0;
            pdu.pattern_case          = pattern_case;
            pdu.ports.emplace_back(port_dist(rgen));
            for (uint8_t& bit : pdu.bch_payload) {
              bit = bit_dist(rgen);
            }

            // Reset spies
            encoder->reset();
            modulator->reset();
            dmrs->reset();
            pss->reset();
            sss->reset();

            // Process PDU
            pbch->process(pdu, grid);

            // Assert modules number of entries
            assert(encoder->get_nof_entries() == 1);
            assert(modulator->get_nof_entries() == 1);
            assert(dmrs->get_nof_entries() == 1);
            assert(pss->get_nof_entries() == 1);
            assert(sss->get_nof_entries() == 1);

            // Assert encoder
            const auto& encoder_entry = encoder->get_entries()[0];
            assert(encoder_entry.msg.N_id == pdu.phys_cell_id);
            assert(encoder_entry.msg.ssb_idx == pdu.ssb_idx);
            assert(encoder_entry.msg.L_max == L_max);
            assert(encoder_entry.msg.hrf == pdu.slot.get_half_radio_frame());
            assert(srsvec::equal(encoder_entry.msg.payload, encoder_entry.msg.payload));
            assert(encoder_entry.msg.sfn == pdu.slot.frame);
            assert(encoder_entry.msg.k_ssb == pdu.ssb_subcarrier_offset);
            assert(encoder_entry.encoded.size() == pbch_encoder::E);

            // Assert modulator
            const auto& modulator_entry = modulator->get_entries()[0];
            assert(modulator_entry.config.phys_cell_id == pdu.phys_cell_id);
            assert(modulator_entry.config.ssb_idx == pdu.ssb_idx);
            assert(modulator_entry.config.ssb_first_subcarrier == ssb_first_subcarrier);
            assert(modulator_entry.config.ssb_first_symbol == ssb_first_symbol_slot);
            assert(modulator_entry.config.amplitude == 1.0F);
            assert(srsvec::equal(modulator_entry.config.ports, pdu.ports));
            assert(srsvec::equal(modulator_entry.bits, encoder_entry.encoded));
            assert(modulator_entry.grid_ptr == &grid);

            // Assert DMRS for PBCH
            const auto& dmrs_entry = dmrs->get_entries()[0];
            assert(dmrs_entry.config.phys_cell_id == pdu.phys_cell_id);
            assert(dmrs_entry.config.ssb_idx == pdu.ssb_idx);
            assert(dmrs_entry.config.L_max == pdu.L_max);
            assert(dmrs_entry.config.ssb_first_subcarrier == ssb_first_subcarrier);
            assert(dmrs_entry.config.ssb_first_symbol == ssb_first_symbol_slot);
            assert(dmrs_entry.config.n_hf == pdu.slot.get_half_radio_frame());
            assert(dmrs_entry.config.amplitude == 1.0F);
            assert(srsvec::equal(dmrs_entry.config.ports, pdu.ports));

            // Assert PSS
            const auto& pss_entry = pss->get_entries()[0];
            assert(pss_entry.config.phys_cell_id == pdu.phys_cell_id);
            assert(pss_entry.config.ssb_first_subcarrier == ssb_first_subcarrier);
            assert(pss_entry.config.ssb_first_symbol == ssb_first_symbol_slot);
            assert(pss_entry.config.amplitude == convert_dB_to_amplitude(beta_pss));
            assert(srsvec::equal(pss_entry.config.ports, pdu.ports));

            // Assert SSS
            const auto& sss_entry = sss->get_entries()[0];
            assert(sss_entry.config.phys_cell_id == pdu.phys_cell_id);
            assert(sss_entry.config.ssb_first_subcarrier == ssb_first_subcarrier);
            assert(sss_entry.config.ssb_first_symbol == ssb_first_symbol_slot);
            assert(sss_entry.config.amplitude == 1.0F);
            assert(srsvec::equal(sss_entry.config.ports, pdu.ports));
          }
        }
      }
    }
  }

  return 0;
}
