#define BOOST_TEST_MODULE offline_online
#include <emp-tool/emp-tool.h>
#include <io/netmp.h>
#include <SemiHoRGod/offline_evaluator.h>
#include <SemiHoRGod/online_evaluator.h>
#include <SemiHoRGod/sharing.h>
#include <SemiHoRGod/types.h>

#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/included/unit_test.hpp>
#include <cmath>
#include <future>
#include <memory>
#include <string>
#include <vector>

using namespace SemiHoRGod;
using namespace SemiHoRGod::utils;
namespace bdata = boost::unit_test::data;

constexpr int TEST_DATA_MAX_VAL = 1000;
constexpr int SECURITY_PARAM = 128;
constexpr int seed = 200;
constexpr int cm_threads = 1;

std::vector<Ring> generateRandomPermutation(emp::PRG& prg, uint64_t permutation_length) {
  std::vector<Ring> permutation;
  
  // 创建初始序列 [0, 1, 2, ..., n-1]
  for (int i = 0; i < permutation_length; ++i) {
    permutation.push_back(i);
  }
  
  // 使用 Fisher-Yates 洗牌算法
  for (int i = permutation_length - 1; i > 0; --i) {
      // 生成 [0, i] 范围内的随机数
      Ring rand_val;
      prg.random_data(&rand_val, sizeof(Ring));
      int j = rand_val % (i + 1);
      
      // 交换元素
      std::swap(permutation[i], permutation[j]);
  }
  return permutation;
}

BOOST_AUTO_TEST_SUITE(offline_online_evaluator)
BOOST_AUTO_TEST_CASE(permu_gate) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;
  Circuit<Ring> circ;
  std::vector<wire_t> vwa(nf);
  std::vector<wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate_permu(GateType::kPerm, vwa, vwb);
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  emp::PRG prg(&seed, 0);
  vector<Ring> data_vector = generateRandomPermutation(prg ,nf);
  vector<Ring> permutation_vector = generateRandomPermutation(prg ,nf);
  std::unordered_map<wire_t, Ring> input_map;
  std::unordered_map<wire_t, int> input_pid_map;

  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    // data_vector[i] = i;
    // permutation_vector[i] = nf-i-1;
    input_map[vwa[i]] = data_vector[i];
    input_map[vwb[i]] = permutation_vector[i];
    input_pid_map[vwa[i]] = 0;
    input_pid_map[vwb[i]] = 1;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NUM_PARTIES; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network_offline = std::make_shared<io::NetIOMP<NUM_PARTIES>>(i, 10002, nullptr, true);
      auto network = std::make_shared<io::NetIOMP<NUM_PARTIES>>(i, 10000, nullptr, true);
      
      OfflineEvaluator offline_eval(i, std::move(network_offline), nullptr, level_circ, SECURITY_PARAM, cm_threads);
      emp::PRG prg1(&seed, 0);
      auto preproc =  offline_eval.dummy_permutation(level_circ, input_pid_map, SECURITY_PARAM, i, prg1, data_vector, permutation_vector);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc), level_circ, SECURITY_PARAM, 1);
      // vector<Ring> result;
      // result.push_back(1);
      // return result;
      return online_eval.evaluateCircuit_perm(data_vector, permutation_vector);
    }));
  }

    std::cout<<"exp_output: ";
    for (const auto& num : exp_output) {
        std::cout << num << " ";
    }
//   for (auto& p : parties) {
//     auto output = p.get();

//     std::cout<<"output: ";
//     for (const auto& num : output) {
//         std::cout << num << " ";
//     }
//     std::cout<<"exp_output: ";
//     for (const auto& num : exp_output) {
//         std::cout << num << " ";
//     }
//     std::cout<<endl;
//     BOOST_TEST(output == exp_output);
//   }
}
BOOST_AUTO_TEST_SUITE_END()