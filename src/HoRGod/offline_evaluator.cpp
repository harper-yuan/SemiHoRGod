#include "offline_evaluator.h"

#include <NTL/BasicThreadPool.h>
#include <NTL/vec_ZZ_pE.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>

#include "helpers.h"
#include "ijmp.h"
#include "online_evaluator.h"

namespace HoRGod{
OfflineEvaluator::OfflineEvaluator(int my_id,
                                   std::shared_ptr<io::NetIOMP<5>> network1,
                                   std::shared_ptr<io::NetIOMP<5>> network2,
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

std::vector<Ring> OfflineEvaluator::reconstruct(
    const std::array<std::vector<Ring>, 4>& recon_shares) {
  // All vectors in recon_shares should have same size.
  size_t num = recon_shares[0].size();
  size_t nbytes = sizeof(Ring) * num;

  if (nbytes == 0) {
    return {};
  }

  std::vector<Ring> vres(num);

  jump_.jumpUpdate(id_, pidFromOffset(id_, 1), pidFromOffset(id_, 2), pidFromOffset(id_, -1), nbytes, 
                  recon_shares[idxFromSenderAndReceiver(id_, pidFromOffset(id_, -1))].data());
  jump_.jumpUpdate(pidFromOffset(id_, -1), id_, pidFromOffset(id_, 1), pidFromOffset(id_, -2), nbytes, 
                  recon_shares[idxFromSenderAndReceiver(id_, pidFromOffset(id_, -2))].data());
  jump_.jumpUpdate(pidFromOffset(id_, -2), pidFromOffset(id_, -1), id_, pidFromOffset(id_, -3), nbytes, 
                  recon_shares[idxFromSenderAndReceiver(id_, pidFromOffset(id_, -3))].data());
  jump_.jumpUpdate(pidFromOffset(id_, 1), pidFromOffset(id_, 2), pidFromOffset(id_, 3), id_, nbytes, 
                  recon_shares[idxFromSenderAndReceiver(id_, pidFromOffset(id_, -3))].data());
  jump_.communicate(*network_, *tpool_);

  //reinterpret_cast 的作用是 对指针类型进行低级别的重新解释，即将原始指针类型强制转换为另一种不相关的指针类型（这里是 const Ring*），而无需修改底层数据。
  const auto* miss_values = reinterpret_cast<const Ring*>(jump_.getValues(pidFromOffset(id_, 1), pidFromOffset(id_, 2), pidFromOffset(id_, 3)).data());       
  std::copy(miss_values, miss_values + num, vres.begin());
  for (size_t i = 0; i<num; i++) {
    vres[i] = vres[i] + recon_shares[0][i] + recon_shares[1][i] + recon_shares[2][i] + recon_shares[3][i];
  }
  jump_.reset();
  return vres;
}

void OfflineEvaluator::randomShare(RandGenPool& rgen,
                                   ReplicatedShare<Ring>& share) {
  rgen.getRelative(1).random_data(&share[0], sizeof(Ring));
  rgen.getRelative(2).random_data(&share[1], sizeof(Ring));
  rgen.getRelative(3).random_data(&share[2], sizeof(Ring));
}

ReplicatedShare<Ring> OfflineEvaluator::jshShare(int id, RandGenPool& rgen, int i, int j, int k) {
  ReplicatedShare<Ring> result;
  // id是当前执行这个函数的参与方i，他需要获取{0,1,2,3,4}\{i}
  int idx = 0;
  for (int pid = 0; pid < 5; ++pid) {
    if (pid == id) { //他无法获取共享x_{id}这个数据
      continue;
    }
    else {
      //直接使用公共的随机数种子生成数据，可能不安全，但效率是一样的
      rgen.self().random_data(&result[idx], sizeof(Ring));
      result[idx] = pid;
      idx++;
    }
  }
  return result;
}

std::vector<Ring> OfflineEvaluator::reconstruct(
    const std::vector<ReplicatedShare<Ring>>& shares) {
  std::array<std::vector<Ring>, 4> recon_shares;
  for (const auto& s : shares) {
    for (size_t i = 0; i < 4; ++i) {
      recon_shares[i].push_back(s[i]);
    }
  }
  return reconstruct(recon_shares);
}


ReplicatedShare<Ring> OfflineEvaluator::randomShareWithParty(int id, RandGenPool& rgen) {
  ReplicatedShare<Ring> result;
  // id是当前执行这个函数的参与方i，他需要获取{0,1,2,3,4}\{i}
  int idx = 0;
  for (int pid = 0; pid < 5; ++pid) {
    if (pid == id) { //他无法获取共享x_{id}这个数据
      continue;
    }
    else {
      //直接使用公共的随机数种子生成数据，可能不安全，但效率是一样的
      rgen.getComplement(pid).random_data(&result[idx++], sizeof(Ring));
    }
  }
  return result;
}

void OfflineEvaluator::randomShareWithParty(int id, int dealer,
                                            RandGenPool& rgen,
                                            ReplicatedShare<Ring>& share) {
  // id是当前执行这个函数的参与方i，他需要获取{0,1,2,3,4}\{i}
  int idx = 0;
  for (int pid = 0; pid < 5; ++pid) {
    if (pid == id) { //他无法获取共享x_{id}这个数据
      continue;
    }
    else {
      //如果碰到数据x_{dealer}，dealer是需要知道x_{dealer}的，所以用公共的随机数种子生成数据
      if (pid == dealer) {
        rgen.getComplement(id).random_data(&share[idx++], sizeof(Ring));
      }
      else {
        rgen.getComplement(pid).random_data(&share[idx++], sizeof(Ring));
      }
      
    }
  }
}

//如果是秘密的持有者，那么执行共享，除了得到秘密的共享，还会得到真实的秘密
void OfflineEvaluator::randomShareWithParty(int id, RandGenPool& rgen,
                                            ReplicatedShare<Ring>& share,
                                            Ring& secret) {
  // id是当前执行这个函数的参与方i，他需要获取{0,1,2,3,4}\{i}
  int idx = 0;
  secret = 0;
  Ring temp = 0;
  for (int pid = 0; pid < 5; ++pid) {
    if (pid == id) { //他无法获取共享x_i
      rgen.getComplement(pid).random_data(&temp, sizeof(Ring));
      secret += temp;
    }
    else {
      rgen.getComplement(pid).random_data(&share[idx], sizeof(Ring));
      secret += share[idx];
      idx++;
    }
  }
}

ReplicatedShare<Ring> OfflineEvaluator::randomShareWithParty_for_trun(int id, RandGenPool& rgen, int number_random_id) {
  ReplicatedShare<Ring> result;
  result.init_zero();
  if(number_random_id == 0) { //for r1
    if(id == 0) {
      return result;
    }
    else{
      //参与方0没有这个随机数种子
      rgen.getComplement(0).random_data(&result[idxFromSenderAndReceiver(id, 0)], sizeof(Ring));
    }
  }
  else if(number_random_id == 1) { //for r2
    if(id == 1) {
      return result;
    }
    else{
      //参与方1没有这个随机数种子
      rgen.getComplement(1).random_data(&result[idxFromSenderAndReceiver(id, 1)], sizeof(Ring));
    }
  }
  else if(number_random_id == 2) { //for r3
    if(id == 2) {
      return result;
    }
    else{
      // 参与方2没有这个随机数种子
      rgen.getComplement(2).random_data(&result[idxFromSenderAndReceiver(id, 2)], sizeof(Ring));
    }
  }

  return result;
}

ReplicatedShare<Ring> OfflineEvaluator::compute_prod_mask(ReplicatedShare<Ring> mask_in1, ReplicatedShare<Ring> mask_in2) {
  ReplicatedShare<Ring> mask_prod;
  mask_prod.init_zero();
  
  //①计算α_xy = α_x * α_y的共享，除了要接受消息，还要发送消息
  for(int i = 0; i<5; i++) {
    for (int j = i+1; j<5; j++) {
      if(i == id_ || j == id_) {
        //无法生成α_{xyij}，需要接受收消息，这种情况会发送四次
        // Ring x_m;
        auto [min, mid, max] = findRemainingNumbers(i, j);
        jump_.jumpUpdate(min, mid, max, id_, (size_t) sizeof(Ring), nullptr);
      }
      else {
        //发送消息
        auto [min, mid, max] = sortThreeNumbers(id_, i, j); //这三个人可以计算的值有
        auto [other1, other2] = findRemainingNumbers(min, mid, max); //一定有other1<ohter2
        //计算α_{xy,ohter1,other2}
        auto alpha_x_y_other1_other2 = mask_in1[idxFromSenderAndReceiver(id_, other1)] * mask_in2[idxFromSenderAndReceiver(id_, other2)]+
                                        mask_in1[idxFromSenderAndReceiver(id_, other2)] * mask_in2[idxFromSenderAndReceiver(id_, other1)];
        if (min == 0 && mid == 1 && max == 2) {
          alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, 3)] * mask_in2[idxFromSenderAndReceiver(id_, 3)] + 
                                      mask_in1[idxFromSenderAndReceiver(id_, 4)] * mask_in2[idxFromSenderAndReceiver(id_, 4)];
        }
        else if (min == 2 && mid == 3 && max == 4) {
          alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, 0)] * mask_in2[idxFromSenderAndReceiver(id_, 0)] + 
                                      mask_in1[idxFromSenderAndReceiver(id_, 1)] * mask_in2[idxFromSenderAndReceiver(id_, 1)];
        }
        else if (min == 0 && mid == 1 && max == 3) {
          alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, 2)] * mask_in2[idxFromSenderAndReceiver(id_, 2)];
        }
        //然后把数据share出去
        auto alpha_x_y_other1_other2_mask = jshShare(id_, rgen_, min, mid, max);
        auto x_m = alpha_x_y_other1_other2 - (min+mid+max+other1);
        alpha_x_y_other1_other2_mask[idxFromSenderAndReceiver(id_, other2)] = x_m;

        //自己把最终结果加上
        mask_prod += alpha_x_y_other1_other2_mask;
        //按顺序排序，这样其他发送者的发送参数是一样的，接收者也用一样的接受参数接受数据
        jump_.jumpUpdate(min, mid, max, other1, (size_t) sizeof(Ring), &x_m);
        jump_.jumpUpdate(min, mid, max, other2, (size_t) sizeof(Ring), &x_m);
      }
    }
  }
  
  //通信得到数据
  jump_.communicate(*network_, *tpool_);

  for(int i = 0; i<5; i++) {
    for (int j = i+1; j<5; j++) {
      if (i == id_ || j == id_) {
        auto [min, mid, max] = findRemainingNumbers(i, j);
        if(i == id_) { //如果是参与方l，那么需要用通信协议来更新x_m
          // Ring x_m;
          auto alpha_x_y_i_j_mask = jshShare(id_, rgen_, min, mid, max);
          const Ring *x_m = reinterpret_cast<const Ring*>(jump_.getValues(min, mid, max).data());
          alpha_x_y_i_j_mask[idxFromSenderAndReceiver(id_, j)] = *x_m; //j就对应x_m中的m
          //自己吧最终结果加上
          mask_prod += alpha_x_y_i_j_mask;
        }
        else if(j == id_) { //如果是参与方m，那么只需要得到x_i,x_j,x_k,x_l即可，这些通过随机生成已经获得了
          auto alpha_x_y_i_j_mask = jshShare(id_, rgen_, min, mid, max);
          //自己吧最终结果加上
          mask_prod += alpha_x_y_i_j_mask;
        }
      }
    }
  }
  jump_.reset();
  return mask_prod;
}

