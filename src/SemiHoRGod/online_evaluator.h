#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "../io/netmp.h"
#include "../utils/circuit.h"
// #include "jump_provider.h"
#include "ijmp.h"
#include "preproc.h"
#include "rand_gen_pool.h"
#include "sharing.h"
#include "types.h"
#include "helpers.h"
using namespace SemiHoRGod;
namespace SemiHoRGod {
class OnlineEvaluator {
  int id_; //标记自身的id
  int security_param_; //安全参数
  Ring msb_temp_value_ ;
  Ring relu_temp_value_ ;
  RandGenPool rgen_;  //随机数生成器
  std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network_;
  PreprocCircuit<Ring> preproc_; //预处理环
  utils::LevelOrderedCircuit circ_; //
  std::vector<Ring> wires_; //Ring = uint64_t
  ImprovedJmp jump_; //联合消息传输协议
  std::shared_ptr<ThreadPool> tpool_; //多线程
  utils::LevelOrderedCircuit msb_circ_;

  std::vector<Ring> my_betas_;
  std::vector<Ring> my_betas_perm_;
  vector<ReplicatedShare<Ring>> data_sharing_vec_;
  PreprocCircuit_permutation<Ring> preproc_perm_;

  // Reconstruct shares stored in recon_shares_.
  // Argument format is more suitable for communication compared to
  // vector<ReplicatedShare<Ring>>.
  std::vector<Ring> elementwise_sum(const std::array<std::vector<Ring>, NUM_RSS>& recon_shares, int i, int j, int k);
  std::vector<Ring> reconstruct(
      const std::array<std::vector<Ring>, NUM_RSS>& recon_shares); //通过秘密重构数据

  std::array<std::vector<Ring>, NUM_RSS> reluEvaluate(
      const std::vector<utils::FIn1Gate>& relu_gates);

  std::array<std::vector<Ring>, NUM_RSS> msbEvaluate(
      const std::vector<utils::FIn1Gate>& msb_gates);

 public:
  OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network,
                  PreprocCircuit<Ring> preproc, utils::LevelOrderedCircuit circ,
                  int security_param, int threads, int seed = 200);

  OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network,
                  PreprocCircuit<Ring> preproc, utils::LevelOrderedCircuit circ,
                  int security_param, std::shared_ptr<ThreadPool> tpool,
                  int seed = 200);

   OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network,
                  PreprocCircuit_permutation<Ring> preproc_perm, utils::LevelOrderedCircuit circ,
                  int security_param, int threads, int seed = 200);
  // Secret share inputs.
  // 'inputs' is a mapping from wire id to input value with entries for only
  // those inputs provided by this party.
  void setInputs(const std::unordered_map<utils::wire_t, Ring>& inputs);
  void setBetaVectors(const std::vector<Ring>& my_betas, const std::vector<Ring>& my_beta_perm);
  void setInputs_perm(vector<Ring> data_vector, vector<Ring> permutation_vector);
  // Set random values on circuit input wires.
  void setRandomInputs();
  // Evaluate gates at depth 'depth'.
  // This method should be called in increasing order of 'depth' values.
  void evaluateGatesAtDepth(size_t depth);
  // Compute and returns circuit outputs.
  std::vector<Ring> getOutputs();
  std::vector<Ring> getOutputs_perm();
  // Utility function to reconstruct vector of shares.
  std::vector<Ring> reconstruct(
      const std::vector<ReplicatedShare<Ring>>& shares);

  // Evaluate online phase for circuit.
  std::vector<Ring> evaluateCircuit(
      const std::unordered_map<utils::wire_t, Ring>& inputs);

  std::vector<Ring> evaluateCircuit_perm(vector<Ring> data_vector, vector<Ring> permutation_vector);
};

// Helper class to efficiently evaluate online phase on boolean circuit.
struct BoolEvaluator {
  int id;
  std::vector<std::vector<BoolRing>> vwires;
  std::vector<preprocg_ptr_t<BoolRing>*> vpreproc;
  utils::LevelOrderedCircuit circ;

  explicit BoolEvaluator(int my_id,
                         std::vector<preprocg_ptr_t<BoolRing>*> vpreproc,
                         utils::LevelOrderedCircuit circ);

  static std::vector<BoolRing> reconstruct(
      int id, const std::array<std::vector<BoolRing>, NUM_RSS>& recon_shares,
      io::NetIOMP<NUM_PARTIES>& network, ImprovedJmp& jump, ThreadPool& tpool);

  void evaluateGatesAtDepth(size_t depth, io::NetIOMP<NUM_PARTIES>& network,
                            ImprovedJmp& jump, ThreadPool& tpool);
  void evaluateAllLevels(io::NetIOMP<NUM_PARTIES>& network, ImprovedJmp& jump,
                         ThreadPool& tpool);

  std::vector<std::vector<BoolRing>> getOutputShares(io::NetIOMP<NUM_PARTIES>& network,
                                      ImprovedJmp& jump, ThreadPool& tpool);
};
};  // namespace SemiHoRGod
