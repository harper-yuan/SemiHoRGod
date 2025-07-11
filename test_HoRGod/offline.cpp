#define BOOST_TEST_MODULE offline
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <HoRGod/helpers.h>
#include <HoRGod/offline_evaluator.h>
#include <HoRGod/ot_provider.h>
#include <HoRGod/rand_gen_pool.h>
#include <utils/circuit.h>

#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/included/unit_test.hpp>
#include <future>
#include <memory>
#include <random>
#include <vector>

using namespace HoRGod;
namespace bdata = boost::unit_test::data;

constexpr int TEST_DATA_MAX_VAL = 1000;
constexpr int SECURITY_PARAM = 128;
constexpr int SEED = 200;

Ring compute_sum_alpha(int i, int j, const PreprocCircuit<Ring> & preproc_i, const PreprocCircuit<Ring> & preproc_j, int k) {
  Ring sum_alpha = 0;
  int pos = 0;
  for(int alpha_index_count = 0; alpha_index_count < 5; alpha_index_count++) {
    if (alpha_index_count == i) { //还需要一个x_i，这个值参与方i没有，需要找j要
      sum_alpha += preproc_j.gates[k]->mask[idxFromSenderAndReceiver(j, i)];
    }
    else {
      sum_alpha += preproc_i.gates[k]->mask[pos++];
    }
  }
  return sum_alpha;
}
BOOST_AUTO_TEST_SUITE(dummy_offline)

BOOST_AUTO_TEST_CASE(input_circuit) {
  utils::Circuit<Ring> circ;
  std::vector<utils::wire_t> input_wires;
  std::unordered_map<utils::wire_t, int> input_pid_map;
  for (size_t i = 0; i < 5; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 0;
    circ.setAsOutput(winp);
  }
  auto level_circ = circ.orderGatesByLevel();
  std::vector<std::future<PreprocCircuit<Ring>>> parties;
  parties.reserve(5);
  for (int i = 0; i < 5; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i, input_pid_map]() {
      auto network1 = std::make_shared<io::NetIOMP<5>>(i, 10000, nullptr, true);
      auto network2 = std::make_shared<io::NetIOMP<5>>(i, 11000, nullptr, true);
      OfflineEvaluator eval(i, std::move(network1), std::move(network2),
                            level_circ, SECURITY_PARAM, 5);
      emp::PRG prg(&SEED, 0);
      return eval.run(level_circ, input_pid_map, SECURITY_PARAM, i, prg);
    }));
  }

  std::vector<PreprocCircuit<Ring>> v_preproc;
  v_preproc.reserve(parties.size());
  for (auto& f : parties) {
    v_preproc.push_back(f.get());
  }

  for (int i = 0; i < 5; ++i) { //对每个参与方的预处理环进行判断
    BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
    for (int j = i + 1; j < 5; ++j) { //两两之间进行对比
      const auto& preproc_i = v_preproc[i];
      const auto& preproc_j = v_preproc[j];

      for (size_t k = 0; k < preproc_i.gates.size(); ++k) {
        auto mask_i = preproc_i.gates[k]->mask.commonTreeValues(i, j);
        auto mask_j = preproc_j.gates[k]->mask.commonTreeValues(j, i);
        BOOST_TEST(mask_i == mask_j);

        
      }
    }
  }
}

BOOST_AUTO_TEST_CASE(add_sub_2_circuit) {
  utils::Circuit<Ring> circ;
  std::vector<utils::wire_t> input_wires;
  std::unordered_map<utils::wire_t, int> input_pid_map;
  for (size_t i = 0; i < 5; ++i) {
    auto winp = circ.newInputWire();
    input_wires.push_back(winp);
    input_pid_map[winp] = 0;
  }
  auto w_aab =
      circ.addGate(utils::GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd =
      circ.addGate(utils::GateType::kSub, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(utils::GateType::kSub, w_aab, w_cmd);
  auto w_aout = circ.addGate(utils::GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();

  std::vector<std::future<PreprocCircuit<Ring>>> parties;
  parties.reserve(5);
  for (int i = 0; i < 5; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i, input_pid_map]() {
      auto network1 = std::make_shared<io::NetIOMP<5>>(i, 10000, nullptr, true);
      auto network2 = std::make_shared<io::NetIOMP<5>>(i, 11000, nullptr, true);
      OfflineEvaluator eval(i, std::move(network1), std::move(network2),
                            level_circ, SECURITY_PARAM, 5);
      emp::PRG prg(&SEED, 0);
      return eval.run(level_circ, input_pid_map, SECURITY_PARAM, i, prg);
    }));
  }

  std::vector<PreprocCircuit<Ring>> v_preproc;
  v_preproc.reserve(parties.size());
  for (auto& f : parties) {
    v_preproc.push_back(f.get());
  }

  for (int i = 0; i < 5; ++i) { //对每个参与方的预处理环进行判断
    BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
    for (int j = i + 1; j < 5; ++j) { //两两之间进行对比
      const auto& preproc_i = v_preproc[i];
      const auto& preproc_j = v_preproc[j];

      for (size_t k = 0; k < preproc_i.gates.size(); ++k) {
        auto mask_i = preproc_i.gates[k]->mask.commonTreeValues(i, j);
        auto mask_j = preproc_j.gates[k]->mask.commonTreeValues(j, i);
        BOOST_TEST(mask_i == mask_j);
      }
    }
  }
}