ReplicatedShare<Ring> OfflineEvaluator::compute_prod_mask_dot(vector<ReplicatedShare<Ring>> mask_in1_vec, vector<ReplicatedShare<Ring>> mask_in2_vec) {
  ReplicatedShare<Ring> mask_prod;
  mask_prod.init_zero();
  
  //①计算α_xy = α_x * α_y的共享，除了要接受消息，还要发送消息
  for(int i = 0; i<5; i++) {
    for (int j = i+1; j<5; j++) {
      if(i == id_ || j == id_) {
        //无法生成α_{xyij}，需要接受收消息，这种情况会发送四次
        // Ring x_m;
        auto [min, mid, max] = findRemainingNumbers(i, j);
        jump_.jumpUpdate(min, mid, max, id_, (size_t) sizeof(Ring), nullptr);
      }
      else {
        //发送消息
        auto [min, mid, max] = sortThreeNumbers(id_, i, j); //这三个人可以计算的值有
        auto [other1, other2] = findRemainingNumbers(min, mid, max); //一定有other1<ohter2
        //计算α_{xy,ohter1,other2}
        Ring alpha_x_y_other1_other2 = 0;
        for(int t = 0; t<mask_in1_vec.size(); t++) {
          auto &mask_in1 = mask_in1_vec[t];
          auto &mask_in2 = mask_in2_vec[t];
          alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, other1)] * mask_in2[idxFromSenderAndReceiver(id_, other2)]+
                                        mask_in1[idxFromSenderAndReceiver(id_, other2)] * mask_in2[idxFromSenderAndReceiver(id_, other1)];
          if (min == 0 && mid == 1 && max == 2) {
            alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, 3)] * mask_in2[idxFromSenderAndReceiver(id_, 3)] + 
                                        mask_in1[idxFromSenderAndReceiver(id_, 4)] * mask_in2[idxFromSenderAndReceiver(id_, 4)];
          }
          else if (min == 2 && mid == 3 && max == 4) {
            alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, 0)] * mask_in2[idxFromSenderAndReceiver(id_, 0)] + 
                                        mask_in1[idxFromSenderAndReceiver(id_, 1)] * mask_in2[idxFromSenderAndReceiver(id_, 1)];
          }
          else if (min == 0 && mid == 1 && max == 3) {
            alpha_x_y_other1_other2 += mask_in1[idxFromSenderAndReceiver(id_, 2)] * mask_in2[idxFromSenderAndReceiver(id_, 2)];
          }
        }
        
        //然后把数据share出去
        auto alpha_x_y_other1_other2_mask = jshShare(id_, rgen_, min, mid, max);
        auto x_m = alpha_x_y_other1_other2 - (min+mid+max+other1);
        alpha_x_y_other1_other2_mask[idxFromSenderAndReceiver(id_, other2)] = x_m;

        //自己把最终结果加上
        mask_prod += alpha_x_y_other1_other2_mask;
        //按顺序排序，这样其他发送者的发送参数是一样的，接收者也用一样的接受参数接受数据
        jump_.jumpUpdate(min, mid, max, other1, (size_t) sizeof(Ring), &x_m);
        jump_.jumpUpdate(min, mid, max, other2, (size_t) sizeof(Ring), &x_m);
      }
    }
  }
  
  //通信得到数据
  jump_.communicate(*network_, *tpool_);

  for(int i = 0; i<5; i++) {
    for (int j = i+1; j<5; j++) {
      if (i == id_ || j == id_) {
        auto [min, mid, max] = findRemainingNumbers(i, j);
        if(i == id_) { //如果是参与方l，那么需要用通信协议来更新x_m
          // Ring x_m;
          auto alpha_x_y_i_j_mask = jshShare(id_, rgen_, min, mid, max);
          const Ring *x_m = reinterpret_cast<const Ring*>(jump_.getValues(min, mid, max).data());
          alpha_x_y_i_j_mask[idxFromSenderAndReceiver(id_, j)] = *x_m; //j就对应x_m中的m
          //自己吧最终结果加上
          mask_prod += alpha_x_y_i_j_mask;
        }
        else if(j == id_) { //如果是参与方m，那么只需要得到x_i,x_j,x_k,x_l即可，这些通过随机生成已经获得了
          auto alpha_x_y_i_j_mask = jshShare(id_, rgen_, min, mid, max);
          //自己吧最终结果加上
          mask_prod += alpha_x_y_i_j_mask;
        }
      }
    }
  }
  jump_.reset();
  return mask_prod;
}


