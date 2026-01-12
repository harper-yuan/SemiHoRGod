#pragma once

#include <emp-tool/emp-tool.h>
#include <array>
#include <vector>
#include <mutex> // 新增: 用于互斥锁
#include "../io/netmp.h"
#include "types.h"
namespace SemiHoRGod {

// Manages instances of jump.
class ImprovedJmp {
  int id_;
  
  // 互斥锁，用于保护 jumpUpdate 中的数据写入
  std::mutex mtx_;

  std::array<std::array<std::array<bool, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> send_;
  std::array<std::array<std::array<emp::Hash, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> send_hash_;
  std::array<std::array<std::array<std::vector<uint8_t>, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> send_values_;
  
  std::array<std::array<std::array<size_t, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> recv_lengths_;
  
  std::array<std::array<std::array<bool, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> is_received1_;
  std::array<std::array<std::array<bool, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> is_received2_;
  std::array<std::array<std::array<bool, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> is_received3_;
  
  std::array<std::array<std::array<std::vector<uint8_t>, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> recv_values1_;
  std::array<std::array<std::array<std::vector<uint8_t>, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> recv_values2_;
  std::array<std::array<std::array<std::vector<uint8_t>, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> recv_values3_;
  
  std::array<std::array<std::array<std::vector<uint8_t>, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> final_recv_values_;
  
  uint64_t counter = 0;

  static bool isHashSender(int sender, int other_sender1, int other_sender2, int receiver);

 public:
  explicit ImprovedJmp(int my_id);

  void reset();

  void jumpUpdate(int sender1, int sender2, int sender3, int receiver, size_t nbytes,
                  const void* data = nullptr);
  
  // 注意：虽然签名保留了 ThreadPool 以兼容旧代码，但在内部我们不再使用它来避免死锁
  void communicate(io::NetIOMP<NUM_PARTIES>& network, ThreadPool& tpool);
  
  const std::vector<uint8_t>& getValues(int sender1, int sender2, int sender3);
  void checkConsistency(io::NetIOMP<NUM_PARTIES>& network);
  size_t calculate_total_communication() 
  {
      size_t total_bits = 0;
      // 遍历所有发送方 (Sender PID)
      for (size_t sender = 0; sender < recv_lengths_.size(); ++sender) {
          // 遍历所有接收方 (Receiver PID)
          for (size_t receiver = 0; receiver < recv_lengths_[sender].size(); ++receiver) {
              // 遍历所有上下文/连接 (Context ID)
              for (size_t context = 0; context < recv_lengths_[sender][receiver].size(); ++context) {
                  total_bits += recv_lengths_[sender][receiver][context];
              }
          }
      }
      return total_bits;
  }
};

};  // namespace SemiHoRGod