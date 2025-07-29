#define BOOST_TEST_MODULE online
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

BOOST_AUTO_TEST_SUITE(online_evaluator)

// BOOST_AUTO_TEST_CASE(reconstruct) {
//   const int num_shares = 50;

//   auto seed_block = emp::makeBlock(0, 200);
//   emp::PRG prg(&seed_block);
//   std::vector<DummyShare<Ring>> dummy_shares(num_shares); //包含50个默认的DummyShare变量
//   for (size_t i = 0; i < num_shares; ++i) {
//     dummy_shares[i].randomize(prg);
//   }

//   std::vector<std::future<std::vector<Ring>>> parties;
//   for (int i = 0; i < NP; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
//       PreprocCircuit<Ring> preproc;
//       LevelOrderedCircuit circ;
//       OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
//                                   circ, SECURITY_PARAM, 1);
//       std::vector<ReplicatedShare<Ring>> shares(num_shares);
//       for (size_t j = 0; j < num_shares; ++j) {
//         shares[j] = dummy_shares[j].getRSS(i);
//       }

//       return online_eval.reconstruct(shares);
//     }));
//   }

//   for (auto& p : parties) {
//     auto res = p.get();

//     for (size_t i = 0; i < num_shares; ++i) {
//       BOOST_TEST(res[i] == dummy_shares[i].secret());
//     }
//   }
// }

// BOOST_DATA_TEST_CASE(no_op_circuit,
//                      bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
//                      input, idx) {
//   auto seed = emp::makeBlock(100, 200);

//   Circuit<Ring> circ;
//   auto wa = circ.newInputWire();
//   circ.setAsOutput(wa);
//   auto level_circ = circ.orderGatesByLevel();

//   std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}}; //意思是参与方0，有wire id = wa
//   std::unordered_map<wire_t, Ring> inputs = {{wa, input}}; //wire id = wa输入数据input
//   auto exp_output = circ.evaluate(inputs);

//   std::vector<std::future<std::vector<Ring>>> parties;
//   for (int i = 0; i < NP; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
//       emp::PRG prg(&seed, 0);
//       auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map, SECURITY_PARAM, i, prg); //每个i需要预处理
//       OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);

//       return online_eval.evaluateCircuit(inputs);
//     }));
//   }

//   for (auto& p : parties) {
//     auto output = p.get();
//     BOOST_TEST(output == exp_output);
//   }
// }

// BOOST_DATA_TEST_CASE(add_gate,
//                      bdata::random(0, TEST_DATA_MAX_VAL) ^
//                          bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
//                      input_a, input_b, idx) {
//   auto seed = emp::makeBlock(100, 200);

//   Circuit<Ring> circ;
//   auto wa = circ.newInputWire(); //wa=0
//   auto wb = circ.newInputWire(); //wb=1
//   auto wsum = circ.addGate(GateType::kAdd, wa, wb); //加2条线的输入，wsum=2
//   circ.setAsOutput(wsum);
//   auto level_circ = circ.orderGatesByLevel();

//   std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
//   std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
//   auto exp_output = circ.evaluate(inputs);

//   std::vector<std::future<std::vector<Ring>>> parties;
//   for (int i = 0; i < NP; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
//       emp::PRG prg(&seed, 0);
//       auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
//                                              SECURITY_PARAM, i, prg);
//       OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);

//       return online_eval.evaluateCircuit(inputs);
//     }));
//   }
//   for (auto& p : parties) {
//     auto output = p.get();
//     BOOST_TEST(output == exp_output);
//   }
// }

// BOOST_DATA_TEST_CASE(sub_gate,
//                      bdata::random(0, TEST_DATA_MAX_VAL) ^
//                          bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
//                      input_a, input_b, idx) {
//   auto seed = emp::makeBlock(100, 200);

