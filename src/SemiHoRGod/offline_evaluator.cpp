#include "offline_evaluator.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>

#include "helpers.h"
#include "ijmp.h"
#include "online_evaluator.h"

namespace SemiHoRGod{
OfflineEvaluator::OfflineEvaluator(int my_id,
                                   std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network1,
                                   std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network2,
                                   utils::LevelOrderedCircuit circ,
                                   int security_param, int threads, int seed)
    : id_(my_id),
      security_param_(security_param),
      rgen_(my_id, seed),
      network_(std::move(network1)),
      network_ot_(std::move(network2)),
      circ_(std::move(circ)),
      preproc_(circ.num_gates, circ.outputs.size()),
      jump_(my_id) {
  tpool_ = std::make_shared<ThreadPool>(threads);
}

std::vector<Ring> OfflineEvaluator::elementwise_sum(const std::array<std::vector<Ring>, NUM_RSS>& recon_shares, int i, int j, int k) {
    // 检查向量是否非空且大小一致
    if (recon_shares[i].empty()) return {};
    size_t size = recon_shares[i].size();
    assert(recon_shares[j].size() == size && recon_shares[k].size() == size);
    
    std::vector<Ring> result(size);
    for (size_t index = 0; index < size; ++index) {
        result[index] = recon_shares[i][index] + recon_shares[j][index] + recon_shares[k][index];
    }
    return result;
}

std::vector<Ring> OfflineEvaluator::reconstruct(
    const std::array<std::vector<Ring>, NUM_RSS>& recon_shares) {
  // All vectors in recon_shares should have same size.
  size_t num = recon_shares[0].size();
  size_t nbytes = sizeof(Ring) * num;

  if (nbytes == 0) {
    return {};
  }

  std::vector<Ring> vres1(num);
  std::vector<Ring> vres2(num);
  std::vector<Ring> result(num);

  for(int i = 0; i<NUM_PARTIES; i++) {
    int sender1 = pidFromOffset(i, 1);
    int sender2 = pidFromOffset(i, 2);
    int sender3 = pidFromOffset(i, 3);
    int other1 = pidFromOffset(i, 4);
    int other2 = pidFromOffset(i, 5);
    int other3 = pidFromOffset(i, 6);
    int receiver = i;
    if(id_ == sender1 | id_ == sender2 | id_ == sender3 | receiver == id_) {
      if(receiver == id_) {
        jump_.jumpUpdate(sender1, sender2, sender3, receiver, nbytes, nullptr);
      }
      else {
        auto z_1 = elementwise_sum(recon_shares, upperTriangularToArray(receiver, other1), 
                                                 upperTriangularToArray(receiver, other2),
                                                 upperTriangularToArray(receiver, other3));
        jump_.jumpUpdate(sender1, sender2, sender3, receiver, nbytes, z_1.data());
      }
    }

    sender1 = pidFromOffset(i, 4);
    sender2 = pidFromOffset(i, 5);
    sender3 = pidFromOffset(i, 6);
    other1 = pidFromOffset(i, 1);
    other2 = pidFromOffset(i, 2);
    other3 = pidFromOffset(i, 3);
    if(id_ == sender1 | id_ == sender2 | id_ == sender3 | receiver == id_) {
      if(receiver == id_) {
        jump_.jumpUpdate(sender1, sender2, sender3, receiver, nbytes, nullptr);
      }
      else {
        auto z_2 = elementwise_sum(recon_shares, upperTriangularToArray(receiver, other1), 
                                                 upperTriangularToArray(receiver, other2),
                                                 upperTriangularToArray(receiver, other3));
        jump_.jumpUpdate(sender1, sender2, sender3, receiver, nbytes, z_2.data());
      }
    }
  }
  jump_.communicate(*network_, *tpool_);

  //reinterpret_cast 的作用是 对指针类型进行低级别的重新解释，即将原始指针类型强制转换为另一种不相关的指针类型（这里是 const Ring*），而无需修改底层数据。
  const auto* miss_values1 = reinterpret_cast<const Ring*>(jump_.getValues(pidFromOffset(id_, 1), pidFromOffset(id_, 2), pidFromOffset(id_, 3)).data());
  const auto* miss_values2 = reinterpret_cast<const Ring*>(jump_.getValues(pidFromOffset(id_, 4), pidFromOffset(id_, 5), pidFromOffset(id_, 6)).data());     
  std::copy(miss_values1, miss_values1 + num, vres1.begin());
  std::copy(miss_values2, miss_values2 + num, vres2.begin());
  for (size_t i = 0; i<num; i++) {
    Ring temp = 0;
    for(size_t j = 0; j<NUM_RSS; j++) {
      temp += recon_shares[j][i];
    }
    result[i] = vres1[i] + vres2[i] + temp;
  }
  jump_.reset();
  return result;
}

void OfflineEvaluator::randomShare(RandGenPool& rgen,
                                   ReplicatedShare<Ring>& share) {
  rgen.getRelative(1).random_data(&share[0], sizeof(Ring));
  rgen.getRelative(2).random_data(&share[1], sizeof(Ring));
  rgen.getRelative(3).random_data(&share[2], sizeof(Ring));
}

ReplicatedShare<Ring> OfflineEvaluator::jshShare(int id, RandGenPool& rgen, int i, int j, int k) {
  ReplicatedShare<Ring> result;
  result.init_zero();
  // id是当前执行这个函数的参与方i，他需要获取{0,1,2,3,4}\{i}
  int idx = 0;
  int temp;
  for (size_t pid1 = 0; pid1 < NUM_PARTIES; ++pid1) {
    for (size_t pid2 = pid1+1; pid2 < NUM_PARTIES; ++pid2) {
      if (pid1 == id || pid2 == id) { //他无法获取共享x_{id}这个数据
        rgen.self().random_data(&temp, sizeof(Ring));
      }
      else {
        //直接使用公共的随机数种子生成数据，可能不安全，但效率是一样的
        rgen.self().random_data(&temp, sizeof(Ring));
      }
    }
  }
  return result;
}

std::vector<Ring> OfflineEvaluator::reconstruct(
    const std::vector<ReplicatedShare<Ring>>& shares) {
  std::array<std::vector<Ring>, NUM_RSS> recon_shares;
  for (const auto& s : shares) {
    for (size_t i = 0; i < NUM_RSS; ++i) {
      recon_shares[i].push_back(s[i]);
    }
  }
  return reconstruct(recon_shares);
}


ReplicatedShare<Ring> OfflineEvaluator::randomShareWithParty(int id, RandGenPool& rgen) {
  ReplicatedShare<Ring> result;
  int idx = 0;
  int temp;
  for (size_t pid1 = 0; pid1 < NUM_PARTIES; ++pid1) {
    for (size_t pid2 = pid1+1; pid2 < NUM_PARTIES; ++pid2) {
      if (pid1 == id || pid2 == id) { //他无法获取共享x_{id}这个数据
        rgen.all().random_data(&temp, sizeof(Ring));
        result[upperTriangularToArray(pid1, pid2)] = 0;
      }
      else {
        //直接使用公共的随机数种子生成数据，可能不安全，但效率是一样的
        rgen.all().random_data(&result[upperTriangularToArray(pid1, pid2)], sizeof(Ring));
      }
    }
  }
  return result;
}

void OfflineEvaluator::randomShareWithParty(int id, int dealer,
                                            RandGenPool& rgen,
                                            ReplicatedShare<Ring>& share) {
  Ring temp;
  for (int pid1 = 0; pid1 < NUM_PARTIES; ++pid1) {
    for (int pid2 = pid1+1; pid2 < NUM_PARTIES; ++pid2) {
      if (pid1 == id || pid2 == id) { //他无法获取共享x_{id}这个数据
        rgen.all().random_data(&temp, sizeof(Ring));
        share[upperTriangularToArray(pid1, pid2)] = 0;
      }
      else {
        //如果碰到数据x_{dealer}，dealer是需要知道x_{dealer}的，所以用公共的随机数种子生成数据
        if (pid1 == dealer || pid2 == dealer) {
          rgen.all().random_data(&share[upperTriangularToArray(pid1, pid2)], sizeof(Ring));
        }
        else {
          rgen.all().random_data(&share[upperTriangularToArray(pid1, pid2)], sizeof(Ring));
        }
      }
    }
  }
}

//如果是秘密的持有者，那么执行共享，除了得到秘密的共享，还会得到真实的秘密
void OfflineEvaluator::randomShareWithParty(int id, RandGenPool& rgen,
                                            ReplicatedShare<Ring>& share,
                                            Ring& secret) {
  Ring temp = 0;
  secret = 0;
  for (int pid1 = 0; pid1 < NUM_PARTIES; ++pid1) {
    for (int pid2 = pid1+1; pid2 < NUM_PARTIES; ++pid2) {
      if (pid1 == id || pid2 == id) { //他无法获取共享x_{id}这个数据
        rgen.all().random_data(&temp, sizeof(Ring));
        secret += temp;
      }
      else {
        rgen.all().random_data(&share[upperTriangularToArray(pid1, pid2)], sizeof(Ring));
        secret += share[upperTriangularToArray(pid1, pid2)];
      }
    }
  }
}

std::vector<ReplicatedShare<Ring>> OfflineEvaluator::randomShareWithParty_for_trun(int id, RandGenPool& rgen, std::vector<std::pair<int, int>> indices) {
  std::vector<ReplicatedShare<Ring>> result;
  for(auto pair : indices) {
    ReplicatedShare<Ring> result_temp;
    Ring temp;
    result_temp.init_zero();
    auto index1 = std::get<0>(pair);
    auto index2 = std::get<1>(pair);

    if(index1 == id || index2 == id) { //如果是id是{u,v}，那么共享被设置为0
      rgen.all().random_data(&temp, sizeof(Ring));
    }
    else {
      rgen.all().random_data(&result_temp[upperTriangularToArray(index1, index2)], sizeof(Ring));
    }
    result.push_back(result_temp);
  }
  return result;
}

// 自定义哈希函数 for std::tuple<int, int, int>
struct TupleHash {
    size_t operator()(const std::tuple<int, int, int>& key) const {
        auto [i, j, k] = key;
        // 简单的哈希组合（可以使用更复杂的哈希算法如 boost::hash_combine）
        return std::hash<int>{}(i) ^ std::hash<int>{}(j) ^ std::hash<int>{}(k);
    }
};

ReplicatedShare<Ring> OfflineEvaluator::compute_prod_mask(ReplicatedShare<Ring> mask_in1, ReplicatedShare<Ring> mask_in2) {
  std::unordered_map<std::tuple<Ring, Ring, Ring>, Ring, TupleHash> Gamma_i_j_k_2_mapping;
  ReplicatedShare<Ring> mask_prod;
  mask_prod.init_zero();
  for(int i = 0; i < NUM_PARTIES; i++) {
    for (int j = i+1; j < NUM_PARTIES; j++) {
      for (int k = j+1; k < NUM_PARTIES; k++) {
        if(i != id_ && j != id_ && k != id_) {
          auto [l, m, n, o] = findRemainingNumbers_7PC(i, j, k);
          auto key = std::make_tuple(l,m,n);
          if(id_ != o) {
            if (Gamma_i_j_k_2_mapping.find(key) != Gamma_i_j_k_2_mapping.end()) {//如果这个key存在
              Gamma_i_j_k_2_mapping[{l,m,n}] += mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(i, k)] + 
                                                mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(j, k)] + 
                                                mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                                mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(j, k)] + 
                                                mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                                mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, k)];
            }
            else {
              Gamma_i_j_k_2_mapping[{l,m,n}] = mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(i, k)] + 
                                              mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(j, k)] + 
                                              mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                              mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(j, k)] + 
                                              mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                              mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, k)];
            }
          }
        }
      }
    }
  }
  //①计算α_xy = α_x * α_y的共享，除了要接受消息，还要发送消息
  for(int i = 0; i < NUM_PARTIES; i++) {
    for (int j = i+1; j < NUM_PARTIES; j++) {
      for (int k = j+1; k < NUM_PARTIES; k++) {
        auto [l, m, n, o] = findRemainingNumbers_7PC(i, j, k);
        if(i == id_ || j == id_ || k == id_) {
          auto Gamma_i_j_k_1 = mask_in1[upperTriangularToArray(l, m)] * mask_in2[upperTriangularToArray(n, o)] + 
                               mask_in1[upperTriangularToArray(l, n)] * mask_in2[upperTriangularToArray(m, o)] + 
                               mask_in1[upperTriangularToArray(l, o)] * mask_in2[upperTriangularToArray(m, n)] + 
                               mask_in1[upperTriangularToArray(m, n)] * mask_in2[upperTriangularToArray(l, o)] + 
                               mask_in1[upperTriangularToArray(m, o)] * mask_in2[upperTriangularToArray(l, n)] + 
                               mask_in1[upperTriangularToArray(n, o)] * mask_in2[upperTriangularToArray(l, m)];

          auto Gamma_i_j_k = Gamma_i_j_k_1;
          if (Gamma_i_j_k_2_mapping.find({i,j,k}) != Gamma_i_j_k_2_mapping.end()) {
            Gamma_i_j_k += Gamma_i_j_k_2_mapping[{i,j,k}];
          }

          if (i == 0 && j == 1 && k == 2) {
            Gamma_i_j_k += mask_in1[upperTriangularToArray(3, 4)] * mask_in2[upperTriangularToArray(3, 4)] + 
                          mask_in1[upperTriangularToArray(3, 5)] * mask_in2[upperTriangularToArray(3, 5)] +
                          mask_in1[upperTriangularToArray(3, 6)] * mask_in2[upperTriangularToArray(3, 6)] +
                          mask_in1[upperTriangularToArray(4, 5)] * mask_in2[upperTriangularToArray(4, 5)] +
                          mask_in1[upperTriangularToArray(4, 6)] * mask_in2[upperTriangularToArray(4, 6)] +
                          mask_in1[upperTriangularToArray(5, 6)] * mask_in2[upperTriangularToArray(5, 6)];
          }
          else if (i == 4 && j == 5 && k == 6) {
            Gamma_i_j_k += mask_in1[upperTriangularToArray(0, 1)] * mask_in2[upperTriangularToArray(0, 1)] + 
                          mask_in1[upperTriangularToArray(0, 2)] * mask_in2[upperTriangularToArray(0, 2)] +
                          mask_in1[upperTriangularToArray(0, 3)] * mask_in2[upperTriangularToArray(0, 3)] +
                          mask_in1[upperTriangularToArray(1, 2)] * mask_in2[upperTriangularToArray(1, 2)] +
                          mask_in1[upperTriangularToArray(1, 3)] * mask_in2[upperTriangularToArray(1, 3)] +
                          mask_in1[upperTriangularToArray(2, 3)] * mask_in2[upperTriangularToArray(2, 3)];
          }
          else if (i == 0 && j == 1 && k == 3) {
            Gamma_i_j_k += mask_in1[upperTriangularToArray(2, 4)] * mask_in2[upperTriangularToArray(2, 4)] + 
                          mask_in1[upperTriangularToArray(2, 5)] * mask_in2[upperTriangularToArray(2, 5)] +
                          mask_in1[upperTriangularToArray(2, 6)] * mask_in2[upperTriangularToArray(2, 6)];
          }
          else if (i == 0 && j == 2 && k == 3) {
            Gamma_i_j_k += mask_in1[upperTriangularToArray(1, 4)] * mask_in2[upperTriangularToArray(1, 4)] + 
                          mask_in1[upperTriangularToArray(1, 5)] * mask_in2[upperTriangularToArray(1, 5)] +
                          mask_in1[upperTriangularToArray(1, 6)] * mask_in2[upperTriangularToArray(1, 6)];
          }
          else if (i == 1 && j == 2 && k == 3) {
            Gamma_i_j_k += mask_in1[upperTriangularToArray(0, 4)] * mask_in2[upperTriangularToArray(0, 4)] + 
                          mask_in1[upperTriangularToArray(0, 5)] * mask_in2[upperTriangularToArray(0, 5)] +
                          mask_in1[upperTriangularToArray(0, 6)] * mask_in2[upperTriangularToArray(0, 6)];
          }

          //然后把数据share出去
          auto Gamma_i_j_k_mask = jshShare(id_, rgen_, i, j, k);
          auto x_l_m = Gamma_i_j_k - Gamma_i_j_k_mask.sum();
          Gamma_i_j_k_mask[upperTriangularToArray(l, m)] = x_l_m; //i,j,k本地设置，而n,o需要接收消息设置，l,m不用设置

          //自己把最终结果加上
          mask_prod += Gamma_i_j_k_mask;

          //按顺序排序，这样其他发送者的发送参数是一样的，接收者也用一样的接受参数接受数据
          jump_.jumpUpdate(i, j, k, n, (size_t) sizeof(Ring), &x_l_m);
          jump_.jumpUpdate(i, j, k, o, (size_t) sizeof(Ring), &x_l_m);
          if(id_ == 0) {
          }
        }
        else {
          //接收消息, id_不属于i，j，k中的一个
          if(n == id_ || o == id_) { //如果是参与方n, o，那么需要用通信协议来更新x_l_m
            jump_.jumpUpdate(i, j, k, id_, (size_t) sizeof(Ring), nullptr);
          }
        }
      }
    }
  }
  
  //通信得到数据
  jump_.communicate(*network_, *tpool_);

  for(int i = 0; i < NUM_PARTIES; i++) {
    for (int j = i+1; j < NUM_PARTIES; j++) {
      for (int k = j+1; k < NUM_PARTIES; k++) {
        if(i != id_ && j != id_ && k != id_) { //确保是接收方
          auto [l, m, n, o] = findRemainingNumbers_7PC(i, j, k);
          if(n == id_ || o == id_) { //如果是参与方n, o，那么需要用通信协议来更新x_l_m
            // Ring x_m;
            auto Gamma_i_j_k_mask = jshShare(id_, rgen_, i, j, k);
            const Ring *x_l_m = reinterpret_cast<const Ring*>(jump_.getValues(i, j, k).data());
            Gamma_i_j_k_mask[upperTriangularToArray(l, m)] = *x_l_m; //j就对应x_m中的m
            //自己吧最终结果加上
            mask_prod += Gamma_i_j_k_mask;
          }
          else if(l == id_ || m == id_) { //如果是参与方l，m，那么直接把x_l_m设置为0即可
            auto Gamma_i_j_k_mask = jshShare(id_, rgen_, i, j, k);
            Gamma_i_j_k_mask[upperTriangularToArray(l, m)] = 0;
            //自己吧最终结果加上
            mask_prod += Gamma_i_j_k_mask;
          }
        }
      }
    }
  }
  jump_.reset();
  return mask_prod;
}

