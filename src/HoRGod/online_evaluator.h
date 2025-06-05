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
using namespace HoRGod;
namespace HoRGod {
class OnlineEvaluator {
  int id_; //标记自身的id
  int security_param_; //安全参数
  Ring msb_temp_value_ ;
  Ring relu_temp_value_ ;
  RandGenPool rgen_;  //随机数生成器
  std::shared_ptr<io::NetIOMP<5>> network_;
  PreprocCircuit<Ring> preproc_; //预处理环
  utils::LevelOrderedCircuit circ_; //
  std::vector<Ring> wires_; //Ring = uint64_t
  ImprovedJmp jump_; //联合消息传输协议
  std::shared_ptr<ThreadPool> tpool_; //多线程
  utils::LevelOrderedCircuit msb_circ_;

  // Reconstruct shares stored in recon_shares_.
  // Argument format is more suitable for communication compared to
  // vector<ReplicatedShare<Ring>>.
  std::vector<Ring> reconstruct(
      const std::array<std::vector<Ring>, 4>& recon_shares); //通过秘密重构数据

  std::array<std::vector<Ring>, 4> reluEvaluate(
      const std::vector<utils::FIn1Gate>& relu_gates);

  std::array<std::vector<Ring>, 4> msbEvaluate(
      const std::vector<utils::FIn1Gate>& msb_gates);

 public:
  OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP<5>> network,
                  PreprocCircuit<Ring> preproc, utils::LevelOrderedCircuit circ,
                  int security_param, int threads, int seed = 200);

  OnlineEvaluator(int id, std::shared_ptr<io::NetIOMP<5>> network,
                  PreprocCircuit<Ring> preproc, utils::LevelOrderedCircuit circ,
                  int security_param, std::shared_ptr<ThreadPool> tpool,
                  int seed = 200);

  // Secret share inputs.
  // 'inputs' is a mapping from wire id to input value with entries for only
  // those inputs provided by this party.
  void setInputs(const std::unordered_map<utils::wire_t, Ring>& inputs);
  // Set random values on circuit input wires.
  void setRandomInputs();
  // Evaluate gates at depth 'depth'.
  // This method should be called in increasing order of 'depth' values.
  void evaluateGatesAtDepth(size_t depth);
  // Compute and returns circuit outputs.
  std::vector<Ring> getOutputs();
  // Utility function to reconstruct vector of shares.
  std::vector<Ring> reconstruct(
      const std::vector<ReplicatedShare<Ring>>& shares);

  // Evaluate online phase for circuit.
  std::vector<Ring> evaluateCircuit(
      const std::unordered_map<utils::wire_t, Ring>& inputs);
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
      int id, const std::array<std::vector<BoolRing>, 4>& recon_shares,
      io::NetIOMP<5>& network, ImprovedJmp& jump, ThreadPool& tpool);

  void evaluateGatesAtDepth(size_t depth, io::NetIOMP<5>& network,
                            ImprovedJmp& jump, ThreadPool& tpool);
  void evaluateAllLevels(io::NetIOMP<5>& network, ImprovedJmp& jump,
                         ThreadPool& tpool);

  std::vector<std::vector<BoolRing>> getOutputShares(io::NetIOMP<5>& network,
                                      ImprovedJmp& jump, ThreadPool& tpool);
};
};  // namespace HoRGod