//   Circuit<Ring> circ;
//   auto wa = circ.newInputWire();
//   auto wb = circ.newInputWire();
//   auto wdiff = circ.addGate(GateType::kSub, wa, wb);
//   circ.setAsOutput(wdiff);
//   auto level_circ = circ.orderGatesByLevel();

//   std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
//   std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
//   auto exp_output = circ.evaluate(inputs);

//   std::vector<std::future<std::vector<Ring>>> parties;
//   for (int i = 0; i < NP; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
//       emp::PRG prg(&seed, 0);
//       auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
//                                              SECURITY_PARAM, i, prg);
//       OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);

//       return online_eval.evaluateCircuit(inputs);
//     }));
//   }

//   for (auto& p : parties) {
//     auto output = p.get();
//     BOOST_TEST(output == exp_output);
//   }
// }

// BOOST_DATA_TEST_CASE(const_add_gate,
//                      bdata::random(0, TEST_DATA_MAX_VAL) ^
//                          bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
//                      input_a, input_b, idx) {
//   auto seed = emp::makeBlock(100, 200);
//   Circuit<Ring> circ;
//   auto wa = circ.newInputWire();
//   auto wsum = circ.addConstOpGate(GateType::kConstAdd, wa, static_cast<Ring>(input_b));
//   circ.setAsOutput(wsum);
//   auto level_circ = circ.orderGatesByLevel();

//   std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
//   std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}};
//   auto exp_output = circ.evaluate({{wa, input_a}});

//   std::vector<std::future<std::vector<Ring>>> parties;
//   for (int i = 0; i < NP; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
//       emp::PRG prg(&seed, 0);
//       auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
//                                              SECURITY_PARAM, i, prg);
//       OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);

//       return online_eval.evaluateCircuit(inputs);
//       // vector<Ring> a(1,0);
//       // return a;
//     }));
//   }

//   for (auto& p : parties) {
//     auto output = p.get();
//     BOOST_TEST(output == exp_output);
//   }
// }

// BOOST_DATA_TEST_CASE(const_mul_gate,
//                      bdata::random(0, TEST_DATA_MAX_VAL) ^
//                          bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
//                      input_a, input_b, idx) {
//   auto seed = emp::makeBlock(100, 200);
//   Circuit<Ring> circ;
//   auto wa = circ.newInputWire();
//   auto wsum = circ.addConstOpGate(GateType::kConstMul, wa, static_cast<Ring>(input_b));
//   circ.setAsOutput(wsum);
//   auto level_circ = circ.orderGatesByLevel();

//   std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
//   std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}};
//   auto exp_output = circ.evaluate({{wa, input_a}});

//   std::vector<std::future<std::vector<Ring>>> parties;
//   for (int i = 0; i < NP; ++i) {
//     parties.push_back(std::async(std::launch::async, [&, i]() {
//       auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
//       emp::PRG prg(&seed, 0);
//       auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
//                                              SECURITY_PARAM, i, prg);
//       OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
//                                   level_circ, SECURITY_PARAM, 1);

//       return online_eval.evaluateCircuit(inputs);
//       // vector<Ring> a(1,0);
//       // return a;
//     }));
//   }

//   for (auto& p : parties) {
//     auto output = p.get();
//     BOOST_TEST(output == exp_output);
//   }
// }

BOOST_DATA_TEST_CASE(mul_gate,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, idx) {
  auto seed = emp::makeBlock(100, 200);

  Circuit<Ring> circ;
  auto wa = circ.newInputWire();
  auto wb = circ.newInputWire();
  auto wprod = circ.addGate(GateType::kMul, wa, wb);
  circ.setAsOutput(wprod);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_AUTO_TEST_CASE(dotp_gate) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;
  Circuit<Ring> circ;
  std::vector<wire_t> vwa(nf);
  std::vector<wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate(GateType::kDotprod, vwa, vwb);
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, Ring> input_map;
  std::unordered_map<wire_t, int> input_pid_map;
  std::mt19937 gen(200);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    input_map[vwa[i]] = distrib(gen);
    input_map[vwb[i]] = distrib(gen);
    input_pid_map[vwa[i]] = 0;
    input_pid_map[vwb[i]] = 1;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(input_map);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}