ReplicatedShare<Ring> OfflineEvaluator::compute_prod_mask_dot(vector<ReplicatedShare<Ring>> mask_in1_vec, vector<ReplicatedShare<Ring>> mask_in2_vec) {
  std::unordered_map<std::tuple<Ring, Ring, Ring>, Ring, TupleHash> Gamma_i_j_k_2_mapping;
  ReplicatedShare<Ring> mask_prod;
  mask_prod.init_zero();
  for(int i = 0; i < NUM_PARTIES; i++) {
    for (int j = i+1; j < NUM_PARTIES; j++) {
      for (int k = j+1; k < NUM_PARTIES; k++) {
        if(i != id_ && j != id_ && k != id_) {
          auto [l, m, n, o] = findRemainingNumbers_7PC(i, j, k);
          auto key = std::make_tuple(l,m,n);
          if(id_ != o) {
            for(int t = 0; t<mask_in1_vec.size(); t++) {
            auto &mask_in1 = mask_in1_vec[t];
            auto &mask_in2 = mask_in2_vec[t];
              if (Gamma_i_j_k_2_mapping.find(key) != Gamma_i_j_k_2_mapping.end()) {//如果这个key存在
                
                Gamma_i_j_k_2_mapping[{l,m,n}] += mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(i, k)] + 
                                                  mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(j, k)] + 
                                                  mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                                  mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(j, k)] + 
                                                  mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                                  mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, k)];
              }
              else {
                Gamma_i_j_k_2_mapping[{l,m,n}] = mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(i, k)] + 
                                                mask_in1[upperTriangularToArray(i, j)] * mask_in2[upperTriangularToArray(j, k)] + 
                                                mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                                mask_in1[upperTriangularToArray(i, k)] * mask_in2[upperTriangularToArray(j, k)] + 
                                                mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, j)] + 
                                                mask_in1[upperTriangularToArray(j, k)] * mask_in2[upperTriangularToArray(i, k)];
              }
            }
          }
        }
      }
    }
  }
  // sleep(id_);
  // std::cout<<"##############################id="<<id_<<"##############################"<<endl;
  //①计算α_xy = α_x * α_y的共享，除了要接受消息，还要发送消息
  for(int i = 0; i < NUM_PARTIES; i++) {
    for (int j = i+1; j < NUM_PARTIES; j++) {
      for (int k = j+1; k < NUM_PARTIES; k++) {
        auto [l, m, n, o] = findRemainingNumbers_7PC(i, j, k);
        if(i == id_ || j == id_ || k == id_) {
          Ring Gamma_i_j_k = 0;
          for(int t = 0; t<mask_in1_vec.size(); t++) {
            auto &mask_in1 = mask_in1_vec[t];
            auto &mask_in2 = mask_in2_vec[t];
            auto Gamma_i_j_k_1 = mask_in1[upperTriangularToArray(l, m)] * mask_in2[upperTriangularToArray(n, o)] + 
                                mask_in1[upperTriangularToArray(l, n)] * mask_in2[upperTriangularToArray(m, o)] + 
                                mask_in1[upperTriangularToArray(l, o)] * mask_in2[upperTriangularToArray(m, n)] + 
                                mask_in1[upperTriangularToArray(m, n)] * mask_in2[upperTriangularToArray(l, o)] + 
                                mask_in1[upperTriangularToArray(m, o)] * mask_in2[upperTriangularToArray(l, n)] + 
                                mask_in1[upperTriangularToArray(n, o)] * mask_in2[upperTriangularToArray(l, m)];

            Gamma_i_j_k += Gamma_i_j_k_1;

            if (i == 0 && j == 1 && k == 2) {
              Gamma_i_j_k += mask_in1[upperTriangularToArray(3, 4)] * mask_in2[upperTriangularToArray(3, 4)] + 
                            mask_in1[upperTriangularToArray(3, 5)] * mask_in2[upperTriangularToArray(3, 5)] +
                            mask_in1[upperTriangularToArray(3, 6)] * mask_in2[upperTriangularToArray(3, 6)] +
                            mask_in1[upperTriangularToArray(4, 5)] * mask_in2[upperTriangularToArray(4, 5)] +
                            mask_in1[upperTriangularToArray(4, 6)] * mask_in2[upperTriangularToArray(4, 6)] +
                            mask_in1[upperTriangularToArray(5, 6)] * mask_in2[upperTriangularToArray(5, 6)];
            }
            else if (i == 4 && j == 5 && k == 6) {
              Gamma_i_j_k += mask_in1[upperTriangularToArray(0, 1)] * mask_in2[upperTriangularToArray(0, 1)] + 
                            mask_in1[upperTriangularToArray(0, 2)] * mask_in2[upperTriangularToArray(0, 2)] +
                            mask_in1[upperTriangularToArray(0, 3)] * mask_in2[upperTriangularToArray(0, 3)] +
                            mask_in1[upperTriangularToArray(1, 2)] * mask_in2[upperTriangularToArray(1, 2)] +
                            mask_in1[upperTriangularToArray(1, 3)] * mask_in2[upperTriangularToArray(1, 3)] +
                            mask_in1[upperTriangularToArray(2, 3)] * mask_in2[upperTriangularToArray(2, 3)];
            }
            else if (i == 0 && j == 1 && k == 3) {
              Gamma_i_j_k += mask_in1[upperTriangularToArray(2, 4)] * mask_in2[upperTriangularToArray(2, 4)] + 
                            mask_in1[upperTriangularToArray(2, 5)] * mask_in2[upperTriangularToArray(2, 5)] +
                            mask_in1[upperTriangularToArray(2, 6)] * mask_in2[upperTriangularToArray(2, 6)];
            }
            else if (i == 0 && j == 2 && k == 3) {
              Gamma_i_j_k += mask_in1[upperTriangularToArray(1, 4)] * mask_in2[upperTriangularToArray(1, 4)] + 
                            mask_in1[upperTriangularToArray(1, 5)] * mask_in2[upperTriangularToArray(1, 5)] +
                            mask_in1[upperTriangularToArray(1, 6)] * mask_in2[upperTriangularToArray(1, 6)];
            }
            else if (i == 1 && j == 2 && k == 3) {
              Gamma_i_j_k += mask_in1[upperTriangularToArray(0, 4)] * mask_in2[upperTriangularToArray(0, 4)] + 
                            mask_in1[upperTriangularToArray(0, 5)] * mask_in2[upperTriangularToArray(0, 5)] +
                            mask_in1[upperTriangularToArray(0, 6)] * mask_in2[upperTriangularToArray(0, 6)];
            }
          }
          if (Gamma_i_j_k_2_mapping.find({i,j,k}) != Gamma_i_j_k_2_mapping.end()) {
            Gamma_i_j_k += Gamma_i_j_k_2_mapping[{i,j,k}];
          }
          //然后把数据share出去
          auto Gamma_i_j_k_mask = jshShare(id_, rgen_, i, j, k);
          auto x_l_m = Gamma_i_j_k - Gamma_i_j_k_mask.sum();
          Gamma_i_j_k_mask[upperTriangularToArray(l, m)] = x_l_m; //i,j,k本地设置，而n,o需要接收消息设置，l,m不用设置

          //自己把最终结果加上
          mask_prod += Gamma_i_j_k_mask;

          //按顺序排序，这样其他发送者的发送参数是一样的，接收者也用一样的接受参数接受数据
          jump_.jumpUpdate(i, j, k, n, (size_t) sizeof(Ring), &x_l_m);
          jump_.jumpUpdate(i, j, k, o, (size_t) sizeof(Ring), &x_l_m);
          if(id_ == 0) {
          }
        }
        else {
          //接收消息, id_不属于i，j，k中的一个
          if(n == id_ || o == id_) { //如果是参与方n, o，那么需要用通信协议来更新x_l_m
            jump_.jumpUpdate(i, j, k, id_, (size_t) sizeof(Ring), nullptr);
          }
        }
      }
    }
  }
  
  //通信得到数据
  jump_.communicate(*network_, *tpool_);

  for(int i = 0; i < NUM_PARTIES; i++) {
    for (int j = i+1; j < NUM_PARTIES; j++) {
      for (int k = j+1; k < NUM_PARTIES; k++) {
        if(i != id_ && j != id_ && k != id_) { //确保是接收方
          auto [l, m, n, o] = findRemainingNumbers_7PC(i, j, k);
          if(n == id_ || o == id_) { //如果是参与方n, o，那么需要用通信协议来更新x_l_m
            // Ring x_m;
            auto Gamma_i_j_k_mask = jshShare(id_, rgen_, i, j, k);
            const Ring *x_l_m = reinterpret_cast<const Ring*>(jump_.getValues(i, j, k).data());
            Gamma_i_j_k_mask[upperTriangularToArray(l, m)] = *x_l_m; //j就对应x_m中的m
            //自己吧最终结果加上
            mask_prod += Gamma_i_j_k_mask;
          }
          else if(l == id_ || m == id_) { //如果是参与方l，m，那么直接把x_l_m设置为0即可
            auto Gamma_i_j_k_mask = jshShare(id_, rgen_, i, j, k);
            Gamma_i_j_k_mask[upperTriangularToArray(l, m)] = 0;
            //自己吧最终结果加上
            mask_prod += Gamma_i_j_k_mask;
          }
        }
      }
    }
  }
  jump_.reset();
  return mask_prod;
}

