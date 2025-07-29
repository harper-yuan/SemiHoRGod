#include "ijmp.h"

#include <algorithm>
#include <boost/format.hpp>
#include <future>
#include <stdexcept>

#include "helpers.h"

namespace SemiHoRGod {
ImprovedJmp::ImprovedJmp(int my_id) : id_(my_id), recv_lengths_{}, send_{} {}

void ImprovedJmp::reset() {
  for (size_t i = 0; i < NP ; ++i) {
    for (size_t j = 0; j < NP ; ++j) {
      for(size_t k = 0; k < NP ; ++k)
      {
        send_[i][j][k] = false;  //代表进程中通信状态，i是否需要向j通信
        send_hash_[i][j][k].reset(); //
        send_values_[i][j][k].clear(); //每一个都是一个哈希函数
        recv_lengths_[i][j][k] = 0; //size_t 代表发送长度
        recv_values1_[i][j][k].clear(); //std::vector<uint8_t>代表发送的数据
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

bool ImprovedJmp::isHashSender(int sender, int other_sender, int receiver) { //确定某组三人中，谁负责发送哈希
  if (other_sender == pidFromOffset(sender, 1)) {
    return receiver == pidFromOffset(sender, 3);
  }
  return receiver == pidFromOffset(sender, 1);
}

void ImprovedJmp::jumpUpdate(int sender1, int sender2, int sender3, int receiver,
                              size_t nbytes, const void* data) {
  if (sender1 == sender2 || sender1 == sender3 || sender1 == receiver || 
      sender2 == sender3 || sender2 == receiver|| sender3 == receiver) {
    throw std::invalid_argument(boost::str(
        boost::format(
            "ID, other_sender and receiver must be distinct for Jump3 but "
            "received sender1=%1%, sender2=%2%, sender3=%3% and receiver=%5%") %
        sender1 % sender2 % sender3 % receiver));
  }
  auto [min, mid, max] = sortThreeNumbers(sender1, sender2, sender3);
  if (id_ == receiver) { //如果是receiver执行函数，那么update接受消息的长度
    recv_lengths_[min][mid][max] += nbytes;
    is_received1_[min][mid][max] = true;
    is_received2_[min][mid][max] = true;
    is_received3_[min][mid][max] = true;
    return;
  }
  if (id_ != sender1 && id_ != sender2 && id_ != sender3) {
    return;
  }

  //只剩下，id_为发送者的情况，直接发送数据即可
  auto [other_sender1, other_sender2] = findOtherSenders(min, mid, max, id_);
  const auto* temp = static_cast<const uint8_t*>(data);
  auto& values = send_values_[other_sender1][other_sender2][receiver];
  values.insert(values.end(), temp, temp + nbytes); //在指定位置 pos 之前插入 [first, last) 区间的数据。
  send_[other_sender1][other_sender2][receiver] = true;
}

void ImprovedJmp::communicate(io::NetIOMP<NP>& network, ThreadPool& tpool) {
  std::vector<std::future<void>> res; // std::future<void>作用：表示一个异步操作的结果（来自 std::async、std::promise 或线程池任务）。

  // Send data.
  for (int receiver = 0; receiver < NP ; ++receiver) {
    if (receiver == id_) { //如果id_是接收者，不用发送数据，于是跳过
      continue;
    }
    //下面的情况，id_一定是发送者，所以遍历所有可能的发送情况，是否需要发送查询send_即可
    res.push_back(tpool.enqueue([&, receiver]() {
      for (int other_sender1 = 0; other_sender1 < NP ; ++other_sender1) {
        for (int other_sender2 = 0; other_sender2 < NP ; ++other_sender2) {//已经确定了
          if (other_sender1 == receiver || other_sender1 == id_ ||
              other_sender2 == receiver || other_sender2 == id_ || other_sender1 == other_sender2) { //确保id_一定是发送者
            continue;
          }
          // auto& hash = send_hash_[other_sender][receiver];
          int min, max;
          if (other_sender1 < other_sender2) {
            min = other_sender1;
            max = other_sender2;
          }
          else {
            min = other_sender2;
            max = other_sender1;
          }
          
          auto& values = send_values_[min][max][receiver];
          bool should_send = send_[min][max][receiver];
          if (should_send) {
            // std::array<char, emp::Hash::DIGEST_SIZE> digest{};
            network.send(receiver, values.data(), values.size());
          }
        }
      }

      network.flush(receiver);
    }));
  }

  // Receive data.
  std::array<std::array<std::array<char, emp::Hash::DIGEST_SIZE>, NP>, NP> recv_hash{};
  for (int sender = 0; sender < NP ; ++sender) {
    if (sender == id_) { //如果id_是发送者，则不需要接收数据
      continue;
    }

    res.push_back(tpool.enqueue([&, sender]() {
      for (int other_sender1 = 0; other_sender1 < NP ; ++other_sender1) {
        for (int other_sender2 = 0; other_sender2 < NP ; ++other_sender2) {
          if (other_sender1 == sender || other_sender1 == id_ ||
              other_sender2 == sender || other_sender2 == id_ || other_sender1 == other_sender2) { //彻底排除id_是发送者的可能，下面的代码中id_一定是接受者，是否接收查询recv_lengths_是否大于0
            continue;
          }
          auto [min, mid, max] = sortThreeNumbers(sender, other_sender1, other_sender2);
          auto nbytes = recv_lengths_[min][mid][max];

          if (nbytes != 0) {
            if (is_received1_[min][mid][max]) {
              auto& values = recv_values1_[min][mid][max];
              values.resize(values.size() + nbytes);
              network.recv(sender, values.data() + values.size() - nbytes, nbytes);//收到的值会被放在数组末尾
              is_received1_[min][mid][max] = false;
            }
            else if (is_received2_[min][mid][max]) {
              auto& values = recv_values2_[min][mid][max];
              values.resize(values.size() + nbytes);
              network.recv(sender, values.data() + values.size() - nbytes, nbytes);//收到的值会被放在数组末尾
              is_received2_[min][mid][max] = false;
            }
            else {
              auto& values = recv_values3_[min][mid][max];
              values.resize(values.size() + nbytes);
              network.recv(sender, values.data() + values.size() - nbytes, nbytes);//收到的值会被放在数组末尾
              is_received3_[min][mid][max] = false;
            }
          }
        }
      }
    }));
  }

  for (auto& f : res) {
    f.get();
  }

  // Verify.
  emp::Hash hash;
  // std::array<char, emp::Hash::DIGEST_SIZE> digest{};
  for (int sender1 = 0; sender1 < NP ; ++sender1) {
    for (int sender2 = sender1 + 1; sender2 < NP ; ++sender2) {
      for (int sender3 = sender2 + 1; sender3 < NP ; ++sender3) {
        if (sender1 == id_ || sender2 == id_ || sender3 == id_) {
          continue;
        }

        auto nbytes = recv_lengths_[sender1][sender2][sender3];
        auto& values1 = recv_values1_[sender1][sender2][sender3];
        auto& values2 = recv_values2_[sender1][sender2][sender3];
        auto& values3 = recv_values3_[sender1][sender2][sender3];
        auto& final_values = final_recv_values_[sender1][sender2][sender3];

        
        if (!values1.empty() && !values2.empty() && !values3.empty()) {
          if (isEqual(values1, values2)) {
            final_values = values1;
          }
          else{
            final_values = values3;
          }
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