BOOST_AUTO_TEST_CASE(tr_dotp_gate) {
  auto seed = emp::makeBlock(100, 200);
  int nf = 10;

  Circuit<Ring> circ;
  std::vector<wire_t> vwa(nf);
  std::vector<wire_t> vwb(nf);
  for (int i = 0; i < nf; i++) {
    vwa[i] = circ.newInputWire();
    vwb[i] = circ.newInputWire();
  }
  auto wdotp = circ.addGate(GateType::kTrdotp, vwa, vwb); //向量相乘然后截断
  circ.setAsOutput(wdotp);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, Ring> input_map;
  std::unordered_map<wire_t, int> input_pid_map;
  std::mt19937 gen(200);
  std::uniform_int_distribution<Ring> distrib(0, TEST_DATA_MAX_VAL);
  for (size_t i = 0; i < nf; ++i) {
    input_map[vwa[i]] = distrib(gen);
    input_map[vwb[i]] = distrib(gen);
    input_pid_map[vwa[i]] = 0;
    input_pid_map[vwb[i]] = 1;
  }

  auto exp_output = circ.evaluate(input_map);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(input_map);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    bool check = (output[0] == exp_output[0]) ||
                 (output[0] == (exp_output[0] + 1)) ||
                 (output[0] == (exp_output[0] - 1));
    BOOST_TEST(check);
  }
}

BOOST_DATA_TEST_CASE(depth_2_circuit,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, input_c, input_d, idx) {
  auto seed = emp::makeBlock(100, 200);
  std::vector<int> vinputs = {input_a, input_b, input_c, input_d};

  Circuit<Ring> circ;
  std::vector<wire_t> input_wires;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_wires.push_back(circ.newInputWire());
  }
  auto w_aab = circ.addGate(GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd = circ.addGate(GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map;
  std::unordered_map<wire_t, Ring> inputs;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_pid_map[input_wires[i]] = i % 4;
    inputs[input_wires[i]] = vinputs[i];
  }
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}
BOOST_DATA_TEST_CASE(kCmp_gate, bdata::xrange(2), idx) {
  std::random_device rd;       // 真随机数种子（硬件熵源）
  std::mt19937 gen(rd());      // Mersenne Twister 伪随机数引擎
  std::uniform_int_distribution<> dis(0, 1ULL<<BITS_GAMMA - 1); // 随机产生gamma bit的数据

  // 2. 生成随机数
  int random_num = dis(gen);
  Circuit<Ring> circ;
  uint64_t a = random_num;
  // uint64_t a = 200525;
  Ring input_a = static_cast<Ring>(a);
  std::vector<BoolRing> bits = bitDecompose(input_a);

  auto wa = circ.newInputWire();
  auto wmsb = circ.addGate(GateType::kCmp, wa);
  circ.setAsOutput(wmsb);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&emp::zero_block, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);
      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output[0] == exp_output[0]);
  }
}

BOOST_DATA_TEST_CASE(relu_gate, bdata::xrange(2), idx) {
  std::random_device rd;       // 真随机数种子（硬件熵源）
  std::mt19937 gen(rd());      // Mersenne Twister 伪随机数引擎
  std::uniform_int_distribution<> dis(0, 1ULL<<BITS_GAMMA - 1); // 随机产生gamma bit的数据

  // 2. 生成随机数
  int random_num = dis(gen);
  Circuit<Ring> circ;
  // uint64_t a = 5;
  uint64_t a = random_num;

  Ring input_a = static_cast<Ring>(a);
  std::vector<BoolRing> bits = bitDecompose(input_a);

  auto wa = circ.newInputWire();
  auto wrelu = circ.addGate(GateType::kRelu, wa);
  circ.setAsOutput(wrelu);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&emp::zero_block, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output[0] == exp_output[0]);
  }
}