ReplicatedShare<Ring> OfflineEvaluator::bool_mul(ReplicatedShare<Ring> a, ReplicatedShare<Ring> b){
  ReplicatedShare<Ring> temp = a + b;
  auto temp2 = compute_prod_mask(a, b);
  temp -= temp2.cosnt_mul(2);
  return temp;
}

ReplicatedShare<Ring> OfflineEvaluator::bool_mul_by_indices(vector<ReplicatedShare<Ring>> r_mask_vec, vector<int> indices) {
  ReplicatedShare<Ring> result = r_mask_vec[indices[0]];
  for(int i = 1; i < indices.size(); i++) {
    result = bool_mul(result, r_mask_vec[indices[i]]);
  }
  return result;
}

std::tuple<vector<ReplicatedShare<Ring>>, vector<ReplicatedShare<Ring>>> OfflineEvaluator::comute_random_r_every_bit_sharing(int id, vector<ReplicatedShare<Ring>> r_mask_vec,
                                                                                          std::vector<std::pair<int, int>> indices) {
  //首先计算17个随机数的每一比特的共享
  std::array<std::array<ReplicatedShare<Ring>, 17>, N> r_mask_vec_every_bit;
  vector<ReplicatedShare<Ring>> r_1_mask_vec_every_bit;
  vector<ReplicatedShare<Ring>> r_2_mask_vec_every_bit;
  for(int i = 0; i<N; i++) {
    for(int j = 0; j<indices.size(); j++) {
      //计算第i个随机数r的第j个比特
      ReplicatedShare<Ring> r_i_for_j_bit_mask;
      r_i_for_j_bit_mask.init_zero();

      Ring r_i;
      auto pair = indices[j];
      auto index1 = std::get<0>(pair);
      auto index2 = std::get<1>(pair);
      
      //找到r_i的值，不同的参与方拥有的值不一样
      if(id == index1 || id == index2) {
        r_i = 0;
      }
      else {
        r_i = r_mask_vec[i][upperTriangularToArray(index1, index2)];
      }
      if(((r_i >> i) & 1ULL) == 1 && id != index1 && id != index2) { //计算r1的第i比特的共享，要确保五个人的共享值复原后等于r的第i比特
        r_i_for_j_bit_mask[upperTriangularToArray(index1, index2)] = 1;
      }
      r_mask_vec_every_bit[i][j] = r_i_for_j_bit_mask;
    }
  }

  for(int i = 0; i<N; i++) {
    //首先把17个随机数的第j比特存在一个vector中
    std::vector<ReplicatedShare<Ring>> r_j_bit_mask_vec;
    r_j_bit_mask_vec.reserve(r_mask_vec_every_bit[i].size());
    std::copy(r_mask_vec_every_bit[i].begin(), r_mask_vec_every_bit[i].end(), std::back_inserter(r_j_bit_mask_vec));

    std::vector<int>  indices1 = {0, 1, 2};
    auto r = bool_mul_by_indices(r_j_bit_mask_vec, indices1);

    std::vector<int>  indices2 = {5, 8, 6, 9, 7, 10, 3};
    auto r_1 = bool_mul_by_indices(r_j_bit_mask_vec, indices2);
    r_1 = bool_mul(r_1, r);

    std::vector<int>  indices3 = {11, 14, 12, 15, 13, 16, 4};
    auto r_2 = bool_mul_by_indices(r_j_bit_mask_vec, indices3);
    r_2 = bool_mul(r_2, r);

    r_1_mask_vec_every_bit.push_back(r_1);
    r_2_mask_vec_every_bit.push_back(r_2);
  }

  return {r_1_mask_vec_every_bit, r_2_mask_vec_every_bit};
}