vector<ReplicatedShare<Ring>> OfflineEvaluator::comute_random_r_every_bit_sharing(int id, ReplicatedShare<Ring> r_1_mask,
                                                                          ReplicatedShare<Ring> r_2_mask,
                                                                          ReplicatedShare<Ring> r_3_mask) {
  //然后首先计算c = r1 xor r2，这里需要调用乘法了
  vector<ReplicatedShare<Ring>> r_1_xor_r_2_mask;
  Ring r1, r2, r3;
  
  if(id != 0) {
    r1 = r_1_mask[idxFromSenderAndReceiver(id, 0)];
  }
  else {
    r1 = 0;
  }

  if(id != 1) {
    r2 = r_2_mask[idxFromSenderAndReceiver(id, 1)];
  }
  else {
    r2 = 0;
  }

  for(int i = 0; i<N; i++) {
    ReplicatedShare<Ring> temp;
    //计算r1的第i比特共享
    ReplicatedShare<Ring> r_1_i_bit_mask;
    r_1_i_bit_mask.init_zero();
    if(((r1 >> i) & 1ULL) == 1 && id != 0) { //计算r1的第i比特的共享，要确保五个人的共享值复原后等于r的第i比特
      r_1_i_bit_mask[idxFromSenderAndReceiver(id_, 0)] = 1;
    }
    //计算r2的第i比特共享
    ReplicatedShare<Ring> r_2_i_bit_mask;
    r_2_i_bit_mask.init_zero();
    if(((r2 >> i) & 1ULL) == 1 && id != 1) { //计算r1的第i比特的共享，要确保五个人的共享值复原后等于r的第i比特
      r_2_i_bit_mask[idxFromSenderAndReceiver(id_, 1)] = 1;
    }

    //r1 xor r2 = r1 + r2 - 2r1·r2
    temp = r_1_i_bit_mask + r_2_i_bit_mask;
    auto r_1_times_r_2_mask = compute_prod_mask(r_1_i_bit_mask, r_2_i_bit_mask);
    temp -= r_1_times_r_2_mask.cosnt_mul(2);
    r_1_xor_r_2_mask.push_back(temp);
    
  }

  //然后计算c xor r3
  vector<ReplicatedShare<Ring>> r_1_xor_r_2_xor_r_3_mask;
  if(id != 2) {
    r3 = r_3_mask[idxFromSenderAndReceiver(id_, 2)];
  }
  else {
    r3 = 0;
  }
  
  for(int i = 0; i<N; i++) {
    ReplicatedShare<Ring> temp;
    //计算r3的第i比特共享
    ReplicatedShare<Ring> r_3_i_bit_mask;
    r_3_i_bit_mask.init_zero();
    if(((r3 >> i) & 1ULL) == 1 && id != 2) { //计算r1的第i比特的共享，要确保五个人的共享值复原后等于r的第i比特
      r_3_i_bit_mask[idxFromSenderAndReceiver(id_, 2)] = 1;
    }

    //c xor r2 = c + r2 - 2c·r2
    temp = r_1_xor_r_2_mask[i] + r_3_i_bit_mask;
    auto c_times_r_3_mask = compute_prod_mask(r_1_xor_r_2_mask[i], r_3_i_bit_mask);
    temp -= c_times_r_3_mask.cosnt_mul(2);
    r_1_xor_r_2_xor_r_3_mask.push_back(temp);
  }
  return r_1_xor_r_2_xor_r_3_mask;
}

