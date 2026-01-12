#pragma once

#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <NTL/matrix.h>
#include <NTL/vector.h>
#include <emp-tool/emp-tool.h>
#include <map>
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <tuple>
#include "../io/netmp.h"
#include "../utils/circuit.h"
#include "ijmp.h"
#include "preproc.h"
#include "rand_gen_pool.h"
#include "sharing.h"
#include "types.h"
using namespace SemiHoRGod;
namespace SemiHoRGod {
// Helper struct for managing buffer offsets in 7PC
struct ChannelOffsets {
    std::map<std::tuple<int, int, int>, size_t> offsets;

    size_t getAndIncrement(int i, int j, int k) {
        auto key = std::make_tuple(i, j, k);
        size_t off = offsets[key];
        offsets[key] += sizeof(Ring);
        return off;
    }
    
    void reset() {
        offsets.clear();
    }
};

class OfflineEvaluator {
  int id_;
  int security_param_;
  RandGenPool rgen_;

  std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network_;
  std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network_ot_;
  utils::LevelOrderedCircuit circ_;
  std::shared_ptr<ThreadPool> tpool_;
  PreprocCircuit<Ring> preproc_;
  ImprovedJmp jump_;

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
  OfflineEvaluator(int my_id, std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network1,
                   std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network2,
                   utils::LevelOrderedCircuit circ, int security_param,
                   int threads, int seed = 200);

  //reconstruct protocol
  std::vector<Ring> elementwise_sum(const std::array<std::vector<Ring>, NUM_RSS>& recon_shares, int i, int j, int k);
  std::vector<Ring> reconstruct(const std::array<std::vector<Ring>, NUM_RSS>& recon_shares);
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
  std::vector<ReplicatedShare<Ring>> randomShareWithParty_for_trun(int id, RandGenPool& rgen, std::vector<std::pair<int, int>> indices);
  //Used for multiplication to compute α_{xy}
  ReplicatedShare<Ring> compute_prod_mask(ReplicatedShare<Ring> mask_in1, ReplicatedShare<Ring> mask_in2);
  ReplicatedShare<Ring> compute_prod_mask_part1(ReplicatedShare<Ring> mask_in1, ReplicatedShare<Ring> mask_in2);
  void compute_prod_mask_part2(ReplicatedShare<Ring>& mask_prod, size_t idx);

  ReplicatedShare<Ring> compute_prod_mask_dot(vector<ReplicatedShare<Ring>> mask_in1, vector<ReplicatedShare<Ring>> mask_in2);
  ReplicatedShare<Ring> compute_prod_mask_dot_part1(vector<ReplicatedShare<Ring>> mask_in1_vec, vector<ReplicatedShare<Ring>> mask_in2_vec);
  void compute_prod_mask_dot_part2(ReplicatedShare<Ring>& mask_prod, size_t idx);
  // === 新增这两个声明 ===
  void compute_prod_mask_part2(ReplicatedShare<Ring>& mask_prod, ChannelOffsets& offsets);
  void compute_prod_mask_dot_part2(ReplicatedShare<Ring>& mask_prod, ChannelOffsets& offsets);
  // ======================

  //given sharings of three random number r1, r2, r3, generating the every bit sharing of r = r1 xor r2 xor r3
  ReplicatedShare<Ring> bool_mul(ReplicatedShare<Ring> a, ReplicatedShare<Ring> b);
  ReplicatedShare<Ring> bool_mul_by_indices(vector<ReplicatedShare<Ring>> r_mask_vec, vector<int> indices);
  std::tuple<vector<ReplicatedShare<Ring>>, vector<ReplicatedShare<Ring>>> comute_random_r_every_bit_sharing(int id, 
                                                                                                            vector<ReplicatedShare<Ring>> r_mask_vec, 
                                                                                                            std::vector<std::pair<int, int>> indices);
                                                                                            

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
  PreprocCircuit<Ring> offline_setwire_no_batch(
      const utils::LevelOrderedCircuit& circ,
      const std::unordered_map<utils::wire_t, int>& input_pid_map,
      size_t security_param, int pid, emp::PRG& prg);

  // Insecure preprocessing. All preprocessing data is generated in clear but
  // cast in a form that can be used in the online phase.
  static PreprocCircuit<Ring> dummy(
      const utils::LevelOrderedCircuit& circ,
      const std::unordered_map<utils::wire_t, int>& input_pid_map,
      size_t security_param, int pid, emp::PRG& prg);
  
  std::array<vector<Ring>, 22> reshare_gen_random_vector(int pid, RandGenPool& rgen, int array_length);
  PreprocCircuit_permutation<Ring> dummy_permutation(
      const utils::LevelOrderedCircuit& circ,
      const std::unordered_map<utils::wire_t, int>& input_pid_map,
      size_t security_param, int pid, emp::PRG& prg, vector<Ring>& data_vector, vector<Ring>& permutation_vector);
};
};  // namespace SemiHoRGod