// BOOST_AUTO_TEST_CASE(mul_circuit) {
//   utils::Circuit<Ring> circ;
//   std::vector<utils::wire_t> input_wires;
//   std::unordered_map<utils::wire_t, int> input_pid_map;
//   for (size_t i = 0; i < 5; ++i) {
//     auto winp = circ.newInputWire();
//     input_wires.push_back(winp);
//     input_pid_map[winp] = 0;
//   }
//   auto w_aab =
//       circ.addGate(utils::GateType::kAdd, input_wires[0], input_wires[1]);
//   auto w_cmd =
//       circ.addGate(utils::GateType::kMul, input_wires[2], input_wires[3]);
//   auto w_mout = circ.addGate(utils::GateType::kMul, w_aab, w_cmd);
//   auto w_aout = circ.addGate(utils::GateType::kAdd, w_aab, w_cmd);
//   circ.setAsOutput(w_mout);
//   circ.setAsOutput(w_aout);
//   auto level_circ = circ.orderGatesByLevel();

//   std::vector<std::future<PreprocCircuit<Ring>>> parties;
//   parties.reserve(5);
//   for (int i = 0; i < 5; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i, input_pid_map]() {
//       auto network1 = std::make_shared<io::NetIOMP<5>>(i, 10000, nullptr, true);
//       auto network2 = std::make_shared<io::NetIOMP<5>>(i, 11000, nullptr, true);
//       OfflineEvaluator eval(i, std::move(network1), std::move(network2),
//                             level_circ, SECURITY_PARAM, 5);
//       return eval.run(input_pid_map);
//     }));
//   }

//   std::vector<PreprocCircuit<Ring>> v_preproc;
//   v_preproc.reserve(parties.size());
//   for (auto& f : parties) {
//     v_preproc.push_back(f.get());
//   }

//   for (int i = 0; i < 5; ++i) { //对每个参与方的预处理环进行判断
//     BOOST_TEST(v_preproc[i].gates.size() == level_circ.num_gates);
//     for (int j = i + 1; j < 5; ++j) { //两两之间进行对比
//       const auto& preproc_i = v_preproc[i];
//       const auto& preproc_j = v_preproc[j];

//       for (size_t k = 0; k < preproc_i.gates.size(); ++k) {
//         auto mask_i = preproc_i.gates[k]->mask.commonTreeValues(i, j);
//         auto mask_j = preproc_j.gates[k]->mask.commonTreeValues(j, i);
//         BOOST_TEST(mask_i == mask_j);

//         //判断一下乘法门
//         auto* gi =
//             dynamic_cast<PreprocMultGate<Ring>*>(preproc_i.gates[k].get());
//         if (gi != nullptr) {
//           auto* gj =
//               dynamic_cast<PreprocMultGate<Ring>*>(preproc_j.gates[k].get());
//           auto prod_i = gi->mask_prod.commonTreeValues(i, j);
//           auto prod_j = gj->mask_prod.commonTreeValues(j, i);
//           BOOST_TEST(prod_i == prod_j);
//         }

//         //下面检验alpha的累加是否正确
//         Ring sum_alpha_x = compute_sum_alpha(i, j, preproc_i, preproc_j, k);
//         Ring sum_alpha_y = compute_sum_alpha(i, j, preproc_i, preproc_j, k);
//         // BOOST_TEST(sum_alpha == preproc_i.gates[k]->mask_value);
//       }
//     }
//   }
// }
BOOST_AUTO_TEST_SUITE_END()