void OfflineEvaluator::setWireMasks(
    const std::unordered_map<utils::wire_t, int>& input_pid_map) {
  for (const auto& level : circ_.gates_by_level) {
    for (const auto& gate : level) {
      switch (gate->type) {
        case utils::GateType::kInp: {
          auto pregate = std::make_unique<PreprocInput<Ring>>();

          auto pid = input_pid_map.at(gate->out);
          pregate->pid = pid;
          if (pid == id_) {
            randomShareWithParty(id_, rgen_, pregate->mask,
                                 pregate->mask_value); //如果是数据的拥有者，他是可以获得α的累计值的，以此计算β
          } else {
            randomShareWithParty(id_, pid, rgen_, pregate->mask);
          }
          preproc_.gates[gate->out] = std::move(pregate);
          break;
        }

        case utils::GateType::kMul: {
          for(int i = 0; i<5; i++) {
            for(int j = i+1; j<5; j++) {
              if (i == id_ || j == id_) {
                continue;
              }
              else {
                auto alpha_x_y_i_j = 0;
                // alpha_x_y_i_j += 
              }

            }
          }
          preproc_.gates[gate->out] = std::make_unique<PreprocMultGate<Ring>>();
          randomShare(rgen_, preproc_.gates[gate->out]->mask);
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          mult_gates_.push_back(*g);
          break;
        }

        case utils::GateType::kAdd: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          preproc_.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(mask_in1 + mask_in2);
          break;
        }

        case utils::GateType::kSub: {
          const auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          const auto& mask_in1 = preproc_.gates[g->in1]->mask;
          const auto& mask_in2 = preproc_.gates[g->in2]->mask;
          preproc_.gates[gate->out] =
              std::make_unique<PreprocGate<Ring>>(mask_in1 - mask_in2);
          break;
        }

        default:
          break;
      }
    }
  }
}

