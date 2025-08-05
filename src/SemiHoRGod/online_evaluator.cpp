#include "online_evaluator.h"
#include <array>
using namespace SemiHoRGod;
namespace SemiHoRGod {
OnlineEvaluator::OnlineEvaluator(int id,  //复制创建评估器
                                 std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network,
                                 PreprocCircuit<Ring> preproc,
                                 utils::LevelOrderedCircuit circ,
                                 int security_param, int threads, int seed)
    : id_(id),
      security_param_(security_param),
      rgen_(id, seed),
      network_(std::move(network)),
      preproc_(std::move(preproc)),
      circ_(std::move(circ)),
      wires_(circ.num_gates),
      jump_(id),
      msb_circ_(
          utils::Circuit<BoolRing>::generatePPAMSB().orderGatesByLevel()) {
  tpool_ = std::make_shared<ThreadPool>(threads);
}

OnlineEvaluator::OnlineEvaluator(int id,
                                 std::shared_ptr<io::NetIOMP<NUM_PARTIES>> network,
                                 PreprocCircuit<Ring> preproc,
                                 utils::LevelOrderedCircuit circ,
                                 int security_param,
                                 std::shared_ptr<ThreadPool> tpool, int seed)
    : id_(id),
      security_param_(security_param),
      rgen_(id, seed),
      network_(std::move(network)),
      preproc_(std::move(preproc)),
      circ_(std::move(circ)),
      tpool_(std::move(tpool)),
      wires_(circ.num_gates),
      jump_(id) {}

void OnlineEvaluator::setInputs(
    const std::unordered_map<utils::wire_t, Ring>& inputs) { //映射：从wire_id -> values
  // Input gates have depth 0.
  std::vector<Ring> my_betas;
  std::vector<size_t> num_inp_pid(NUM_PARTIES, 0);

  for (auto& g : circ_.gates_by_level[0]) { //每一层都是一个门数组，g是一个门，std::vector<std::vector<gate_ptr_t>> gates_by_level
    if (g->type == utils::GateType::kInp) {
      auto* pre_input = static_cast<PreprocInput<Ring>*>(preproc_.gates[g->out].get()); //g->out 是一个wire_t = size_t类型，64bit无符号整数
      auto pid = pre_input->pid;
      //输出的pre_input->mask是一个RSS秘密共享！
      num_inp_pid[pid]++; //记录某个pid的输入数量
      if (pid == id_) { //只有输入的拥有者，有五个随机值，于是可以计算β
        my_betas.push_back(pre_input->mask_value + inputs.at(g->out)); // β = Σα + x
      }
    }
  }
  
  //下面作为数据的拥有者，需要发送数据给其他4个参与方，发送的数据就是my_betas。同时还需要从另外四个参与方接收数据
  std::vector<std::vector<Ring>> all_recv_betas; // 保存所有 prev_beta 的向量
  // 为每个 recv_pid 初始化 recv_beta
  for (int recv_pid = 0; recv_pid < NUM_PARTIES; ++recv_pid) {
      // 根据 num_inp_pid[recv_pid] 初始化 recv_beta
      std::vector<Ring> recv_beta(num_inp_pid[recv_pid]);
      
      // 可以在这里初始化 recv_beta 的内容（如果需要）
      // 例如：std::fill(recv_beta.begin(), recv_beta.end(), Ring(/*初始化值*/));
      
      // 保存到总向量中
      all_recv_betas.push_back(recv_beta);
  }

  
  for (int recv_pid = 0; recv_pid < NUM_PARTIES; ++recv_pid) {
    std::vector<std::future<void>> res;
    // Send betas to other 4 parties.
    if (recv_pid != id_) {
      if (!my_betas.empty()) {
        res.push_back(tpool_->enqueue([&]() {
          // network_->sendRelative(1, my_betas.data(), //1代表偏移量，如果发送方是2，那么给3发消息
          //                       my_betas.size() * sizeof(Ring));
          network_->send(recv_pid, my_betas.data(), my_betas.size() * sizeof(Ring));
          network_->flush(recv_pid);
        }));
      }
    }

    // Receive betas from previous party.
    if (recv_pid != id_) {
      if (num_inp_pid[recv_pid] != 0) {
        res.push_back(tpool_->enqueue([&]() {
          //send_pid代表谁发来的数据，然后接收保存到prev_betas数组中
          network_->recv(recv_pid, all_recv_betas[recv_pid].data(), all_recv_betas[recv_pid].size() * sizeof(Ring));
        }));
      }
    }
    for (auto& f : res) {
      f.get();
    }
  }

  std::vector<size_t> pid_inp_idx(NUM_PARTIES, 0);
  for (auto& g : circ_.gates_by_level[0]) {
    if (g->type == utils::GateType::kInp) { //如果是输入门，那么设置输出为输入值
      auto* pre_input =
          static_cast<PreprocInput<Ring>*>(preproc_.gates[g->out].get());
      auto pid = pre_input->pid;

      if (pid == id_) {//如果pid是自己，自己就是数据发送方，自然知道β
        wires_[g->out] = my_betas[pid_inp_idx[pid]];
      } 
      else { //如果pid不是自己，那么接收其他人发来的β
        // const auto* values = reinterpret_cast<const Ring*>(
        //     jump_.getValues(pid, pidFromOffset(pid, 1)).data());
        wires_[g->out] = all_recv_betas[pid][pid_inp_idx[pid]];
      }
      pid_inp_idx[pid]++;
    }
  }
  jump_.reset();
}

void OnlineEvaluator::setRandomInputs() {
  // Input gates have depth 0.
  std::random_device rd;       // 真随机数种子（硬件熵源）
  std::mt19937 gen(rd());      // Mersenne Twister 伪随机数引擎
  std::uniform_int_distribution<> dis(0, 100); // 均匀分布 [0, 100]
  for (auto& g : circ_.gates_by_level[0]) {
    if (g->type == utils::GateType::kInp) {
      // rgen_.all().random_data(&wires_[g->out], sizeof(Ring));
      wires_[g->out] = dis(gen);
    }
  }
}

std::array<std::vector<Ring>, NUM_RSS> OnlineEvaluator::msbEvaluate(
    const std::vector<utils::FIn1Gate>& msb_gates) { //msb门全部在这个向量里，就是直接push进去的
  auto num_msb_gates = msb_gates.size();
  std::vector<preprocg_ptr_t<BoolRing>*> vpreproc(num_msb_gates);

  // Iterate through preproc_ and extract info of msb gates.
  std::vector<utils::wire_t> win(num_msb_gates);
  for (size_t i = 0; i < num_msb_gates; ++i) {
    auto* pre_msb = static_cast<PreprocMsbGate<Ring>*>(
        preproc_.gates[msb_gates[i].out].get());
    //每个MSB门预处理的数据，都放在vpreproc
    vpreproc[i] = pre_msb->msb_gates.data(); //.data()是把指针返回，对应vpreproc[i]需要指针
  }

  BoolEvaluator bool_eval(id_, vpreproc, msb_circ_);

  // Set the inputs.
  for (size_t i = 0; i < num_msb_gates; ++i) {
    auto val = wires_[msb_gates[i].in];

    auto val_bits = bitDecompose(val);
    for (size_t j = 0; j < msb_circ_.gates_by_level[0].size(); ++j) { //第零层全是输入，只处理第0层
      const auto& gate = msb_circ_.gates_by_level[0][j];

      if (gate->type == utils::GateType::kInp) {
        bool_eval.vwires[i][gate->out] = 0;
        if (gate->out > 63) {
          bool_eval.vwires[i][gate->out] = val_bits[j - 64];
        }
      }
    }
  }

  bool_eval.evaluateAllLevels(*network_, jump_, *tpool_);
  auto output_shares = bool_eval.getOutputShares(*network_, jump_, *tpool_);//这里直接重构得到了比较结果了

  std::vector<Ring> output_share_val(num_msb_gates);
  for (size_t i = 0; i < num_msb_gates; ++i) {
    if (output_shares[i][0].val()) {
      output_share_val[i] = 1;
    } else {
      output_share_val[i] = 0;
    }
    // std::cout<<"参与方"<<id_<<"没有B2A前的比较结果:"<<output_share_val[i]<<std::endl;
  }

  // Bit to A.
  std::array<std::vector<Ring>, NUM_RSS> outputs;
  for (size_t i = 0; i < num_msb_gates; ++i) {
    auto* pre_msb = static_cast<PreprocMsbGate<Ring>*>(
        preproc_.gates[msb_gates[i].out].get());
    auto beta_w = pre_msb->mask_w + (pre_msb->mask_msb * output_share_val[i]); //output_share_val[i]是结果，
    beta_w *= static_cast<Ring>(-2);
    // beta_w.add(output_share_val[i], id_);
    msb_temp_value_ = output_share_val[i];

    for (int j = 0; j < NUM_RSS; ++j) {
      outputs[j].push_back(beta_w[j]);
    }
  }
  // std::cout<<"参与方"<<id_<<"进行B2A后的比较共享值为:"<<endl;
  // for (int j = 0; j < NUM_RSS; ++j) {
  //   std::cout<<"参与方"<<id_<<"持有的第"<<j<<"个元素为:"<<outputs[j][0]<<endl;
  // }
  // std::cout<<std::endl;
  return outputs;
}

std::vector<Ring> OnlineEvaluator::elementwise_sum(const std::array<std::vector<Ring>, NUM_RSS>& recon_shares, int i, int j, int k) {
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

std::vector<Ring> OnlineEvaluator::reconstruct(
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

void OnlineEvaluator::evaluateGatesAtDepth(size_t depth) {
  std::array<std::vector<Ring>, NUM_RSS> recon_shares;
  std::vector<utils::FIn1Gate> relu_gates;
  std::vector<utils::FIn1Gate> msb_gates;

  for (auto& gate : circ_.gates_by_level[depth]) {
    switch (gate->type) {
      case utils::GateType::kMul: {
        auto* g = static_cast<utils::FIn2Gate*>(gate.get());
        auto& m_in1 = preproc_.gates[g->in1]->mask;
        auto& m_in2 = preproc_.gates[g->in2]->mask;
        auto* pre_out =
            static_cast<PreprocMultGate<Ring>*>(preproc_.gates[g->out].get());

        auto rec_share = pre_out->mask + pre_out->mask_prod -
                         m_in1 * wires_[g->in2] - m_in2 * wires_[g->in1]; //wires_[g->in1]和wires_[g->in2]是两个β
        // rec_share.add(wires_[g->in1] * wires_[g->in2], id_);

        for (int i = 0; i < NUM_RSS; ++i) {
          recon_shares[i].push_back(rec_share[i]);
        }
        break;
      }

      case utils::GateType::kCmp: {
        auto* g = static_cast<utils::FIn1Gate*>(gate.get());
        
        auto* pre_out =
            static_cast<PreprocCmpGate<Ring>*>(preproc_.gates[g->out].get());
        auto& m_in1 = preproc_.gates[g->in]->mask; //mask代表秘密共享形式下的四个值，即四个alpha
        auto& m_in2 = pre_out->mask_mu_1; //mask代表秘密共享形式下的四个值，即四个alpha
        auto& beta_mu_1 = pre_out->beta_mu_1;
        //pre_out->mask代表α_z，pre_out->mask_prod代表alpha_{xy}
        auto rec_share = pre_out->prev_mask + pre_out->mask_prod -
                         m_in1 * beta_mu_1 - m_in2 * wires_[g->in]; //m_in1代表(x-y)的[]共享，beta_mu_1代表mu_1的β，m_in2代表mu_1的共享，wires_[g->in]代表(x-y)的β
        // rec_share.add(wires_[g->in1] * wires_[g->in2], id_);

        for (int i = 0; i < NUM_RSS; ++i) {
          recon_shares[i].push_back(rec_share[i]);
        }
        break;
      }

      case utils::GateType::kDotprod: {
        auto* g = static_cast<utils::SIMDGate*>(gate.get());
        auto* pre_out =
            static_cast<PreprocDotpGate<Ring>*>(preproc_.gates[g->out].get());

        auto rec_share = pre_out->mask + pre_out->mask_prod; // [α_z] +  [x]，x代表最终计算结果
        for (size_t i = 0; i < g->in1.size(); i++) {
          auto win1 = g->in1[i];
          auto win2 = g->in2[i];
          auto& m_in1 = preproc_.gates[win1]->mask;
          auto& m_in2 = preproc_.gates[win2]->mask;

          rec_share -= m_in1 * wires_[win2] + m_in2 * wires_[win1]; //对应步骤-Σ^d_1 \beta_{x_t}[\alpha_{y_t}] - Σ^d_1 \beta_{y_t}[\alpha_{x_t}]
          // rec_share.add(wires_[win1] * wires_[win2], id_);
        }

        for (int i = 0; i < NUM_RSS; i++) {
          recon_shares[i].push_back(rec_share[i]);
        }
        break;
      }

      case utils::GateType::kTrdotp: {
        auto* g = static_cast<utils::SIMDGate*>(gate.get());
        auto* pre_out =
            static_cast<PreprocTrDotpGate<Ring>*>(preproc_.gates[g->out].get());

        auto rec_share = pre_out->mask_prod + pre_out->mask_d;
        for (size_t i = 0; i < g->in1.size(); i++) {
          auto win1 = g->in1[i];
          auto win2 = g->in2[i];
          auto& m_in1 = preproc_.gates[win1]->mask;
          auto& m_in2 = preproc_.gates[win2]->mask;

          rec_share -= (m_in1 * wires_[win2] + m_in2 * wires_[win1]);
          // rec_share.add(wires_[win1] * wires_[win2], id_);
        }

        for (int i = 0; i < NUM_RSS; i++) {
          recon_shares[i].push_back(rec_share[i]);
        }

        break;
      }

      case utils::GateType::kRelu: {
        auto* g = static_cast<utils::FIn1Gate*>(gate.get());
        
        auto* pre_out =
            static_cast<PreprocReluGate<Ring>*>(preproc_.gates[g->out].get());
        auto& m_in1 = preproc_.gates[g->in]->mask; //mask代表秘密共享形式下的四个值，即四个alpha
        auto& m_in2 = pre_out->mask_mu_1; //mask代表秘密共享形式下的四个值，即四个alpha
        auto& beta_mu_1 = pre_out->beta_mu_1;
        //pre_out->mask代表α_z，pre_out->mask_prod代表alpha_{xy}
        auto rec_share = pre_out->prev_mask + pre_out->mask_prod -
                         m_in1 * beta_mu_1 - m_in2 * wires_[g->in]; //m_in1代表(x-y)的[]共享，beta_mu_1代表mu_1的β，m_in2代表mu_1的共享，wires_[g->in]代表(x-y)的β
        // rec_share.add(wires_[g->in1] * wires_[g->in2], id_);

        for (int i = 0; i < NUM_RSS; ++i) {
          recon_shares[i].push_back(rec_share[i]);
        }
        break;
      }

      case utils::GateType::kMsb: {
        auto* g = static_cast<utils::FIn1Gate*>(gate.get());
        msb_gates.push_back(*g);
        break;
      }

      default:
        break;
    }
  }

  size_t non_relu_recon = recon_shares[0].size();
  // if (!relu_gates.empty()) {
  //   auto shares = reluEvaluate(relu_gates);
  //   for (size_t i = 0; i < NUM_RSS; ++i) {
  //     recon_shares[i].insert(recon_shares[i].end(), shares[i].begin(),
  //                            shares[i].end()); //把关于relu的重构，直接放在后面，所以访问重构的数据，需要加上non_relu_recon的索引
  //   }
  // }

  if (!msb_gates.empty()) {
    auto shares = msbEvaluate(msb_gates);
    for (size_t i = 0; i < NUM_RSS; ++i) {
      recon_shares[i].insert(recon_shares[i].end(), shares[i].begin(),
                             shares[i].end());
    }
  }

  auto vres = reconstruct(recon_shares); //重构出beta_z

  size_t idx = 0;
  size_t relu_idx = 0;
  size_t msb_idx = 0;
  for (auto& gate : circ_.gates_by_level[depth]) {
    switch (gate->type) {
      case utils::GateType::kAdd: {
        auto* g = static_cast<utils::FIn2Gate*>(gate.get());
        wires_[g->out] = wires_[g->in1] + wires_[g->in2];//这里wires_存的是beta
        break;
      }

      case utils::GateType::kSub: {
        auto* g = static_cast<utils::FIn2Gate*>(gate.get());
        wires_[g->out] = wires_[g->in1] - wires_[g->in2];
        break;
      }

      case utils::GateType::kMul: {
        auto* g = static_cast<utils::FIn2Gate*>(gate.get());
        wires_[gate->out] = vres[idx++] + wires_[g->in1] * wires_[g->in2];
        break;
      }

      case utils::GateType::kCmp: {
        auto* g = static_cast<utils::FIn1Gate*>(gate.get());
        auto* pre_out =
            static_cast<PreprocCmpGate<Ring>*>(preproc_.gates[g->out].get());
        auto& beta_mu_1 = pre_out->beta_mu_1;
        //上面已经重构了一次，得到了beta_z，直接加到这上面即可，但是还需要一次重构来获取Z的值
        wires_[gate->out] = vres[idx] + wires_[g->in] * beta_mu_1; //for multiplication

        auto& beta_mu_2 = pre_out->beta_mu_2;
        wires_[gate->out] += beta_mu_2; //for addition
        // preproc_.gates[gate->out]->mask = preproc_.gates[gate->out]->mask + pre_out->mask_mu_2; //for addition

        //下面进行重构，获取z的值，判断比较结果。
        std::array<std::vector<Ring>, NUM_RSS> recon_shares_for_z;
        for (int i = 0; i < NUM_RSS; ++i) {
          recon_shares_for_z[i].push_back(preproc_.gates[gate->out]->mask[i]);
        }
        auto sum_z = reconstruct(recon_shares_for_z); //为了重构出z
        auto z = wires_[gate->out] - sum_z[0];
        std::vector<BoolRing> bin = bitDecompose(z);
        if (bin[BITS_GAMMA+BITS_BETA-1].val()) { //最高位是1，那么是负数
          wires_[gate->out] = sum_z[0] + CMP_lESS_RESULT;
        }
        else {
          wires_[gate->out] = sum_z[0] + CMP_GREATER_RESULT;
        }

        idx++;
        break;
      }

      case utils::GateType::kDotprod: {
        auto* g = static_cast<utils::SIMDGate*>(gate.get());

        Ring sum_beta = 0;
        for (size_t i = 0; i < g->in1.size(); i++) {
          auto win1 = g->in1[i];
          auto win2 = g->in2[i];
          sum_beta += wires_[win1] * wires_[win2];
        }
        wires_[gate->out] = vres[idx++] + sum_beta;
        break;
      }

      case utils::GateType::kTrdotp: {
        auto* g = static_cast<utils::SIMDGate*>(gate.get());

        Ring sum_beta = 0;
        for (size_t i = 0; i < g->in1.size(); i++) {
          auto win1 = g->in1[i];
          auto win2 = g->in2[i];
          sum_beta += wires_[win1] * wires_[win2];
        }
        wires_[gate->out] = (vres[idx++] + sum_beta) >> FRACTION ;
        break;
      }

      case utils::GateType::kConstAdd: {
        auto* g = static_cast<utils::ConstOpGate<Ring>*>(gate.get());
        wires_[g->out] = wires_[g->in] + g->cval;  //只需要beta加即可,alpha不用加
        break;
      }

      case utils::GateType::kConstMul: {
        auto* g = static_cast<utils::ConstOpGate<Ring>*>(gate.get());
        wires_[g->out] = wires_[g->in] * g->cval;
        break;
      }

      case utils::GateType::kRelu: {
        auto* g = static_cast<utils::FIn1Gate*>(gate.get());
        auto* pre_out =
            static_cast<PreprocReluGate<Ring>*>(preproc_.gates[g->out].get());
        auto& beta_mu_1 = pre_out->beta_mu_1;
        //上面已经重构了一次，得到了beta_z，直接加到这上面即可，但是还需要一次重构来获取Z的值
        wires_[gate->out] = vres[idx] + wires_[g->in] * beta_mu_1; //for multiplication

        auto& beta_mu_2 = pre_out->beta_mu_2;
        wires_[gate->out] += beta_mu_2; //for addition
        // preproc_.gates[gate->out]->mask = preproc_.gates[gate->out]->mask + pre_out->mask_mu_2; //for addition

        //下面进行重构，获取z的值，判断比较结果。
        std::array<std::vector<Ring>, NUM_RSS> recon_shares_for_z;
        for (int i = 0; i < NUM_RSS; ++i) {
          recon_shares_for_z[i].push_back(preproc_.gates[gate->out]->mask[i]);
        }
        auto sum_z = reconstruct(recon_shares_for_z); //为了重构出z
        auto z = wires_[gate->out] - sum_z[0];
        std::vector<BoolRing> bin = bitDecompose(z);
        if (bin[BITS_GAMMA+BITS_BETA-1].val()) { //最高位是1，那么是负数
          wires_[gate->out] = sum_z[0] + 0;
        }
        else {
          wires_[gate->out] = sum_z[0] + 1;
        }

        auto& m_in1_mul = preproc_.gates[g->in]->mask; //mask代表秘密共享形式下的四个值，即四个alpha
        auto& m_in2_mul = preproc_.gates[gate->out]->mask; //mask代表秘密共享形式下的四个值，即四个alpha
        
        auto rec_share_for_mul = pre_out->mask + pre_out->mask_prod2 -
                         m_in1_mul * wires_[g->out] - m_in2_mul * wires_[g->in]; //wires_[g->in1]和wires_[g->in2]是两个β
        std::array<std::vector<Ring>, NUM_RSS> recon_shares_for_mul;
        for (int i = 0; i < NUM_RSS; ++i) {
          recon_shares_for_mul[i].push_back(rec_share_for_mul[i]);
        }

        auto mul_result = reconstruct(recon_shares_for_mul);
        wires_[gate->out] = mul_result[0] + wires_[g->out] * wires_[g->in];
        // preproc_.gates[gate->out]->mask = pre_out->mask_for_mul;
        idx++;
        break;
      }

      case utils::GateType::kMsb: {
        if(msb_temp_value_) {
          wires_[gate->out] = vres[non_relu_recon + relu_idx + msb_idx] + 2;
        }
        else {
          wires_[gate->out] = vres[non_relu_recon + relu_idx + msb_idx] - 1;
        }
        
        // std::cout<<"参与方"<<id_<<"重构出的β为"<<wires_[gate->out]<<std::endl;
        msb_idx++;
        break;
      }

      default:
        break;
    }
  }
}

std::vector<Ring> OnlineEvaluator::reconstruct(
    const std::vector<ReplicatedShare<Ring>>& shares) {
  std::array<std::vector<Ring>, NUM_RSS> recon_shares;
  for (const auto& s : shares) {
    for (size_t i = 0; i < NUM_RSS; ++i) {
      recon_shares[i].push_back(s[i]);
    }
  }
  return reconstruct(recon_shares);
}

std::vector<Ring> OnlineEvaluator::getOutputs() {
  std::vector<Ring> outvals(circ_.outputs.size());
  if (circ_.outputs.empty()) {
    return outvals;
  }

  std::vector<ReplicatedShare<Ring>> shares;
  for (size_t i = 0; i < outvals.size(); ++i) {
    auto wout = circ_.outputs[i];
    // outvals[i] = wires_[wout] - preproc_.gates[wout]->mask.sum(); //β - Σα
    
    shares.push_back(preproc_.gates[wout]->mask);
  }
  auto sum = reconstruct(shares);
  // std::cout<<"参与方"<<id_<<"重构出的sum为"<<sum[0]<<std::endl;
  for (size_t i = 0; i < outvals.size(); ++i) {
    auto wout = circ_.outputs[i];
    outvals[i] = wires_[wout] - sum[i]; //β - Σα
  }

  return outvals;
}

std::vector<Ring> OnlineEvaluator::evaluateCircuit(
    const std::unordered_map<utils::wire_t, Ring>& inputs) {
  setInputs(inputs);
  for (size_t i = 0; i < circ_.gates_by_level.size(); ++i) {
    evaluateGatesAtDepth(i);
  }
  return getOutputs();
}

BoolEvaluator::BoolEvaluator(int my_id,
                             std::vector<preprocg_ptr_t<BoolRing>*> vpreproc,
                             utils::LevelOrderedCircuit circ)
    : id(my_id),
      vwires(vpreproc.size(), std::vector<BoolRing>(circ.num_gates)),
      vpreproc(std::move(vpreproc)),
      circ(std::move(circ)) {}

std::vector<BoolRing> BoolEvaluator::reconstruct(
    int id, const std::array<std::vector<BoolRing>, NUM_RSS>& recon_shares,
    io::NetIOMP<NUM_PARTIES>& network, ImprovedJmp& jump, ThreadPool& tpool) {
  size_t num = recon_shares[0].size(); //看看有多少个bit

  std::array<std::vector<uint8_t>, NUM_RSS> packed_recon_shares;
  for (int i = 0; i < NUM_RSS; ++i) {
    //将 BoolRing 类型的数组按位打包成 uint8_t（字节）数组，方便ijmp协议传输
    packed_recon_shares[i] = BoolRing::pack(recon_shares[i].data(), num);
  }
  size_t nbytes = sizeof(uint8_t) * packed_recon_shares[0].size();

  if (nbytes == 0) {
    return {};
  }

  std::vector<BoolRing> vres;
  jump.jumpUpdate(id, pidFromOffset(id, 1), pidFromOffset(id, 2), pidFromOffset(id, -1), nbytes, 
                  packed_recon_shares[idxFromSenderAndReceiver(id, pidFromOffset(id, -1))].data());
  jump.jumpUpdate(pidFromOffset(id, -1), id, pidFromOffset(id, 1), pidFromOffset(id, -2), nbytes, 
                  packed_recon_shares[idxFromSenderAndReceiver(id, pidFromOffset(id, -2))].data());
  jump.jumpUpdate(pidFromOffset(id, -2), pidFromOffset(id, -1), id, pidFromOffset(id, -3), nbytes, 
                  packed_recon_shares[idxFromSenderAndReceiver(id, pidFromOffset(id, -3))].data());
  jump.jumpUpdate(pidFromOffset(id, 1), pidFromOffset(id, 2), pidFromOffset(id, 3), id, nbytes, 
                  packed_recon_shares[idxFromSenderAndReceiver(id, pidFromOffset(id, -3))].data());
  jump.communicate(network, tpool);

  std::vector<uint8_t> vtemp(packed_recon_shares[0].size());
  const auto miss_values = jump.getValues(pidFromOffset(id, 1), pidFromOffset(id, 2), pidFromOffset(id, 3)).data();       
  // std::copy(miss_values, miss_values + num, vres.begin());
  for (size_t i = 0; i<vtemp.size(); i++) {
    vtemp[i] = miss_values[i] ^ packed_recon_shares[0][i] ^ packed_recon_shares[1][i] ^ packed_recon_shares[2][i] ^ packed_recon_shares[3][i];
  }
  vres = BoolRing::unpack(vtemp.data(), num);
  jump.reset();
  return vres;
}

void BoolEvaluator::evaluateGatesAtDepth(size_t depth, io::NetIOMP<NUM_PARTIES>& network,
                                         ImprovedJmp& jump,
                                         ThreadPool& tpool) {
  std::array<std::vector<BoolRing>, NUM_RSS> recon_shares;
  for (size_t i = 0; i < vwires.size(); ++i) {
    const auto& preproc = vpreproc[i];
    auto& wires = vwires[i];

    for (auto& gate : circ.gates_by_level[depth]) {
      switch (gate->type) {
        case utils::GateType::kMul: {
          auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          auto& m_in1 = preproc[g->in1]->mask;
          auto& m_in2 = preproc[g->in2]->mask;
          auto* pre_out =
              static_cast<PreprocMultGate<BoolRing>*>(preproc[g->out].get());

          auto rec_share = pre_out->mask + pre_out->mask_prod -
                           m_in1 * wires[g->in2] - m_in2 * wires[g->in1]; //wires[g->in1]和wires[g->in2]是两个β
          // rec_share.add(wires[g->in1] * wires[g->in2], id);

          for (int i = 0; i < NUM_RSS; ++i) {
            recon_shares[i].push_back(rec_share[i]);
          }
          break;
        }

        default:
          break;
      }
    }
  }

  auto vres = reconstruct(id, recon_shares, network, jump, tpool);

  // Update mult gate values.
  size_t idx = 0;
  for (auto& wires : vwires) {
    for (auto& gate : circ.gates_by_level[depth]) {
      switch (gate->type) {
        case utils::GateType::kAdd: {
          auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          wires[g->out] = wires[g->in1] + wires[g->in2];
          break;
        }

        case utils::GateType::kSub: {
          auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          wires[g->out] = wires[g->in1] - wires[g->in2];
          break;
        }

        case utils::GateType::kMul: {
          auto* g = static_cast<utils::FIn2Gate*>(gate.get());
          wires[gate->out] = vres[idx++] + wires[g->in1] * wires[g->in2];
          break;
        }

        case utils::GateType::kConstAdd: {
          auto* g = static_cast<utils::ConstOpGate<BoolRing>*>(gate.get());
          wires[g->out] = wires[g->in] + g->cval;
          break;
        }

        case utils::GateType::kConstMul: {
          auto* g = static_cast<utils::ConstOpGate<BoolRing>*>(gate.get());
          wires[g->out] = wires[g->in] * g->cval;
          break;
        }

        default:
          break;
      }
    }
  }
}

void BoolEvaluator::evaluateAllLevels(io::NetIOMP<NUM_PARTIES>& network,
                                      ImprovedJmp& jump, ThreadPool& tpool) {
  for (size_t i = 0; i < circ.gates_by_level.size(); ++i) {
    evaluateGatesAtDepth(i, network, jump, tpool);
  }
}

std::vector<std::vector<BoolRing>> BoolEvaluator::getOutputShares(io::NetIOMP<NUM_PARTIES>& network,
                                      ImprovedJmp& jump, ThreadPool& tpool) {
  std::vector<std::vector<BoolRing>> outputs(vwires.size(), std::vector<BoolRing>(circ.outputs.size()));
  //circ是MSB形成的bool环
  for (size_t i = 0; i < vwires.size(); ++i) {
    auto& preproc = vpreproc[i];
    const auto& wires = vwires[i];

    std::vector<ReplicatedShare<BoolRing>> shares;
    for (size_t j = 0; j < circ.outputs.size(); ++j) {
      auto wout = circ.outputs[j];
      shares.push_back(preproc[wout]->mask);
    }

    std::array<std::vector<BoolRing>, NUM_RSS> recon_shares;
    for (const auto& s : shares) {
      for (size_t i = 0; i < NUM_RSS; ++i) {
        recon_shares[i].push_back(s[i]);
      }
    }
    auto sum = reconstruct(id, recon_shares, network, jump, tpool);
    for (size_t j = 0; j < circ.outputs.size(); ++j) {
      auto wout = circ.outputs[j];
      outputs[i][j] = wires[wout] - sum[j];
    }
  }
  return outputs;
}
};  // namespace SemiHoRGod