BOOST_AUTO_TEST_CASE(double_relu_gate) {
  std::random_device rd;       // 真随机数种子（硬件熵源）
  std::mt19937 gen(rd());      // Mersenne Twister 伪随机数引擎
  std::uniform_int_distribution<> dis(0, 1ULL<<(BITS_GAMMA/2) - 1); // 随机产生gamma/2 bit的数据，且确保乘完后不会溢出

  // 2. 生成随机数
  uint64_t a, b;
  a = dis(gen);
  b = dis(gen);
  // a = 5;
  // b = 2;

  Circuit<Ring> circ;
  
  Ring input_a = static_cast<Ring>(a);
  Ring input_b = b;
  auto wa = circ.newInputWire();
  auto wb = circ.newInputWire();
  auto wrelu_a = circ.addGate(GateType::kRelu, wa);
  auto wprod = circ.addGate(GateType::kMul, wrelu_a, wb);

  auto wrelu_prod = circ.addGate(GateType::kRelu, wprod);

  circ.setAsOutput(wrelu_a);
  circ.setAsOutput(wprod);
  circ.setAsOutput(wrelu_prod);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map = {{wa, 0}, {wb, 1}};
  std::unordered_map<wire_t, Ring> inputs = {{wa, input_a}, {wb, input_b}};
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&emp::zero_block, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output[0] == exp_output[0]);
    BOOST_TEST(output[1] == exp_output[1]);
    BOOST_TEST(output[2] == exp_output[2]);
  }
}

BOOST_DATA_TEST_CASE(multiple_inputs_same_party,
                     bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^
                         bdata::random(0, TEST_DATA_MAX_VAL) ^ bdata::xrange(1),
                     input_a, input_b, input_c, input_d, idx) {
  auto seed = emp::makeBlock(100, 200);
  std::vector<int> vinputs = {input_a, input_b, input_c, input_d};

  Circuit<Ring> circ;
  std::vector<wire_t> input_wires;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_wires.push_back(circ.newInputWire());
  }
  auto w_aab = circ.addGate(GateType::kAdd, input_wires[0], input_wires[1]);
  auto w_cmd = circ.addGate(GateType::kMul, input_wires[2], input_wires[3]);
  auto w_mout = circ.addGate(GateType::kMul, w_aab, w_cmd);
  auto w_aout = circ.addGate(GateType::kAdd, w_aab, w_cmd);
  circ.setAsOutput(w_mout);
  circ.setAsOutput(w_aout);
  auto level_circ = circ.orderGatesByLevel();

  std::unordered_map<wire_t, int> input_pid_map;
  std::unordered_map<wire_t, Ring> inputs;
  for (size_t i = 0; i < vinputs.size(); ++i) {
    input_pid_map[input_wires[i]] = i % 2;
    inputs[input_wires[i]] = vinputs[i];
  }
  auto exp_output = circ.evaluate(inputs);

  std::vector<std::future<std::vector<Ring>>> parties;
  for (int i = 0; i < NP; ++i) {
    parties.push_back(std::async(std::launch::async, [&, i]() {
      auto network = std::make_shared<io::NetIOMP<NP>>(i, 10000, nullptr, true);
      emp::PRG prg(&seed, 0);
      auto preproc = OfflineEvaluator::dummy(level_circ, input_pid_map,
                                             SECURITY_PARAM, i, prg);
      OnlineEvaluator online_eval(i, std::move(network), std::move(preproc),
                                  level_circ, SECURITY_PARAM, 1);

      return online_eval.evaluateCircuit(inputs);
    }));
  }

  for (auto& p : parties) {
    auto output = p.get();
    BOOST_TEST(output == exp_output);
  }
}
BOOST_AUTO_TEST_SUITE_END()
