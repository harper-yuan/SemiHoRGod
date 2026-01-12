#include "ijmp.h"
#include <mutex>
#include <algorithm>
#include <boost/format.hpp>
#include <future>
#include <thread> // 关键新增：用于 std::thread
#include <vector>
#include <stdexcept>

#include "helpers.h"

namespace SemiHoRGod {

ImprovedJmp::ImprovedJmp(int my_id) : id_(my_id), recv_lengths_{}, send_{} {}

void ImprovedJmp::reset() {
  for (size_t i = 0; i < NUM_PARTIES; ++i) {
    for (size_t j = 0; j < NUM_PARTIES; ++j) {
      for(size_t k = 0; k <NUM_PARTIES; ++k)
      {
        send_[i][j][k] = false;  // 代表进程中通信状态，i是否需要向j通信
        send_hash_[i][j][k].reset(); 
        send_values_[i][j][k].clear(); 
        recv_lengths_[i][j][k] = 0; // size_t 代表发送长度
        recv_values1_[i][j][k].clear(); 
        recv_values2_[i][j][k].clear();
        recv_values3_[i][j][k].clear();
        final_recv_values_[i][j][k].clear();
        is_received1_[i][j][k] = false;
        is_received2_[i][j][k] = false;
        is_received3_[i][j][k] = false;
      }
    }
  }
}

bool ImprovedJmp::isHashSender(int sender, int other_sender1, int other_sender2, int receiver) { 
  // 确定某组三人中，谁负责发送哈希
  return (sender > other_sender1) && (sender > other_sender2); // 规定3个人中，number数大的传数据
}

void ImprovedJmp::jumpUpdate(int sender1, int sender2, int sender3, int receiver,
                              size_t nbytes, const void* data) {
  // 使用类成员互斥锁，比 static mutex 更好，避免不同对象间的干扰
  std::lock_guard<std::mutex> lock(mtx_);

  if (sender1 == sender2 || sender1 == sender3 || sender1 == receiver || 
      sender2 == sender3 || sender2 == receiver|| sender3 == receiver) {
    throw std::invalid_argument(boost::str(
        boost::format(
            "ID, other_sender and receiver must be distinct for Jump3 but "
            "received sender1=%1%, sender2=%2%, sender3=%3% and receiver=%NUM_PARTIES%") %
        sender1 % sender2 % sender3 % receiver));
  }
  
  auto [min, mid, max] = sortThreeNumbers(sender1, sender2, sender3);
  
  if (id_ == receiver) { // 如果是 receiver 执行函数，那么 update 接受消息的长度
    recv_lengths_[min][mid][max] += nbytes;
    is_received1_[min][mid][max] = true;
    is_received2_[min][mid][max] = true;
    is_received3_[min][mid][max] = true;
    return;
  }
  
  if (id_ != sender1 && id_ != sender2 && id_ != sender3) {
    return;
  }

  // 只剩下 id_ 为发送者的情况，更新发送缓冲区
  auto [other_sender1, other_sender2] = findOtherSenders(min, mid, max, id_);
  
  if (isHashSender(id_, other_sender1, other_sender2, receiver)) {
    send_hash_[other_sender1][other_sender2][receiver].put(data, nbytes);
  }
  else {
    const auto* temp = static_cast<const uint8_t*>(data);
    auto& values = send_values_[other_sender1][other_sender2][receiver];
    values.insert(values.end(), temp, temp + nbytes); 
  }
  send_[other_sender1][other_sender2][receiver] = true;
}


// ---------------------------------------------------------
// 新增函数：在发送大数据前，先核对双方计算的大小是否一致
// ---------------------------------------------------------
void ImprovedJmp::checkConsistency(io::NetIOMP<NUM_PARTIES>& network) {
    std::cout << "[Consistency Check] Verifying send/recv lengths..." << std::endl;
    std::vector<std::thread> threads;

    // 1. 发送方逻辑：把自己算出来的 send_values 大小发给对方
    for (int receiver = 0; receiver < NUM_PARTIES; ++receiver) {
        if (receiver == id_) continue;
        threads.emplace_back([&, receiver]() {
            for (int other_sender1 = 0; other_sender1 < NUM_PARTIES; ++other_sender1) {
                for (int other_sender2 = other_sender1 + 1; other_sender2 < NUM_PARTIES; ++other_sender2) {
                    if (other_sender1 == receiver || other_sender1 == id_ ||
                        other_sender2 == receiver || other_sender2 == id_ || other_sender1 == other_sender2) {
                        continue;
                    }

                    int min, max;
                    if (other_sender1 < other_sender2) { min = other_sender1; max = other_sender2; } 
                    else { min = other_sender2; max = other_sender1; }

                    // 获取发送方计算出的实际数据大小
                    uint64_t my_send_size = 0;
                    bool should_send = send_[min][max][receiver];
                    
                    if (should_send) {
                        if (isHashSender(id_, min, max, receiver)) {
                            my_send_size = emp::Hash::DIGEST_SIZE;
                        } else {
                            my_send_size = send_values_[min][max][receiver].size();
                        }
                    }

                    // 发送 8 字节的大小头信息
                    network.send(receiver, &my_send_size, sizeof(uint64_t));
                }
            }
            network.flush(receiver);
        });
    }

    // 2. 接收方逻辑：接收对方发来的大小，并与自己的 recv_lengths_ 对比
    for (int sender = 0; sender < NUM_PARTIES; ++sender) {
        if (sender == id_) continue;
        threads.emplace_back([&, sender]() {
            for (int other_sender1 = 0; other_sender1 < NUM_PARTIES; ++other_sender1) {
                for (int other_sender2 = other_sender1 + 1; other_sender2 < NUM_PARTIES; ++other_sender2) {
                    if (other_sender1 == sender || other_sender1 == id_ ||
                        other_sender2 == sender || other_sender2 == id_ || other_sender1 == other_sender2) {
                        continue;
                    }

                    auto [min, mid, max] = sortThreeNumbers(sender, other_sender1, other_sender2);
                    
                    // 获取该通道的 Payload 总长度
                    uint64_t payload_len = recv_lengths_[min][mid][max];
                    uint64_t my_expected_size = 0;

                    // 【核心修复】只有当 Payload > 0 时，才计算期望值
                    if (payload_len > 0) {
                        if (sender == max) {
                            // 只有在这个上下文中我是 Max，且确实有数据要发时，才收 Hash
                            my_expected_size = emp::Hash::DIGEST_SIZE; 
                        } else {
                            // 否则收 Payload
                            my_expected_size = payload_len;
                        }
                    } else {
                        // 如果 Payload 为 0，说明这次没通信，期望收 0
                        my_expected_size = 0;
                    }
                    
                    // 接收对方声称的大小
                    uint64_t peer_claimed_size = 0;
                    network.recv(sender, &peer_claimed_size, sizeof(uint64_t));

                    // --- 核心校验 ---
                    if (my_expected_size != peer_claimed_size) {
                         std::string error_msg = boost::str(boost::format(
                            "\n[FATAL ERROR] Consistency Mismatch at Party %1%!\n"
                            "  Sender: %2% (Context: %3%-%4%)\n"
                            "  My expected Recv Size: %NUM_PARTIES% bytes\n"
                            "  Peer's actual Send Size: %6% bytes\n"
                            "  Difference: %7% bytes\n"
                            "  Hint: Payload len is %8%\n") 
                            % id_ % sender % other_sender1 % other_sender2 
                            % my_expected_size % peer_claimed_size 
                            % (int64_t(peer_claimed_size) - int64_t(my_expected_size))
                            % payload_len);
                        
                        std::cerr << error_msg << std::flush;
                        throw std::runtime_error(error_msg);
                    }
                }
            }
        });
    }

    for (auto& t : threads) t.join();
    std::cout << "[Consistency Check] Passed. Sizes match." << std::endl;
}


// =========================================================================
// 核心修复函数：完全避免线程池死锁，使用 std::thread 分离发送和接收
// =========================================================================
// ---------------------------------------------------------
// 修复版 communicate：增加分块接收 + 详细死锁定位日志
// ---------------------------------------------------------
// ---------------------------------------------------------
// 最终修复版 communicate：分块接收 + 进度监控
// ---------------------------------------------------------
void ImprovedJmp::communicate(io::NetIOMP<NUM_PARTIES>& network, ThreadPool& tpool) {
  // 1. 先进行自检 (保持你之前修复好的 checkConsistency)
  // checkConsistency(network); 

  std::vector<std::thread> threads; 

  // 预分配 recv_hash
  std::array<std::array<std::array<std::array<char, emp::Hash::DIGEST_SIZE>, NUM_PARTIES>, NUM_PARTIES>, NUM_PARTIES> recv_hash{};

  // ================= 1. 启动接收线程 (Receive Threads) =================
  for (int sender = 0; sender < NUM_PARTIES; ++sender) {
    if (sender == id_) continue;

    threads.emplace_back([&, sender]() {
      for (int other_sender1 = 0; other_sender1 < NUM_PARTIES; ++other_sender1) {
        for (int other_sender2 = other_sender1+1; other_sender2 < NUM_PARTIES; ++other_sender2) {
          // 排除无效组合
          if (other_sender1 == sender || other_sender1 == id_ ||
              other_sender2 == sender || other_sender2 == id_ || other_sender1 == other_sender2) {
            continue;
          }

          auto [min, mid, max] = sortThreeNumbers(sender, other_sender1, other_sender2);
          
          // 获取本次要接收的总长度
          auto nbytes = recv_lengths_[min][mid][max];

          if (nbytes != 0) {
            // 如果数据量较大（超过5MB），打印开始日志，方便观察是否卡住
            if (nbytes > NUM_PARTIES * 1024 * 1024) {
                 // std::cout << "[Comm] P" << id_ << " start receiving " << nbytes / 1024 / 1024 << "MB from P" << sender << std::endl;
            }

            if (sender == min) {
              auto& values = recv_values1_[min][mid][max];
              size_t offset = values.size();
              values.resize(offset + nbytes);
              
              // 【核心修复】分块接收循环 (1MB 一块)
              size_t received = 0;
              size_t chunk_size = 1024 * 1024; 
              while(received < nbytes) {
                  size_t remain = nbytes - received;
                  size_t cur_chunk = (remain < chunk_size) ? remain : chunk_size;
                  
                  // 接收一小块
                  network.recv(sender, values.data() + offset + received, cur_chunk);
                  received += cur_chunk;
              }
            } 
            else if (sender == mid) {
              auto& values = recv_values2_[min][mid][max];
              size_t offset = values.size();
              values.resize(offset + nbytes);
              
              // 【核心修复】分块接收循环
              size_t received = 0;
              size_t chunk_size = 1024 * 1024;
              while(received < nbytes) {
                  size_t remain = nbytes - received;
                  size_t cur_chunk = (remain < chunk_size) ? remain : chunk_size;
                  
                  network.recv(sender, values.data() + offset + received, cur_chunk);
                  received += cur_chunk;
              }
            } 
            else if (sender == max) {
              // 接收哈希 (32字节，非常小，直接收)
              network.recv(sender, recv_hash[min][mid][max].data(), emp::Hash::DIGEST_SIZE);
            }
          }
        }
      }
    });
  }

  // ================= 2. 启动发送线程 (Send Threads) =================
  for (int receiver = 0; receiver < NUM_PARTIES; ++receiver) {
    if (receiver == id_) continue;

    threads.emplace_back([&, receiver]() {
      for (int other_sender1 = 0; other_sender1 < NUM_PARTIES; ++other_sender1) {
        for (int other_sender2 = other_sender1+1; other_sender2 < NUM_PARTIES; ++other_sender2) {
          if (other_sender1 == receiver || other_sender1 == id_ ||
              other_sender2 == receiver || other_sender2 == id_ || other_sender1 == other_sender2) {
            continue;
          }
          
          int min, max;
          if (other_sender1 < other_sender2) {
            min = other_sender1; max = other_sender2;
          } else {
            min = other_sender2; max = other_sender1;
          }
          
          bool should_send = send_[min][max][receiver];
          if (should_send) {
            if(isHashSender(id_, min, max, receiver)) {
              auto& hash = send_hash_[min][max][receiver];
              std::array<char, emp::Hash::DIGEST_SIZE> digest{};
              hash.digest(digest.data());
              network.send(receiver, digest.data(), digest.size());
            } else {
              auto& values = send_values_[min][max][receiver];
              
              // 【发送端优化】如果数据太大，我们也分块发，虽然主要瓶颈在接收端
              // 这里保持直接发送通常没问题，因为 network.send 内部通常不阻塞太久
              // 关键是加上 flush
              network.send(receiver, values.data(), values.size());
            }
          }
        }
      }
      // 必须 Flush！确保数据离开本地缓冲区
      network.flush(receiver);
    });
  }

  // ================= 3. 等待所有线程完成 =================
  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  // ================= 4. 校验与合并结果 (保持不变) =================
  emp::Hash hash;
  std::array<char, emp::Hash::DIGEST_SIZE> digest{};
  for (int sender1 = 0; sender1 < NUM_PARTIES; ++sender1) {
    for (int sender2 = sender1 + 1; sender2 < NUM_PARTIES; ++sender2) {
      for (int sender3 = sender2 + 1; sender3 < NUM_PARTIES; ++sender3) {
        if (sender1 == id_ || sender2 == id_ || sender3 == id_) continue;
        
        auto nbytes = recv_lengths_[sender1][sender2][sender3];
        if (nbytes == 0) continue;

        auto& values1 = recv_values1_[sender1][sender2][sender3];
        auto& values2 = recv_values2_[sender1][sender2][sender3];
        auto& final_values = final_recv_values_[sender1][sender2][sender3];

        hash.put(values1.data(), values1.size());
        hash.digest(digest.data());
        
        bool match = true;
        for(int k=0; k<emp::Hash::DIGEST_SIZE; ++k) {
            if(digest[k] != recv_hash[sender1][sender2][sender3][k]) match = false;
        }

        if (!match) {
          final_values = values2;
        } else {
          final_values = values1;
        }
      }
    }
  }
}

const std::vector<uint8_t>& ImprovedJmp::getValues(int sender1, int sender2, int sender3) {
  auto [min, mid, max] = sortThreeNumbers(sender1, sender2, sender3);
  return final_recv_values_[min][mid][max];
}

};  // namespace SemiHoRGod