#pragma once

#include <emp-tool/emp-tool.h>

#include <array>

#include "../io/netmp.h"
#include "types.h"
namespace SemiHoRGod {

// Manages instances of jump.
class ImprovedJmp {
  int id_;
  // std::array<std::array<bool, NP >, NP > send_;
  // std::array<std::array<emp::Hash, NP >, NP > send_hash_;
  // std::array<std::array<std::vector<uint8_t>, NP >, NP > send_values_;
  // std::array<std::array<size_t, NP >, NP > recv_lengths_;
  // std::array<std::array<std::vector<uint8_t>, NP >, NP > recv_values_;
  std::array<std::array<std::array<bool, NP >, NP >, NP > send_;
  std::array<std::array<std::array<emp::Hash, NP >, NP >, NP > send_hash_;
  std::array<std::array<std::array<std::vector<uint8_t>, NP >, NP >, NP > send_values_;
  std::array<std::array<std::array<size_t, NP >, NP >, NP > recv_lengths_;
  std::array<std::array<std::array<bool, NP >, NP >, NP > is_received1_;
  std::array<std::array<std::array<bool, NP >, NP >, NP > is_received2_;
  std::array<std::array<std::array<bool, NP >, NP >, NP > is_received3_;
  std::array<std::array<std::array<std::vector<uint8_t>, NP >, NP >, NP > recv_values1_;
  std::array<std::array<std::array<std::vector<uint8_t>, NP >, NP >, NP > recv_values2_;
  std::array<std::array<std::array<std::vector<uint8_t>, NP >, NP >, NP > recv_values3_;
  std::array<std::array<std::array<std::vector<uint8_t>, NP >, NP >, NP > final_recv_values_;

  static bool isHashSender(int sender, int other_sender, int receiver);

 public:
  explicit ImprovedJmp(int my_id);

  void reset();

  void jumpUpdate(int sender1, int sender2, int sender3, int receiver, size_t nbytes,
                  const void* data = nullptr);
  void communicate(io::NetIOMP<NP>& network, ThreadPool& tpool);
  const std::vector<uint8_t>& getValues(int sender1, int sender2, int sender3);
};

};  // namespace SemiHoRGod
