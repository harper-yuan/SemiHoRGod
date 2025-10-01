#include <io/netmp.h>
#include <SemiHoRGod/offline_evaluator.h>
#include <utils/circuit.h>
#include <SemiHoRGod/online_evaluator.h>
#include <algorithm>
#include <boost/program_options.hpp>
#include <cmath>
#include <iostream>
#include <memory>

#include "utils.h"

using namespace SemiHoRGod;
using json = nlohmann::json;
namespace bpo = boost::program_options;

utils::Circuit<Ring> generateCircuit(size_t gates_per_level, size_t depth) {
  utils::Circuit<Ring> circ;

  std::vector<utils::wire_t> level_inputs(gates_per_level);
  std::generate(level_inputs.begin(), level_inputs.end(),
                [&]() { return circ.newInputWire(); });

  for (size_t d = 0; d < depth; ++d) {
    std::vector<utils::wire_t> level_outputs(gates_per_level);

    for (size_t i = 0; i < gates_per_level - 1; ++i) {
      level_outputs[i] = circ.addGate(utils::GateType::kMul, level_inputs[i],
                                      level_inputs[i + 1]);
    }
    level_outputs[gates_per_level - 1] =
        circ.addGate(utils::GateType::kMul, level_inputs[gates_per_level - 1],
                     level_inputs[0]);

    level_inputs = std::move(level_outputs);
  }

  for (auto i : level_inputs) {
    circ.setAsOutput(i);
  }

  return circ;
}

void benchmark(const bpo::variables_map& opts) {
  bool save_output = false;
  std::string save_file;
  if (opts.count("output") != 0) {
    save_output = true;
    save_file = opts["output"].as<std::string>();
  }

  auto gates = opts["gates"].as<size_t>();
  auto pid = opts["pid"].as<size_t>();
  auto security_param = opts["security-param"].as<size_t>();
  auto threads = opts["threads"].as<size_t>();
  auto seed = opts["seed"].as<size_t>();
  auto repeat = opts["repeat"].as<size_t>();
  auto port = opts["port"].as<int>();
  auto depth = opts["depth"].as<size_t>();

  std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network1 = nullptr;
  std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network2 = nullptr;
  if (opts["localhost"].as<bool>()) {
    network1 = std::make_shared<io::NetIOMP<NUM_PARTIES>>(pid, port, nullptr, true);
    network2 = std::make_shared<io::NetIOMP<NUM_PARTIES>>(pid, port + 100, nullptr, true);
  } else {
    std::ifstream fnet(opts["net-config"].as<std::string>());
    if (!fnet.good()) {
      fnet.close();
      throw std::runtime_error("Could not open network config file");
    }
    json netdata;
    fnet >> netdata;
    fnet.close();

    std::vector<std::string> ipaddress(NUM_PARTIES);
    std::array<char*, NUM_PARTIES> ip{};
    for (size_t i = 0; i < NUM_PARTIES; ++i) {
      ipaddress[i] = netdata[i].get<std::string>();
      ip[i] = ipaddress[i].data();
    }

    network1 = std::make_shared<io::NetIOMP<NUM_PARTIES>>(pid, port, ip.data(), false);
    network2 =
        std::make_shared<io::NetIOMP<NUM_PARTIES>>(pid, port + 100, ip.data(), false);
  }

  json output_data;
  output_data["details"] = {{"gates", gates},
                            {"pid", pid},
                            {"security_param", security_param},
                            {"threads", threads},
                            {"seed", seed},
                            {"repeat", repeat}};
  output_data["benchmarks"] = json::array();

  std::cout << "--- Details ---\n";
  for (const auto& [key, value] : output_data["details"].items()) {
    std::cout << key << ": " << value << "\n";
  }
  std::cout << std::endl;

  auto circ = generateCircuit(gates, depth).orderGatesByLevel();

  std::unordered_map<utils::wire_t, int> input_pid_map;
  for (const auto& g : circ.gates_by_level[0]) {
    if (g->type == utils::GateType::kInp) {
      input_pid_map[g->out] = 0;
    }
  }

  for (size_t r = 0; r < repeat; ++r) {
    emp::PRG prg(&seed, 0);
    auto preproc =
        OfflineEvaluator::dummy(circ, input_pid_map, security_param, pid, prg);

    OnlineEvaluator eval(pid, network1, std::move(preproc), circ, security_param,
                         threads, seed);

    eval.setRandomInputs();
    
    
    network1->sync();
    network2->sync();

    network1->resetStats();
    network2->resetStats();
    CommPoint net1_st(*network1);
    CommPoint net2_st(*network2);
    TimePoint start;
    for (size_t i = 0; i < circ.gates_by_level.size(); ++i) {
      eval.evaluateGatesAtDepth_parallel(i, threads);
    }
    TimePoint end;
    CommPoint net1_ed(*network1);
    CommPoint net2_ed(*network2);

    auto time = end - start;
    auto res1 = net1_ed - net1_st;
    auto res2 = net2_ed - net2_st;
    for (size_t i = 0; i < res1.size(); ++i) {
      res1[i] += res2[i];
    }

    nlohmann::json rbench = {{"time", time}};
    rbench["communication"] = res1;

    size_t bytes_sent = 0;
    for (auto val : res1) {
      bytes_sent += val;
    }

    std::cout << "--- Repetition " << r + 1 << " ---\n";
    std::cout << "time: " << time << " ms\n";
    std::cout << "sent: " << bytes_sent << " bytes\n";
    std::cout << "throughput: " << (gates*depth * 1e3) / time //因为是ms，所以乘1000计算每秒的吞吐量
              << " triples per second\n";

    output_data["benchmarks"].push_back(std::move(rbench));
    if (save_output) {
      saveJson(output_data, save_file);
    }
    std::cout << std::endl;
  }

  output_data["stats"] = {{"peak_virtual_memory", peakVirtualMemory()},
                          {"peak_resident_set_size", peakResidentSetSize()}};

  std::cout << "--- Statistics ---\n";
  for (const auto& [key, value] : output_data["stats"].items()) {
    std::cout << key << ": " << value << "\n";
  }
  std::cout << std::endl;

  if (save_output) {
    saveJson(output_data, save_file);
  }
}

