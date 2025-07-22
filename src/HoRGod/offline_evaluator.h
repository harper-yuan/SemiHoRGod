#pragma once

#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/matrix.h>
#include <NTL/vector.h>
#include <emp-tool/emp-tool.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

#include "../io/netmp.h"
#include "../utils/circuit.h"
#include "ijmp.h"
#include "ot_provider.h"
#include "preproc.h"
#include "rand_gen_pool.h"
#include "sharing.h"
#include "types.h"
using namespace HoRGod;
namespace HoRGod {
class OfflineEvaluator {
  int id_;
  int security_param_;
  RandGenPool rgen_;

  std::shared_ptr<io::NetIOMP<5>> network_;
  std::shared_ptr<io::NetIOMP<5>> network_ot_;
  utils::LevelOrderedCircuit circ_;
  std::shared_ptr<ThreadPool> tpool_;
  PreprocCircuit<Ring> preproc_;
  std::vector<std::unique_ptr<OTProvider>> ot_;
  ImprovedJmp jump_;
  NTL::ZZ_pContext ZZ_p_ctx_;
  NTL::ZZ_pEContext ZZ_pE_ctx_;

  // Data members used for book-keeping across methods.
  std::vector<utils::FIn2Gate> mult_gates_;
  std::array<std::vector<Ring>, 3> ab_terms_;
  std::array<std::vector<Ring>, 6> c_terms_;

  // Used for running common coin protocol. Returns common random PRG key which
  // is then used to generate randomness for common coin output.
  emp::block commonCoinKey();

 public:
  // `network_1` and `network_2` are required to be distinct.
  // `network_2` is used for OT while `network_1` is used for all other tasks.
  OfflineEvaluator(int my_id, std::shared_ptr<io::NetIOMP<5>> network1,
                   std::shared_ptr<io::NetIOMP<5>> network2,
                   utils::LevelOrderedCircuit circ, int security_param,
                   int threads, int seed = 200);

  //reconstruct protocol
  std::vector<Ring> reconstruct(const std::array<std::vector<Ring>, 4>& recon_shares);
  std::vector<Ring> reconstruct(const std::vector<ReplicatedShare<Ring>>& shares);

  // Generate sharing of a random unknown value.
  static void randomShare(RandGenPool& rgen, ReplicatedShare<Ring>& share);
  // Generate sharing of a random value known to dealer (called by all parties
  // except the dealer).
  static void randomShareWithParty(int id, int dealer, RandGenPool& rgen,
                                   ReplicatedShare<Ring>& share);
  // Generate sharing of a random value known to party. Should be called by
  // dealer when other parties call other variant.
  static void randomShareWithParty(int id, RandGenPool& rgen, ReplicatedShare<Ring>& share, Ring& secret);
  
  ReplicatedShare<Ring> jshShare(int id, RandGenPool& rgen, int i, int j, int k);
  
  // Generate sharing of a random value, party i don't know the secret x_i
  ReplicatedShare<Ring> randomShareWithParty(int id, RandGenPool& rgen);
  // Following methods implement various preprocessing subprotocols.

  // Generate the random number r1, r2, r3, where number_random_id ∈ {0,1,2}
  ReplicatedShare<Ring> randomShareWithParty_for_trun(int id, RandGenPool& rgen, int number_random_id);
  //Used for multiplication to compute α_{xy}
  ReplicatedShare<Ring> compute_prod_mask(ReplicatedShare<Ring> mask_in1, ReplicatedShare<Ring> mask_in2);

  ReplicatedShare<Ring> compute_prod_mask_dot(vector<ReplicatedShare<Ring>> mask_in1, vector<ReplicatedShare<Ring>> mask_in2);

  //given sharings of three random number r1, r2, r3, generating the every bit sharing of r = r1 xor r2 xor r3
  vector<ReplicatedShare<Ring>> comute_random_r_every_bit_sharing(int id, ReplicatedShare<Ring> r_1_mask,
                                                                          ReplicatedShare<Ring> r_2_mask,
                                                                          ReplicatedShare<Ring> r_3_mask);
  // Set masks for each wire. Should be called before running any of the other
  // subprotocols.
  void setWireMasks(
      const std::unordered_map<utils::wire_t, int>& input_pid_map);
  // Computes S_1 and S_2 summands.
  void computeABCrossTerms();
  // Computes S_0 summands by running instances of disMult.
  void computeCCrossTerms();
  // Combines all computed summands to create output shares. Should be called
  // after 'computeABCrossTerms' and 'computeCCrossTerms' terminate.
  void combineCrossTerms();
  // Runs distributed ZKP to verify behaviour in 'computeABCrossTerms'. Should
  // be called after 'computeABCrossTerms' terminates.
  void distributedZKP();
  // Compute output commitments. Should be called after 'combineCrossTerms'.
  void computeOutputCommitments();

  PreprocCircuit<Ring> getPreproc();

  // Efficiently runs above subprotocols.
  PreprocCircuit<Ring> run(const utils::LevelOrderedCircuit& circ,
    const std::unordered_map<utils::wire_t, int>& input_pid_map,
    size_t security_param, int pid, emp::PRG& prg);

  // secure preprocessing
  PreprocCircuit<Ring> offline_setwire(
      const utils::LevelOrderedCircuit& circ,
      const std::unordered_map<utils::wire_t, int>& input_pid_map,
      size_t security_param, int pid, emp::PRG& prg);

  // Insecure preprocessing. All preprocessing data is generated in clear but
  // cast in a form that can be used in the online phase.
  static PreprocCircuit<Ring> dummy(
      const utils::LevelOrderedCircuit& circ,
      const std::unordered_map<utils::wire_t, int>& input_pid_map,
      size_t security_param, int pid, emp::PRG& prg);
};
};  // namespace HoRGod