PreprocCircuit<Ring> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<Ring> OfflineEvaluator::run(const utils::LevelOrderedCircuit& circ,
    const std::unordered_map<utils::wire_t, int>& input_pid_map,
    size_t security_param, int pid, emp::PRG& prg) {
  preproc_ = offline_setwire(circ, input_pid_map, security_param, id_, prg);
  return std::move(preproc_);
}

PreprocCircuit<Ring> OfflineEvaluator::offline_setwire(
    const utils::LevelOrderedCircuit& circ,
    const std::unordered_map<utils::wire_t, int>& input_pid_map,
    size_t security_param, int pid, emp::PRG& prg) {
  PreprocCircuit<Ring> preproc(circ.num_gates, circ.outputs.size());
  jump_.reset();
  std::vector<DummyShare<Ring>> wires(circ.num_gates);
  for (const auto& level : circ.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case utils::GateType::kInp: {
          auto pregate = std::make_unique<PreprocInput<Ring>>();
          auto pid = input_pid_map.at(gate->out); //input pid
          pregate->pid = pid;
          if (pid == id_) {
            randomShareWithParty(id_, rgen_, pregate->mask,
                                 pregate->mask_value); //如果是数据的拥有者，他是可以获得α的累计值的，以此计算β
          } 
          else {
            randomShareWithParty(id_, pid, rgen_, pregate->mask);
          }
          preproc.gates[gate->out] = std::move(pregate);
          break;
        }

        case utils::GateType::kAdd: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc.gates[g->in1]->mask;
          const auto& mask_in2 = preproc.gates[g->in2]->mask;
          preproc.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(mask_in1 + mask_in2);
          break;
        }

        case utils::GateType::kSub: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc.gates[g->in1]->mask;
          const auto& mask_in2 = preproc.gates[g->in2]->mask;
          preproc.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(mask_in1 - mask_in2);
          break;
        }

        case utils::GateType::kConstAdd: {
          const auto* g = static_cast<utils::ConstOpGate<Ring>*>(gate.get());
          // const auto& mask_in = preproc.gates[g->in]->mask;
          preproc.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(preproc.gates[g->in]->mask);//mask_in的值不会改变
          break;
        }

        case utils::GateType::kConstMul: {
          const auto* g = static_cast<utils::ConstOpGate<Ring>*>(gate.get());
          const auto& mask_in = preproc.gates[g->in]->mask;
          // wires[g->out] = wires[g->in] * g->cval;
          preproc.gates[g->out] =
              std::make_unique<PreprocGate<Ring>>(mask_in*g->cval);
          break;
        }
        
        case utils::GateType::kMul: {
          //目的有2个，得到α_xy = α_x * α_y。另一个就是随机生成α_z作为乘法结果的alpha部分
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc.gates[g->in1]->mask;
          const auto& mask_in2 = preproc.gates[g->in2]->mask;

          ReplicatedShare<Ring> mask_prod = compute_prod_mask(mask_in1, mask_in2);
          preproc.gates[gate->out] = std::make_unique<PreprocMultGate<Ring>>(
              randomShareWithParty(id_, rgen_), mask_prod);
          break;
        }

        case utils::GateType::kDotprod: {
          const auto* g = static_cast<utils::SIMDGate*>(gate.get());

          vector<ReplicatedShare<Ring>> mask_in1_vec;
          vector<ReplicatedShare<Ring>> mask_in2_vec;
          for (size_t i = 0; i < g->in1.size(); i++) {
            mask_in1_vec.push_back(preproc.gates[g->in1[i]]->mask);
            mask_in2_vec.push_back(preproc.gates[g->in2[i]]->mask);
          }
          ReplicatedShare<Ring> mask_prod_dot = compute_prod_mask_dot(mask_in1_vec, mask_in2_vec);

          preproc.gates[g->out] = std::make_unique<PreprocDotpGate<Ring>>(
              randomShareWithParty(id_, rgen_), mask_prod_dot);
          break;
        }

        case utils::GateType::kTrdotp: {
          const auto* g = static_cast<utils::SIMDGate*>(gate.get());

          ReplicatedShare<Ring> r_1, r_2;
          ReplicatedShare<Ring> r_1_trunted_d, r_2_trunted_d;
          r_1.init_zero(); r_2.init_zero(); 
          r_1_trunted_d.init_zero(); r_2_trunted_d.init_zero();
          //首先生成r1,r2,r3的共享，按照表格的内容生成
          std::vector<std::pair<int, int>> indices = { {0,1}, {0,2}, {1,2}, {3,4}, {5,6}, {0,3},
                                                      {1,3}, {2,3}, {0,4}, {1,4}, {2,4}, {0,5},
                                                      {1,5}, {2,5}, {0,6}, {1,6}, {2,6}};
          vector<ReplicatedShare<Ring>> r_mask_vec = randomShareWithParty_for_trun(id_, rgen_, indices);
        
          //生成r的每一比特共享
          auto [r_1_every_bit, r_2_every_bit] = comute_random_r_every_bit_sharing(id_, r_mask_vec, indices);
          //最后计算随机数r的共享和r^d的共享

          for(int i = 0; i<N; i++) {
            r_1 += r_1_every_bit[i].cosnt_mul((1ULL << i));
            r_2 += r_2_every_bit[i].cosnt_mul((1ULL << i));
            if(i>=FRACTION) {
              r_1_trunted_d += r_1_every_bit[i].cosnt_mul((1ULL << (i-FRACTION)));
              r_2_trunted_d += r_2_every_bit[i].cosnt_mul((1ULL << (i-FRACTION)));
            }
          }          
          
          vector<ReplicatedShare<Ring>> mask_in1_vec;
          vector<ReplicatedShare<Ring>> mask_in2_vec;
          for (size_t i = 0; i < g->in1.size(); i++) {
            mask_in1_vec.push_back(preproc.gates[g->in1[i]]->mask);
            mask_in2_vec.push_back(preproc.gates[g->in2[i]]->mask);
          }
          ReplicatedShare<Ring> mask_prod_dot = compute_prod_mask_dot(mask_in1_vec, mask_in2_vec);

          //生成三个共享，一个是mask，代表[r^d]，即最终的结果r^d的[·]-sharing部分
          //一个是mask_prod，代表[z]，即计算结果的共享[·]-sharing
          //最后一个是mask_d，代表随机数[r]的共享[·]-sharing
          ReplicatedShare<Ring> r = r_1 + r_2;
          ReplicatedShare<Ring> r_trunted_d = r_1_trunted_d + r_2_trunted_d;
          preproc.gates[g->out] = std::make_unique<PreprocTrDotpGate<Ring>>( 
              r_trunted_d, mask_prod_dot, r);
          break;
        }

        //要判断一个数x的正负
        case utils::GateType::kMsb: {
          break;
        }

        case utils::GateType::kRelu: {
          const auto* cmp_g = static_cast<utils::FIn1Gate*>(gate.get()); //一个输入的门
          auto mask_output_alpha = randomShareWithParty(id_, rgen_); //随机化输出值的α

          DummyShare<Ring> mask_mu_1; //随机化mu_1
          mask_mu_1.randomize(prg);
          auto mask_mu_1_share = mask_mu_1.getRSS(pid);
          auto mask_in = preproc.gates[cmp_g->in]->mask;

          auto mask_prod = compute_prod_mask(mask_mu_1_share, mask_in); //直接把关键的prod=(Σα1) x (Σα2)的共享计算出来

          DummyShare<Ring> mask_mu_2; //随机化mu_2
          mask_mu_2.randomize(prg);
          auto mask_mu_2_share = mask_mu_2.getRSS(pid);

          Ring beta_mu_1 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_1.secret();
          Ring beta_mu_2 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_2.secret();

          ReplicatedShare<Ring> prev_mask = mask_output_alpha;
          mask_output_alpha +=  mask_mu_2_share;  //alpha提前加好，后续不用加了
          
          ReplicatedShare<Ring> mask_for_mul = randomShareWithParty(id_, rgen_); //随机化mu_2

          //前面做了一次乘法，得到的结果是(x-y)大于0或者小于0，分别代表1和0，这里再做一次乘法，输入(x-y)，则输出relu的结果
          auto mask_prod2 = compute_prod_mask(mask_output_alpha, mask_in); //(x-y)和比较结果z的α做乘法

          preproc.gates[gate->out] = std::make_unique<PreprocReluGate<Ring>>(
              mask_output_alpha, mask_prod, mask_mu_1_share, mask_mu_2_share, 
              beta_mu_1, beta_mu_2, prev_mask, mask_prod2, mask_for_mul); //然后再把prod 重新share出去，这样下次做乘法，只用线性计算即可
          break;
        }

        case utils::GateType::kCmp: {
          /* The generation of sharing of mu_1 and mu_2 does not require communication, only local computation
          so it is assumed here that there is a third-party generater, and the impact on performance can be ignored */
          const auto* cmp_g = static_cast<utils::FIn1Gate*>(gate.get()); //一个输入的门
          auto mask_output_alpha = randomShareWithParty(id_, rgen_); //随机化输出值的α

          DummyShare<Ring> mask_mu_1; //随机化mu_1
          mask_mu_1.randomize(prg); //
          auto mask_mu_1_share = mask_mu_1.getRSS(pid);
          auto mask_in = preproc.gates[cmp_g->in]->mask;

          auto mask_prod = compute_prod_mask(mask_mu_1_share, mask_in); //直接把关键的prod=(Σα1) x (Σα2)的共享计算出来

          DummyShare<Ring> mask_mu_2; //随机化mu_2
          mask_mu_2.randomize(prg);
          auto mask_mu_2_share = mask_mu_2.getRSS(pid);

          Ring beta_mu_1 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_1.secret();
          Ring beta_mu_2 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_2.secret();

          ReplicatedShare<Ring> prev_mask = mask_output_alpha;
          mask_output_alpha +=  mask_mu_2_share;  //alpha提前加好，后续不用加了
          //除此之外，还有一个重要的操作，如果(x-y)>0，那么最终需要的α已经有了，但是β无法计算，所以我们需要预先计算好最终结果的β，否则计算不了。

          preproc.gates[gate->out] = std::make_unique<PreprocCmpGate<Ring>>(mask_output_alpha, mask_prod,
              mask_mu_1_share, mask_mu_2_share, beta_mu_1, beta_mu_2, prev_mask); //然后再把prod 重新share出去，这样下次做乘法，只用线性计算即可
          break;
        }

        default: {
          throw std::runtime_error("Invalid gate.");
          break;
        }
      }
    }
  }
  return preproc;
}