// clang-format off
bpo::options_description programOptions() {
  bpo::options_description desc("Following options are supported by config file too.");
  desc.add_options()
    ("gates,g", bpo::value<size_t>()->required(), "Number of multiplication gates.")
    ("pid,p", bpo::value<size_t>()->required(), "Party ID.")
    ("depth,d", bpo::value<size_t>()->required(), "Multiplicative depth of circuit.")
    ("security-param", bpo::value<size_t>()->default_value(128), "Security parameter in bits.")
    ("threads,t", bpo::value<size_t>()->default_value(1), "Number of threads (recommended 6).")
    ("seed", bpo::value<size_t>()->default_value(200), "Value of the random seed.")
    ("net-config", bpo::value<std::string>(), "Path to JSON file containing network details of all parties.")
    ("localhost", bpo::bool_switch(), "All parties are on same machine.")
    ("port", bpo::value<int>()->default_value(10000), "Base port for networking.")
    ("output,o", bpo::value<std::string>(), "File to save benchmarks.")
    ("repeat,r", bpo::value<size_t>()->default_value(1), "Number of times to run benchmarks.");

  return desc;
}
// clang-format on

int main(int argc, char* argv[]) {
  auto prog_opts(programOptions());

  bpo::options_description cmdline(
      "Benchmark triple generation in offline phase.");
  cmdline.add(prog_opts);
  cmdline.add_options()(
      "config,c", bpo::value<std::string>(),
      "configuration file for easy specification of cmd line arguments")(
      "help,h", "produce help message");

  bpo::variables_map opts;
  bpo::store(bpo::command_line_parser(argc, argv).options(cmdline).run(), opts);

  if (opts.count("help") != 0) {
    std::cout << cmdline << std::endl;
    return 0;
  }

  if (opts.count("config") > 0) {
    std::string cpath(opts["config"].as<std::string>());
    std::ifstream fin(cpath.c_str());

    if (fin.fail()) {
      std::cerr << "Could not open configuration file at " << cpath << "\n";
      return 1;
    }

    bpo::store(bpo::parse_config_file(fin, prog_opts), opts);
  }

  // Validate program options.
  try {
    bpo::notify(opts);

    // Check if output file already exists.
    if (opts.count("output") != 0) {
      std::ifstream ftemp(opts["output"].as<std::string>());
      if (ftemp.good()) {
        ftemp.close();
        throw std::runtime_error("Output file aready exists.");
      }
      ftemp.close();
    }

    if (!opts["localhost"].as<bool>() && (opts.count("net-config") == 0)) {
      throw std::runtime_error("Expected one of 'localhost' or 'net-config'");
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  try {
    benchmark(opts);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\nFatal error" << std::endl;
    return 1;
  }

  return 0;
}