PreprocCircuit<Ring> OfflineEvaluator::getPreproc() {
  return std::move(preproc_);
}

PreprocCircuit<Ring> OfflineEvaluator::run(const utils::LevelOrderedCircuit& circ,
    const std::unordered_map<utils::wire_t, int>& input_pid_map,
    size_t security_param, int pid, emp::PRG& prg) {
  // setWireMasks(input_pid_map);
  offline_setwire(circ, input_pid_map, security_param, id_, prg);
  return std::move(preproc_);
}

emp::block OfflineEvaluator::commonCoinKey() {
  // Generate random sharing of two 64 bit values and reconstruct to get 128
  // bit key.
  std::vector<ReplicatedShare<Ring>> key_shares(2);
  randomShare(rgen_, key_shares[0]);
  randomShare(rgen_, key_shares[1]);

  OnlineEvaluator oeval(id_, network_, PreprocCircuit<Ring>(),
                        utils::LevelOrderedCircuit(), security_param_, tpool_);
  auto key = oeval.reconstruct(key_shares);
  return emp::makeBlock(key[0], key[1]);
}

PreprocCircuit<Ring> OfflineEvaluator::offline_setwire(
    const utils::LevelOrderedCircuit& circ,
    const std::unordered_map<utils::wire_t, int>& input_pid_map,
    size_t security_param, int pid, emp::PRG& prg) {
  PreprocCircuit<Ring> preproc(circ.num_gates, circ.outputs.size());
  jump_.reset();
  auto msb_circ =
      utils::Circuit<BoolRing>::generatePPAMSB().orderGatesByLevel();
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
              randomShareWithParty(id_, rgen_), //生成α_z的共享
              mask_prod); //然后再把prod 重新share出去，这样下次做乘法，只用线性计算即可
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

          ReplicatedShare<Ring> r;
          ReplicatedShare<Ring> r_trunted_d;
          r.init_zero(); 
          r_trunted_d.init_zero();
          //首先生成r1,r2,r3的共享，按照表格的内容生成
          ReplicatedShare<Ring> r_1_mask = randomShareWithParty_for_trun(id_, rgen_, 0);
          ReplicatedShare<Ring> r_2_mask = randomShareWithParty_for_trun(id_, rgen_, 1);
          ReplicatedShare<Ring> r_3_mask = randomShareWithParty_for_trun(id_, rgen_, 2);
        
          //生成r的每一比特共享
          auto r_1_xor_r_2_xor_r_3_mask = comute_random_r_every_bit_sharing(id_, r_1_mask, r_2_mask, r_3_mask);
          //最后计算随机数r的共享和r^d的共享

          for(int i = 0; i<N; i++) {
            r += r_1_xor_r_2_xor_r_3_mask[i].cosnt_mul((1ULL << i));
            if(i>=FRACTION) {
              r_trunted_d += r_1_xor_r_2_xor_r_3_mask[i].cosnt_mul((1ULL << (i-FRACTION)));
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
          preproc.gates[g->out] = std::make_unique<PreprocTrDotpGate<Ring>>( 
              r_trunted_d, mask_prod_dot,
              r);
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
};  // namespace HoRGod