PreprocCircuit<Ring> OfflineEvaluator::dummy(
    const utils::LevelOrderedCircuit& circ,
    const std::unordered_map<utils::wire_t, int>& input_pid_map,
    size_t security_param, int pid, emp::PRG& prg) {
  PreprocCircuit<Ring> preproc(circ.num_gates, circ.outputs.size());
  auto msb_circ =
      utils::Circuit<BoolRing>::generatePPAMSB().orderGatesByLevel();

  std::vector<DummyShare<Ring>> wires(circ.num_gates);
  for (const auto& level : circ.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case utils::GateType::kInp: {
          wires[gate->out].randomize(prg); //inputGate只有out，随机出5个随机数，存在wires里面

          auto input_pid = input_pid_map.at(gate->out); //根据wire_id找输入的pid
          Ring mask_value = 0; //存的是五个随机数的和
          if (pid == input_pid) { //input_pid标记了谁输入，现在轮到标记的pid进行预处理了，他能知道5个随机数的和！
            mask_value = wires[gate->out].secret(); //mask_value = 5个随机数的和
          }

          preproc.gates[gate->out] = std::make_unique<PreprocInput<Ring>>( //预处理门保存RSS，即4个随机值，放在mask成员里面
              wires[gate->out].getRSS(pid), input_pid, mask_value);
          break;
        }

        case utils::GateType::kAdd: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          wires[g->out] = wires[g->in1] + wires[g->in2]; //wires[g->in1]是其中一个输入，是5个随机值。输入相加，就是随机值的和。说白了就是α的和，后面就只用加β了
          preproc.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(wires[gate->out].getRSS(pid));
          break;
        }

        case utils::GateType::kSub: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          wires[g->out] = wires[g->in1] - wires[g->in2];
          preproc.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(wires[gate->out].getRSS(pid));
          break;
        }

        case utils::GateType::kConstAdd: {
          const auto* g = static_cast<utils::ConstOpGate<Ring>*>(gate.get());
          wires[g->out] = wires[g->in];
          preproc.gates[g->out] =
              std::make_unique<PreprocGate<Ring>>(wires[g->out].getRSS(pid));
          break;
        }

        case utils::GateType::kConstMul: {
          const auto* g = static_cast<utils::ConstOpGate<Ring>*>(gate.get());
          wires[g->out] = wires[g->in] * g->cval;
          preproc.gates[g->out] =
              std::make_unique<PreprocGate<Ring>>(wires[g->out].getRSS(pid));
          break;
        }
        
        case utils::GateType::kMul: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          wires[g->out].randomize(prg); //为了生成α_z的共享
          Ring prod = wires[g->in1].secret() * wires[g->in2].secret(); //直接把关键的prod=(Σα1) x (Σα2)明文计算出来
          preproc.gates[gate->out] = std::make_unique<PreprocMultGate<Ring>>(
              wires[gate->out].getRSS(pid),
              DummyShare<Ring>(prod, prg).getRSS(pid)); //然后再把prod 重新share出去，这样下次做乘法，只用线性计算即可
          break;
        }

        case utils::GateType::kDotprod: {
          const auto* g = static_cast<utils::SIMDGate*>(gate.get());
          wires[g->out].randomize(prg);

          Ring mask_prod = 0;
          for (size_t i = 0; i < g->in1.size(); i++) {
            mask_prod += wires[g->in1[i]].secret() * wires[g->in2[i]].secret(); //直接把Σ(α1 x α2)计算出来
          }

          DummyShare<Ring> mask_prod_share(mask_prod, prg);
          preproc.gates[g->out] = std::make_unique<PreprocDotpGate<Ring>>(
              wires[g->out].getRSS(pid), mask_prod_share.getRSS(pid));
          break;
        }

        case utils::GateType::kTrdotp: {
          const auto* g = static_cast<utils::SIMDGate*>(gate.get());

          DummyShare<Ring> non_trunc_mask;
          non_trunc_mask.randomize(prg); //生成一个随机数r的共享[r]
          wires[g->out] =
              DummyShare<Ring>(non_trunc_mask.secret() >> FRACTION, prg);//得到[r^d]，即r的截断

          Ring mask_prod = 0;
          for (size_t i = 0; i < g->in1.size(); i++) {
            mask_prod += wires[g->in1[i]].secret() * wires[g->in2[i]].secret(); //直接把最终乘积的结果计算出来
          }
          DummyShare<Ring> mask_prod_share(mask_prod, prg); //
          //生成三个共享，一个是mask，代表[r^d]，即最终的结果r^d的[·]-sharing部分
          //一个是mask_prod，代表[z]，即计算结果的共享[·]-sharing
          //最后一个是mask_d，代表随机数[r]的共享[·]-sharing
          preproc.gates[g->out] = std::make_unique<PreprocTrDotpGate<Ring>>( 
              wires[g->out].getRSS(pid), mask_prod_share.getRSS(pid),
              non_trunc_mask.getRSS(pid));
          break;
        }

        //要判断一个数x的正负
        case utils::GateType::kMsb: {
          const auto* msb_g = static_cast<utils::FIn1Gate*>(gate.get()); //一个输入的门
          //先乘一个-1
          auto alpha = 
              -1 * wires[msb_g->in].secret();  // Removed multiplication by -1
          auto alpha_bits = bitDecompose(alpha);

          DummyShare<BoolRing> zero_share;
          std::fill(zero_share.share_elements.begin(),
                    zero_share.share_elements.end(), 0); //全部填0

          //msb_circ为一个计算最高有效位的电路,为这个新电路进行预处理
          std::vector<preprocg_ptr_t<BoolRing>> msb_gates(msb_circ.num_gates); // 新电路的，用来保存预计算好的RSS共享
          std::vector<DummyShare<BoolRing>> msb_wires(msb_circ.num_gates); // 新电路的wire

          size_t inp_counter = 0;
          //遍历电路msb_circ
          for (const auto& msb_level : msb_circ.gates_by_level) {
            for (const auto& msb_gate : msb_level) {
              switch (msb_gate->type) {
                case utils::GateType::kInp: {
                  auto mask = zero_share;
                  if (inp_counter < 64) { //小于64的都是输入
                    mask = DummyShare<BoolRing>(alpha_bits[inp_counter], prg); //把第inp_counter个比特，共享成5个随机数的和
                  }
                  msb_wires[msb_gate->out] = mask; //第i bit的5个共享值
                  msb_gates[msb_gate->out] = std::make_unique<PreprocGate<BoolRing>>(mask.getRSS(pid)); //4个共享值，作为输入保存在一个门中，即msb_gates
                  inp_counter++;
                  break;
                }

                case utils::GateType::kAdd: {
                  const auto* g = static_cast<utils::FIn2Gate*>(msb_gate.get());
                  msb_wires[g->out] = msb_wires[g->in1] + msb_wires[g->in2];
                  msb_gates[g->out] = std::make_unique<PreprocGate<BoolRing>>(
                      msb_wires[g->out].getRSS(pid));
                  break;
                }

                case utils::GateType::kMul: {
                  const auto* g = static_cast<utils::FIn2Gate*>(msb_gate.get());
                  msb_wires[g->out].randomize(prg);
                  BoolRing prod = msb_wires[g->in1].secret() * msb_wires[g->in2].secret();
                  auto prod_share = DummyShare<BoolRing>(prod, prg);

                  msb_gates[g->out] =
                      std::make_unique<PreprocMultGate<BoolRing>>(
                          msb_wires[g->out].getRSS(pid),
                          prod_share.getRSS(pid));
                  break;
                }

                default: {
                  break;
                }
              }
            }
          }

          const auto& out_mask = msb_wires[msb_circ.outputs[0]]; //一个MSB只有一个输出，out_mask代表输出线的掩码

          Ring alpha_msb = out_mask.secret().val();//Σα
          DummyShare<Ring> mask_msb(alpha_msb, prg);

          DummyShare<Ring> mask_w;
          mask_w.randomize(prg);

          Ring alpha_w, alpha_btoa;
          alpha_w = mask_w.secret();

          wires[msb_g->out] =
              static_cast<Ring>(-1) * mask_msb + static_cast<Ring>(-2) * mask_w;

          preproc.gates[msb_g->out] = std::make_unique<PreprocMsbGate<Ring>>(
              wires[msb_g->out].getRSS(pid), std::move(msb_gates), //msb_gates尤为重要，他代表预计算好的MSB所有子门的预计算结果，有443个bool门
              mask_msb.getRSS(pid), mask_w.getRSS(pid));
          break;
        }

        case utils::GateType::kRelu: {
          const auto* cmp_g = static_cast<utils::FIn1Gate*>(gate.get()); //一个输入的门
          wires[cmp_g->out].randomize(prg); //随机化输出值的α

          DummyShare<Ring> mask_mu_1; //随机化mu_1
          mask_mu_1.randomize(prg);
          Ring prod = wires[cmp_g->in].secret() * mask_mu_1.secret(); //直接把关键的prod=(Σα1) x (Σα2)明文计算出来

          DummyShare<Ring> mask_mu_2; //随机化mu_2
          mask_mu_2.randomize(prg);

          Ring beta_mu_1 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_1.secret();
          Ring beta_mu_2 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_2.secret();

          DummyShare<Ring> prev_mask(wires[gate->out]);
          wires[gate->out] +=  mask_mu_2;  //alpha提前加好，后续不用加了
          
          DummyShare<Ring> mask_for_mul; //随机化mu_2
          mask_for_mul.randomize(prg);

          //前面做了一次乘法，得到的结果是(x-y)大于0或者小于0，分别代表1和0，这里再做一次乘法，输入(x-y)，则输出relu的结果
          Ring prod2 = wires[gate->out].secret() * wires[cmp_g->in].secret(); //(x-y)和比较结果z的α做乘法
          preproc.gates[gate->out] = std::make_unique<PreprocReluGate<Ring>>(
              wires[gate->out].getRSS(pid), DummyShare<Ring>(prod, prg).getRSS(pid),
              mask_mu_1.getRSS(pid), mask_mu_2.getRSS(pid), beta_mu_1, beta_mu_2, prev_mask.getRSS(pid), DummyShare<Ring>(prod2, prg).getRSS(pid), mask_for_mul.getRSS(pid)); //然后再把prod 重新share出去，这样下次做乘法，只用线性计算即可
          break;
        }

        case utils::GateType::kCmp: {
          const auto* cmp_g = static_cast<utils::FIn1Gate*>(gate.get()); //一个输入的门
          wires[cmp_g->out].randomize(prg); //随机化输出值的α

          DummyShare<Ring> mask_mu_1; //随机化mu_1
          mask_mu_1.randomize(prg);
          Ring prod = wires[cmp_g->in].secret() * mask_mu_1.secret(); //直接把关键的prod=(Σα1) x (Σα2)明文计算出来

          DummyShare<Ring> mask_mu_2; //随机化mu_2
          mask_mu_2.randomize(prg);

          Ring beta_mu_1 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_1.secret();
          Ring beta_mu_2 = generate_specific_bit_random(prg, BITS_BETA) + mask_mu_2.secret();

          DummyShare<Ring> prev_mask(wires[gate->out]);
          wires[gate->out] +=  mask_mu_2;  //alpha提前加好，后续不用加了
          //除此之外，还有一个重要的操作，如果(x-y)>0，那么最终需要的α已经有了，但是β无法计算，所以我们需要预先计算好最终结果的β，否则计算不了。

          preproc.gates[gate->out] = std::make_unique<PreprocCmpGate<Ring>>(
              wires[gate->out].getRSS(pid), DummyShare<Ring>(prod, prg).getRSS(pid),
              mask_mu_1.getRSS(pid), mask_mu_2.getRSS(pid), beta_mu_1, beta_mu_2, prev_mask.getRSS(pid)); //然后再把prod 重新share出去，这样下次做乘法，只用线性计算即可
          break;
        }
        default: {
          throw std::runtime_error("Invalid gate.");
          break;
        }
      }
    }
  }
  return preproc;
}

std::array<vector<Ring>, 22> OfflineEvaluator::reshare_gen_random_vector(int pid, RandGenPool& rgen, int array_length) {
  std::array<vector<Ring>, 22> result;
  for(int j =  0; j < array_length; j++) {
    Ring sum_temp = 0;
    Ring temp;
    for(int i = 0; i < 21; i++) {
      rgen.getComplement(pid).random_data(&temp, sizeof(Ring));
      // temp = temp % (1ULL<<8);
      result[i].push_back(temp);
      sum_temp += temp;
    }
    result[21].push_back(sum_temp);
  }
  return result;
}

PreprocCircuit_permutation<Ring> OfflineEvaluator::dummy_permutation(
  const utils::LevelOrderedCircuit& circ,
  const std::unordered_map<utils::wire_t, int>& input_pid_map,
  size_t security_param, int pid, emp::PRG& prg, vector<Ring>& data_vector, vector<Ring>& permutation_vector) {

  if(data_vector.size() != permutation_vector.size()) {
    throw std::runtime_error("data vector size must be equal to permutation size.");
  }
  uint64_t nf =  data_vector.size();
  int input_pid = INPUT_PERMUTATION;
  vector<ReplicatedShare<Ring>> data_sharing_vec(nf);

  //Firstly, share the data array
  vector<Ring> mask_value_vec(nf);
  for(uint64_t i = 0; i<nf ; i++) {
    DummyShare<Ring> temp;
    temp.randomize(prg);
    Ring mask_value = 0;
    
    if (pid == INPUT_PERMUTATION) { //fix party INPUT_PERMUTATION inputs the data
      mask_value = temp.secret(); //mask_value = 5个随机数的和
    }
    mask_value_vec[i] = mask_value;
    data_sharing_vec[i] = temp.getRSS(pid);
    // data_sharing_vec[i] = std::make_unique<PreprocInput<Ring>>( //预处理门保存RSS，即4个随机值，放在mask成员里面
    //           temp.getRSS(pid), input_pid, mask_value);
  }

  //share the permutation
  PermutationDummyShare<Ring> temp_dummy_perm_sharing(nf);
  temp_dummy_perm_sharing.randomize(prg);
  
  PermutationShare<Ring> perm_sharing = temp_dummy_perm_sharing.getRSS(pid);
  preprocg_ptr_t_perm<Ring> perm_share_ptr = std::make_unique<PreprocPermutation<Ring>>(
                                                  temp_dummy_perm_sharing.getRSS(pid), 
                                                  input_pid, 
                                                  temp_dummy_perm_sharing.secret()
                                              );
  
  //start to offline
  
  std::array<std::array<int,2>,21> Index = {{
    {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, 
    {1,2}, {1,3}, {1,4}, {1,5}, {1,6},
    {2,3}, {2,4}, {2,5}, {2,6},
    {3,4}, {3,5}, {3,6},
    {4,5}, {4,6},
    {5,6}
  }};
  vector<vector<Ring>> saved_beta(Index.size());
  // sleep(pid);
  // std::cout<<"pid: "<<pid<<endl;
  auto random_vector_array = reshare_gen_random_vector(pid, rgen_, nf);
  for(int i = 0 ; i < Index.size(); i++) { //遍历所有可能性
    size_t nbytes = sizeof(Ring) * nf;
    auto [i_temp,j_temp,k_temp,l_temp,m_temp] = findRemainingNumbers_7PC(Index[i][0], Index[i][1]);
    auto n_temp = Index[i][0];
    auto o_temp = Index[i][1];
    if(pid != Index[i][0] && pid != Index[i][1])  {
      auto alpha_i = perm_sharing[upperTriangularToArray(Index[i][0], Index[i][1])];
      applyPermutation(alpha_i, data_sharing_vec);
      
      //reshare protocol
      
      saved_beta[upperTriangularToArray(Index[i][0], Index[i][1])] = random_vector_array[21]; //每一方总共能存21个，而且是按照顺序存的。
      for(int j = 0; j < nf; j++) { //遍历所有共享
        for(int k= 0; k<Index.size(); k++) { //每一方遍历自己的所有共享
          if(Index[k][0] != pid && Index[i][1] != pid) { //把自己的共享找出来
            data_sharing_vec[j][upperTriangularToArray(Index[k][0], Index[k][1])] += 
            random_vector_array[upperTriangularToArray(Index[k][0], Index[k][1])][j];
          }
        }
      }
      

      if(pid == k_temp || pid == l_temp || pid == m_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, j_temp)]);
        }
        jump_.jumpUpdate(k_temp, l_temp, m_temp, n_temp, nbytes, alpha_temp.data());
        jump_.jumpUpdate(k_temp, l_temp, m_temp, o_temp, nbytes, alpha_temp.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == j_temp || pid == l_temp || pid == m_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, k_temp)]);
        }
        jump_.jumpUpdate(j_temp, l_temp, m_temp, n_temp, nbytes, alpha_temp.data());
        jump_.jumpUpdate(j_temp, l_temp, m_temp, o_temp, nbytes, alpha_temp.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == j_temp || pid == k_temp || pid == m_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, l_temp)]);
        }
        jump_.jumpUpdate(j_temp, k_temp, m_temp, n_temp, nbytes, alpha_temp.data());
        jump_.jumpUpdate(j_temp, k_temp, m_temp, o_temp, nbytes, alpha_temp.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == l_temp || pid == m_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(j_temp, k_temp)]);
        }
        jump_.jumpUpdate(i_temp, l_temp, m_temp, n_temp, nbytes, alpha_temp.data());
        jump_.jumpUpdate(i_temp, l_temp, m_temp, o_temp, nbytes, alpha_temp.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == k_temp || pid == m_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(j_temp, l_temp)]);
        }
        jump_.jumpUpdate(i_temp, k_temp, m_temp, n_temp, nbytes, alpha_temp.data());
        jump_.jumpUpdate(i_temp, k_temp, m_temp, o_temp, nbytes, alpha_temp.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == j_temp || pid == m_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(k_temp, l_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, m_temp, n_temp, nbytes, alpha_temp.data());
        jump_.jumpUpdate(i_temp, j_temp, m_temp, o_temp, nbytes, alpha_temp.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == j_temp || pid == k_temp || pid == l_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, m_temp)]);
        }
        jump_.jumpUpdate(j_temp, k_temp, l_temp, n_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, o_temp)]);
        }
        jump_.jumpUpdate(j_temp, k_temp, l_temp, n_temp, nbytes, alpha_temp1.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == k_temp || pid == l_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(j_temp, m_temp)]);
        }
        jump_.jumpUpdate(i_temp, k_temp, l_temp, n_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(j_temp, o_temp)]);
        }
        jump_.jumpUpdate(i_temp, k_temp, l_temp, n_temp, nbytes, alpha_temp1.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == j_temp || pid == l_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(k_temp, m_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, l_temp, n_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(k_temp, o_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, l_temp, n_temp, nbytes, alpha_temp1.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == j_temp || pid == k_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(l_temp, m_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, k_temp, n_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(l_temp, o_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, k_temp, n_temp, nbytes, alpha_temp1.data());

        vector<Ring> alpha_temp2;
        for(int j = 0; j<nf; j++) {
          alpha_temp2.push_back(data_sharing_vec[j][upperTriangularToArray(m_temp, o_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, k_temp, n_temp, nbytes, alpha_temp2.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == j_temp || pid == k_temp || pid == l_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, m_temp)]);
        }
        jump_.jumpUpdate(j_temp, k_temp, l_temp, o_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(i_temp, n_temp)]);
        }
        jump_.jumpUpdate(j_temp, k_temp, l_temp, o_temp, nbytes, alpha_temp1.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == k_temp || pid == l_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(j_temp, m_temp)]);
        }
        jump_.jumpUpdate(i_temp, k_temp, l_temp, o_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(j_temp, n_temp)]);
        }
        jump_.jumpUpdate(i_temp, k_temp, l_temp, o_temp, nbytes, alpha_temp1.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == j_temp || pid == l_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(k_temp, m_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, l_temp, o_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(k_temp, n_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, l_temp, o_temp, nbytes, alpha_temp1.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }
      if(pid == i_temp || pid == j_temp || pid == k_temp) {
        vector<Ring> alpha_temp;
        for(int j = 0; j<nf; j++) {
          alpha_temp.push_back(data_sharing_vec[j][upperTriangularToArray(l_temp, m_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, k_temp, o_temp, nbytes, alpha_temp.data());

        vector<Ring> alpha_temp1;
        for(int j = 0; j<nf; j++) {
          alpha_temp1.push_back(data_sharing_vec[j][upperTriangularToArray(l_temp, n_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, k_temp, o_temp, nbytes, alpha_temp1.data());

        vector<Ring> alpha_temp2;
        for(int j = 0; j<nf; j++) {
          alpha_temp2.push_back(data_sharing_vec[j][upperTriangularToArray(m_temp, o_temp)]);
        }
        jump_.jumpUpdate(i_temp, j_temp, k_temp, o_temp, nbytes, alpha_temp2.data());
        // std::cout<<i_temp<<", "<<j_temp<<", "<<k_temp<<", "<<i<<endl;
      }

    }
    else { //接受消息，更新自己的alpha
      if(pid == n_temp || pid == o_temp) {
        jump_.jumpUpdate(k_temp, l_temp, m_temp, pid, nbytes, nullptr);
        jump_.jumpUpdate(j_temp, l_temp, m_temp, pid, nbytes, nullptr);
        jump_.jumpUpdate(j_temp, k_temp, m_temp, pid, nbytes, nullptr);
        jump_.jumpUpdate(i_temp, l_temp, m_temp, pid, nbytes, nullptr);
        jump_.jumpUpdate(i_temp, k_temp, m_temp, pid, nbytes, nullptr);
        jump_.jumpUpdate(i_temp, j_temp, m_temp, pid, nbytes, nullptr);
        jump_.jumpUpdate(j_temp, k_temp, l_temp, pid, 2*nbytes, nullptr);
        jump_.jumpUpdate(i_temp, k_temp, l_temp, pid, 2*nbytes, nullptr);
        jump_.jumpUpdate(i_temp, j_temp, l_temp, pid, 2*nbytes, nullptr);
        jump_.jumpUpdate(i_temp, j_temp, k_temp, pid, 3*nbytes, nullptr);
      }
    }
    jump_.communicate(*network_, *tpool_);
    
    //update the share
    if(pid == n_temp || pid == o_temp) {
      vector<Ring> miss_values(nf);

      // 获取数据
      const auto* temp = reinterpret_cast<const Ring*>(jump_.getValues(k_temp, l_temp, m_temp).data());
      std::copy(temp, temp + nf, miss_values.begin());
      for (size_t j = 0; j < nf; j++) {
          data_sharing_vec[j][upperTriangularToArray(i_temp, j_temp)] = miss_values[j];
      }

      temp = reinterpret_cast<const Ring*>(jump_.getValues(j_temp, l_temp, m_temp).data());
      std::copy(temp, temp + nf, miss_values.begin());
      for (size_t j = 0; j < nf; j++) {
          data_sharing_vec[j][upperTriangularToArray(i_temp, k_temp)] = miss_values[j];
      }

      temp = reinterpret_cast<const Ring*>(jump_.getValues(j_temp, k_temp, m_temp).data());
      std::copy(temp, temp + nf, miss_values.begin());
      for (size_t j = 0; j < nf; j++) {
          data_sharing_vec[j][upperTriangularToArray(i_temp, l_temp)] = miss_values[j];
      }

      temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, l_temp, m_temp).data());
      std::copy(temp, temp + nf, miss_values.begin());
      for (size_t j = 0; j < nf; j++) {
          data_sharing_vec[j][upperTriangularToArray(i_temp, k_temp)] = miss_values[j];
      }

      temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, k_temp, m_temp).data());
      std::copy(temp, temp + nf, miss_values.begin());
      for (size_t j = 0; j < nf; j++) {
          data_sharing_vec[j][upperTriangularToArray(j_temp, l_temp)] = miss_values[j];
      }

      temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, j_temp, m_temp).data());
      std::copy(temp, temp + nf, miss_values.begin());
      for (size_t j = 0; j < nf; j++) {
          data_sharing_vec[j][upperTriangularToArray(k_temp, l_temp)] = miss_values[j];
      }

      if(pid == n_temp) {
        temp = reinterpret_cast<const Ring*>(jump_.getValues(j_temp, k_temp, l_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(i_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(i_temp, o_temp)] = miss_values[j];
        }

        temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, k_temp, l_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(j_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(j_temp, o_temp)] = miss_values[j];
        }

        temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, j_temp, l_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(k_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(k_temp, o_temp)] = miss_values[j];
        }

        temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, j_temp, k_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(l_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(l_temp, o_temp)] = miss_values[j];
        }
        std::copy(temp+2*nf, temp + 3*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(m_temp, o_temp)] = miss_values[j];
        }
      }

      if(pid == o_temp) {
        temp = reinterpret_cast<const Ring*>(jump_.getValues(j_temp, k_temp, l_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(i_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(i_temp, n_temp)] = miss_values[j];
        }

        temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, k_temp, l_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(j_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(j_temp, n_temp)] = miss_values[j];
        }

        temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, j_temp, l_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(k_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(k_temp, n_temp)] = miss_values[j];
        }

        temp = reinterpret_cast<const Ring*>(jump_.getValues(i_temp, j_temp, k_temp).data());
        std::copy(temp, temp + nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(l_temp, m_temp)] = miss_values[j];
        }
        std::copy(temp+nf, temp + 2*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(l_temp, n_temp)] = miss_values[j];
        }
        std::copy(temp+2*nf, temp + 3*nf, miss_values.begin());
        for (size_t j = 0; j < nf; j++) {
            data_sharing_vec[j][upperTriangularToArray(m_temp, n_temp)] = miss_values[j];
        }
      }
    }
    jump_.reset(); 
  }
  
  PreprocCircuit_permutation<Ring> preproc_perm(
    std::move(data_sharing_vec),
    std::move(perm_share_ptr),
    std::move(saved_beta),
    std::move(mask_value_vec));
  return preproc_perm;
  
}
};  // namespace SemiHoRGod
