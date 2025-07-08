#include "offline_evaluator.h"

#include <NTL/BasicThreadPool.h>
#include <NTL/vec_ZZ_pE.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>

#include "helpers.h"
#include "jump_provider.h"
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
      ot_(4),
      zk_exp_val_{},
      jump_(my_id) {
  tpool_ = std::make_shared<ThreadPool>(threads);

  // for (int pid1 = 0; pid1 < 5; ++pid1) {
  //   for (int pid2 = pid1 + 1; pid2 < 5; ++pid2) {
  //     if (pid1 != id_ && pid2 != id_) {
  //       continue;
  //     }

  //     int other_id = id_ ^ pid1 ^ pid2;
  //     bool flag = (id_ < other_id);
  //     ot_[other_id] = std::make_unique<OTProvider>(
  //         id_, other_id, network_ot_->get(other_id, flag),
  //         network_ot_->get(other_id, !flag));
  //   }
  // }

  // ZZ_p_ctx_.save();
  // ZZ_pE_ctx_.save();

  // // Used below for interpolation.
  // // 'x_interp_' is unit in Z_{2^k}[X]/f(x).
  // NTL::ZZ_pX temp(NTL::INIT_MONO, 1);
  // NTL::conv(x_interp_, temp);

  // auto& lgc = lagrange_coeff_d3_;
  // lgc[0] = 1 / x_interp_;
  // lgc[1] = 1 / (1 - x_interp_);
  // lgc[2] = -1 * lgc[0] * lgc[1];
}

void OfflineEvaluator::randomShare(RandGenPool& rgen,
                                   ReplicatedShare<Ring>& share) {
  rgen.getRelative(1).random_data(&share[0], sizeof(Ring));
  rgen.getRelative(2).random_data(&share[1], sizeof(Ring));
  rgen.getRelative(3).random_data(&share[2], sizeof(Ring));
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

PreprocCircuit<Ring> OfflineEvaluator::run(
    const std::unordered_map<utils::wire_t, int>& input_pid_map) {
  setWireMasks(input_pid_map);
  // auto c_terms_f = tpool_->enqueue([&]() { computeCCrossTerms(); });

  // computeABCrossTerms();
  // distributedZKP();
  // c_terms_f.get();
  // combineCrossTerms();
  // computeOutputCommitments();

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

void OfflineEvaluator::distributedZKP() {
  auto& inputs = zk_prove_;

  if (inputs.length() == 0) {
    return;
  }

  uint64_t num = inputs.length() / 7;
  bool is_pow_2 = (num & (num - 1)) == 0;

  if (!is_pow_2) {
    auto log = static_cast<uint64_t>(std::ceil(std::log2(num)));
    auto new_len = 1UL << log;
    new_len *= 7;
    inputs.SetLength(new_len);
    zk_verify_[0].SetLength(new_len);
    zk_verify_[1].SetLength(new_len);
    zk_verify_[2].SetLength(new_len);
  }

  // Run recursive DZK.
  // Each loop iteration corresponds to one round of DZK.
  NTL::Vec<NTL::ZZ_pE> poly_f_interp;
  NTL::Mat<NTL::ZZ_pE> sub_poly_p;
  while (inputs.length() > 7) {
    long num_poly = inputs.length() / 2;

    // Interpolate f_i(x).
    poly_f_interp.SetLength(num_poly);

    NTL_EXEC_RANGE(num_poly, first, last);
    ZZ_p_ctx_.restore();
    ZZ_pE_ctx_.restore();

    for (long i = first; i < last; ++i) {
      // Let inputs[i] be evaluation at x = 0.
      // Let inputs[i + num_poly] be evaluation at x = 1.
      // Interpolate to have poly_f_interp[i] be evaluation at x = x_interp_.
      poly_f_interp[i] = inputs[i];
      poly_f_interp[i] += x_interp_ * (inputs[i + num_poly] - inputs[i]);
    }
    NTL_EXEC_RANGE_END;

    // Evaluate circuit on f_i(x).
    auto num_eval = num_poly / 7;
    sub_poly_p.SetDims(num_eval, 3);

    NTL_EXEC_RANGE(num_eval, first, last);
    ZZ_p_ctx_.restore();
    ZZ_pE_ctx_.restore();

    for (long t = first; t < last; ++t) {
      long i = t * 7;
      for (long j = 0; j < 3; ++j) {
        auto base = i + j * num_poly;
        sub_poly_p[t][j] = 0;

        // Evaluate ciruit by summing up the cross-terms.
        for (long p1 = 0; p1 < 3; ++p1) {
          for (long p2 = p1 + 1; p2 < 3; ++p2) {
            if (j == 2) {
              sub_poly_p[t][j] +=
                  poly_f_interp[i + 2 * p1] * poly_f_interp[i + 2 * p2 + 1];
              sub_poly_p[t][j] +=
                  poly_f_interp[i + 2 * p1 + 1] * poly_f_interp[i + 2 * p2];
            } else {
              sub_poly_p[t][j] +=
                  inputs[base + 2 * p1] * inputs[base + 2 * p2 + 1];
              sub_poly_p[t][j] +=
                  inputs[base + 2 * p1 + 1] * inputs[base + 2 * p2];
            }
          }
        }

        if (j == 2) {
          sub_poly_p[t][j] -= poly_f_interp[i + 6];
        } else {
          sub_poly_p[t][j] -= inputs[base + 6];
        }
      }
    }
    NTL_EXEC_RANGE_END;

    // Compute p(x).
    std::mutex poly_p_mutex;
    NTL::Vec<NTL::ZZ_pE> poly_p;
    poly_p.SetLength(3);

    NTL_EXEC_RANGE(num_eval, first, last);
    ZZ_p_ctx_.restore();
    ZZ_pE_ctx_.restore();

    NTL::Vec<NTL::ZZ_pE> acc;
    acc.SetLength(3);
    for (long i = first; i < last; ++i) {
      for (long j = 0; j < 3; ++j) {
        acc[j] += sub_poly_p[i][j];
      }
    }

    std::lock_guard<std::mutex> guard(poly_p_mutex);
    for (long i = 0; i < 3; ++i) {
      poly_p[i] += acc[i];
    }
    NTL_EXEC_RANGE_END;

    NTL::Mat<NTL::ZZ_pE> send_polyp;
    send_polyp.SetDims(3, 3);
    NTL::Mat<NTL::ZZ_pE> recv_polyp;
    recv_polyp.SetDims(3, 3);

    for (int p = 0; p < 5; ++p) {
      if (p == id_) {
        for (int i = 1; i < 3; ++i) {
          for (long j = 0; j < 3; ++j) {
            randomizeZZpE(rgen_.getRelative(i + 1), send_polyp[i][j]);
          }
        }
        send_polyp[0] = poly_p - send_polyp[1] - send_polyp[2];

        zk_res_.push_back(tpool_->enqueue([&]() {
          ZZ_p_ctx_.restore();
          ZZ_pE_ctx_.restore();
          sendZZpE(network_->getSendChannel(pidFromOffset(id_, 1)),
                   send_polyp[0].elts(), 3);
        }));
      } else if (id_ == pidFromOffset(p, 1)) {
        zk_res_.push_back(tpool_->enqueue([&, p]() {
          ZZ_p_ctx_.restore();
          ZZ_pE_ctx_.restore();
          receiveZZpE(network_->getRecvChannel(p), recv_polyp[2].elts(), 3);
        }));
      } else {
        for (int j = 0; j < 3; ++j) {
          randomizeZZpE(rgen_.get(p), recv_polyp[offsetFromPid(id_, p) - 1][j]);
        }
      }
    }

    for (auto& f : zk_res_) {
      f.get();
    }
    zk_res_.clear();

    NTL::ZZ_pE r;
    zk_res_.push_back(tpool_->enqueue([&]() {
      ZZ_p_ctx_.restore();
      ZZ_pE_ctx_.restore();

      receiveZZpE(network_->getRecvChannel(pidFromOffset(id_, 1)), &r, 1);
    }));

    for (int p = 0; p < 5; ++p) {
      if (p != id_) {
        zkVerifyRecursiveIter(p, recv_polyp[offsetFromPid(id_, p) - 1]);
      }
    }

    for (auto& f : zk_res_) {
      f.get();
    }
    zk_res_.clear();

    // Compute f_i(r) as inputs for next round.
    NTL::Vec<NTL::ZZ_pE> next_inputs;
    next_inputs.SetLength(num_poly);
    NTL_EXEC_RANGE(num_poly, first, last);
    ZZ_p_ctx_.restore();
    ZZ_pE_ctx_.restore();

    for (long i = first; i < last; ++i) {
      next_inputs[i] = inputs[i];
      next_inputs[i] += r * (inputs[i + num_poly] - inputs[i]);
    }
    NTL_EXEC_RANGE_END;

    inputs.move(next_inputs);
  }

  // Base case DZK.
  NTL::Vec<NTL::ZZ_pE> omega;
  // Reserve space for later use.
  omega.SetLength(10);
  NTL::random(omega, 7);

  // Interpolate f_i(x).
  poly_f_interp.SetLength(7);
  for (long i = 0; i < 7; ++i) {
    poly_f_interp[i] = omega[i];
    poly_f_interp[i] += x_interp_ * (inputs[i] - omega[i]);
  }

  // Compute p(x).
  std::mutex poly_p_mutex;
  NTL::Vec<NTL::ZZ_pE> poly_p;
  poly_p.SetLength(3);

  for (long p1 = 0; p1 < 3; ++p1) {
    for (long p2 = p1 + 1; p2 < 3; ++p2) {
      poly_p[0] += omega[2 * p1] * omega[2 * p2 + 1];
      poly_p[0] += omega[2 * p1 + 1] * omega[2 * p2];

      poly_p[1] += inputs[2 * p1] * inputs[2 * p2 + 1];
      poly_p[1] += inputs[2 * p1 + 1] * inputs[2 * p2];

      poly_p[2] += poly_f_interp[2 * p1] * poly_f_interp[2 * p2 + 1];
      poly_p[2] += poly_f_interp[2 * p1 + 1] * poly_f_interp[2 * p2];
    }
  }
  poly_p[0] -= omega[6];
  poly_p[1] -= inputs[6];
  poly_p[2] -= poly_f_interp[6];

  // Compute and communicate additive sharing of p(x).
  NTL::Mat<NTL::ZZ_pE> recv_pi_shares;
  recv_pi_shares.SetDims(3, 10);
  NTL::Mat<NTL::ZZ_pE> pi;
  pi.SetDims(3, 10);
  for (int p = 0; p < 5; ++p) {
    if (p == id_) {
      for (int i = 1; i < 3; ++i) {
        for (long j = 0; j < 10; ++j) {
          randomizeZZpE(rgen_.getRelative(i + 1), pi[i][j]);
        }
      }
      omega.append(poly_p);
      pi[0] = omega - pi[1] - pi[2];

      zk_res_.push_back(tpool_->enqueue([&]() {
        ZZ_p_ctx_.restore();
        ZZ_pE_ctx_.restore();
        sendZZpE(network_->getSendChannel(pidFromOffset(id_, 1)), pi[0].elts(),
                 10);
      }));
    } else if (id_ == pidFromOffset(p, 1)) {
      zk_res_.push_back(tpool_->enqueue([&, p]() {
        ZZ_p_ctx_.restore();
        ZZ_pE_ctx_.restore();
        receiveZZpE(network_->getRecvChannel(p), recv_pi_shares[2].elts(), 10);
      }));
    } else {
      for (int j = 0; j < 10; ++j) {
        randomizeZZpE(rgen_.get(p),
                      recv_pi_shares[offsetFromPid(id_, p) - 1][j]);
      }
    }
  }

  for (auto& f : zk_res_) {
    f.get();
  }
  zk_res_.clear();

  auto cc_key2 = commonCoinKey();

  for (int i = 0; i < 5; ++i) {
    if (i != id_) {
      zkVerifyBase(i, cc_key2, recv_pi_shares[offsetFromPid(id_, i) - 1]);
    }
  }

  for (auto& f : zk_res_) {
    f.get();
  }
  zk_res_.clear();

  auto& shares = opp_check_shares_;
  shares[0] += shares[1] + shares[2];
  auto& recon = shares[0];

  NTL::ZZ_pE recon_pr;
  for (long p1 = 0; p1 < 3; ++p1) {
    for (long p2 = p1 + 1; p2 < 3; ++p2) {
      recon_pr += recon[2 * p1] * recon[2 * p2 + 1];
      recon_pr += recon[2 * p1 + 1] * recon[2 * p2];
    }
  }
  recon_pr -= recon[6];

  if (!NTL::IsZero(recon_pr - recon[7]) || !NTL::IsZero(recon[8]) ||
      !NTL::IsZero(recon[9])) {
    throw std::runtime_error(
        "Distributed ZKP failed. Malicious behaviour detected.");
  }
}

void OfflineEvaluator::zkVerifyRecursiveIter(
    int prover_id, const NTL::Vec<NTL::ZZ_pE>& poly_p_shares) {
  auto p_idx = offsetFromPid(id_, prover_id) - 1;
  auto& inputs = zk_verify_[p_idx];
  auto num_poly = inputs.length() / 2;

  NTL::ZZ_pE r;
  randomizeZZpE(rgen_.getComplement(prover_id), r);

  // Compute f_i(r) as inputs for next round.
  NTL::Vec<NTL::ZZ_pE> next_inputs;
  next_inputs.SetLength(num_poly);
  NTL_EXEC_RANGE(num_poly, first, last);
  ZZ_p_ctx_.restore();
  ZZ_pE_ctx_.restore();

  for (long i = first; i < last; ++i) {
    next_inputs[i] = inputs[i];
    next_inputs[i] += r * (inputs[i + num_poly] - inputs[i]);
  }
  NTL_EXEC_RANGE_END;

  inputs.move(next_inputs);

  if (prover_id == pidFromOffset(id_, -1)) {
    zk_res_.push_back(tpool_->enqueue([&, prover_id, r]() {
      ZZ_p_ctx_.restore();
      ZZ_pE_ctx_.restore();
      sendZZpE(network_->getSendChannel(prover_id), &r, 1);
    }));
  }

  // To be opened later for checking correctness.
  zk_check_[p_idx].push_back(poly_p_shares[0] + poly_p_shares[1] -
                             zk_exp_val_[p_idx]);

  // Compute p(r).
  auto& lgc = lagrange_coeff_d3_;
  auto r_x = r - x_interp_;
  auto r_1 = r - 1;
  zk_exp_val_[p_idx] = lgc[0] * r_1 * r_x * poly_p_shares[0] +
                       lgc[1] * r * r_x * poly_p_shares[1] +
                       lgc[2] * r * r_1 * poly_p_shares[2];
}

void OfflineEvaluator::zkVerifyBase(int prover_id, emp::block cc_key,
                                    const NTL::Vec<NTL::ZZ_pE>& pi) {
  auto p_idx = offsetFromPid(id_, prover_id) - 1;
  emp::PRG prg(&cc_key, prover_id);
  NTL::ZZ_pE r;
  randomizeZZpE(prg, r);

  // Compute f_i(r) (over additive shares).
  auto& inputs = zk_verify_[p_idx];
  NTL::Vec<NTL::ZZ_pE> shares;
  shares.SetLength(10);  // Reserve space for later use.
  for (long i = 0; i < 7; ++i) {
    shares[i] = pi[i];
    shares[i] += r * (inputs[i] - pi[i]);
  }

  // Compute p(r) (over additive shares).
  auto& lgc = lagrange_coeff_d3_;
  auto r_x = r - x_interp_;
  auto r_1 = r - 1;
  shares[7] = lgc[0] * r_1 * r_x * pi[7] + lgc[1] * r * r_x * pi[8] +
              lgc[2] * r * r_1 * pi[9];

  shares[8] = pi[8] - zk_exp_val_[p_idx];

  NTL::ZZ_pE beta{};
  for (auto& s : zk_check_[p_idx]) {
    randomizeZZpE(prg, beta);
    shares[9] += beta * s;
  }

  auto opposite_pid = pidFromOffset(prover_id, 2);
  if (id_ == opposite_pid) {
    auto& recv_shares = opp_check_shares_;
    recv_shares.SetDims(3, 10);
    recv_shares[0] = shares;

    zk_res_.push_back(tpool_->enqueue([&, prover_id]() {
      ZZ_p_ctx_.restore();
      ZZ_pE_ctx_.restore();
      receiveZZpE(network_->getRecvChannel(pidFromOffset(prover_id, 1)),
                  opp_check_shares_[1].elts(), 10);
    }));

    zk_res_.push_back(tpool_->enqueue([&, prover_id]() {
      ZZ_p_ctx_.restore();
      ZZ_pE_ctx_.restore();
      receiveZZpE(network_->getRecvChannel(pidFromOffset(prover_id, 3)),
                  opp_check_shares_[2].elts(), 10);
    }));
  } else {
    zk_res_.push_back(tpool_->enqueue([&, prover_id, shares]() {
      ZZ_p_ctx_.restore();
      ZZ_pE_ctx_.restore();
      sendZZpE(network_->getSendChannel(pidFromOffset(prover_id, 2)),
               shares.elts(), 10);
    }));
  }
}

void OfflineEvaluator::computeABCrossTerms() {
  std::vector<Ring> send_vals(mult_gates_.size());
  std::vector<Ring> recv_vals(mult_gates_.size());

  auto recv_f = tpool_->enqueue([&]() {
    network_->recvRelative(2, recv_vals.data(),
                           sizeof(Ring) * recv_vals.size());
  });

  std::fill(ab_terms_.begin(), ab_terms_.end(),
            std::vector<Ring>(mult_gates_.size(), 0));
  zk_prove_.SetLength(7 * mult_gates_.size());
  zk_verify_[0].SetLength(7 * mult_gates_.size());
  zk_verify_[1].SetLength(7 * mult_gates_.size());
  zk_verify_[2].SetLength(7 * mult_gates_.size());

  std::vector<Ring> rand(mult_gates_.size());
  auto opposite_pid = pidFromOffset(id_, 2);
  for (size_t p = 0; p < 5; ++p) {
    if (p == id_) {
      rgen_.getRelative(1, 3).random_data(rand.data(),
                                          sizeof(Ring) * rand.size());

      for (size_t g = 0; g < mult_gates_.size(); ++g) {
        const auto& gate = mult_gates_[g];
        const auto& mask_in1 = preproc_.gates[gate.in1]->mask;
        const auto& mask_in2 = preproc_.gates[gate.in2]->mask;

        Ring sum = 0;
        auto base = g * 7;
        for (size_t i = 0; i < 3; ++i) {
          zk_prove_[base + 2 * i] = mask_in1[i];
          zk_prove_[base + 2 * i + 1] = mask_in2[i];

          for (size_t j = 0; j < 3; ++j) {
            if (i == j) {
              ab_terms_[i][g] += mask_in1[i] * mask_in2[i];
            } else {
              sum += mask_in1[i] * mask_in2[j];
            }
          }
        }
        zk_prove_[base + 6] = sum;
        auto share = sum - rand[g];
        ab_terms_[1][g] += share;
        send_vals[g] = share;
      }
    } else if (p != opposite_pid) {
      auto other_pid = id_ ^ pidFromOffset(p, 1) ^ pidFromOffset(p, 3);
      rgen_.get(p, other_pid)
          .random_data(rand.data(), sizeof(Ring) * rand.size());
      int loc = offsetFromPid(id_, other_pid) - 1;

      auto p_idx = offsetFromPid(id_, p) - 1;
      auto& zk_verif = zk_verify_[p_idx];

      int ploc = offsetFromPid(p, id_) - 1;
      for (size_t g = 0, base = 0; g < mult_gates_.size(); ++g, base += 7) {
        const auto& gate = mult_gates_[g];
        ab_terms_[loc][g] += rand[g];
        zk_verif[base + 2 * ploc] =
            preproc_.gates[gate.in1]->mask.commonValueWithParty(id_, p);
        zk_verif[base + 2 * ploc + 1] =
            preproc_.gates[gate.in2]->mask.commonValueWithParty(id_, p);

        // Only one of the parties needs to use the random value for additive
        // sharing.
        if (id_ == pidFromOffset(p, 1)) {
          zk_verif[base + 6] = rand[g];
        }
      }
    }
  }

  auto send_f = tpool_->enqueue([&]() {
    network_->sendRelative(2, send_vals.data(),
                           sizeof(Ring) * send_vals.size());
    network_->flush(opposite_pid);
  });

  recv_f.get();
  auto& zk_verif = zk_verify_[1];
  for (size_t g = 0, base = 0; g < mult_gates_.size(); ++g, base += 7) {
    const auto& gate = mult_gates_[g];
    ab_terms_[1][g] += recv_vals[g];
    zk_verif[base + 2] = preproc_.gates[gate.in1]->mask[1];
    zk_verif[base + 3] = preproc_.gates[gate.in2]->mask[1];
    zk_verif[base + 6] = recv_vals[g];
  }

  send_f.get();
}

void OfflineEvaluator::combineCrossTerms() {
  for (size_t i = 0; i < mult_gates_.size(); ++i) {
    const auto& gate = mult_gates_[i];
    auto* pregate =
        static_cast<PreprocMultGate<Ring>*>(preproc_.gates[gate.out].get());
    for (size_t j = 0; j < 3; ++j) {
      pregate->mask_prod[j] +=
          ab_terms_[j][i] + c_terms_[j][i] + c_terms_[j + 3][i];
    }
  }
}

// void OfflineEvaluator::computeOutputCommitments() {
//   if (circ_.outputs.empty()) {
//     return;
//   }

//   preproc_.output.resize(circ_.outputs.size());

//   // Compute share commitments.
//   emp::Hash hash;
//   std::array<char, emp::Hash::DIGEST_SIZE> commitment{};
//   for (size_t i = 0; i < circ_.outputs.size(); ++i) {
//     auto& preout = preproc_.output[i];
//     for (int pid = 0; pid < 5; ++pid) {
//       if (pid == id_) {
//         continue;
//       }

//       auto offset = offsetFromPid(id_, pid);
//       auto idx = offset - 1;

//       std::vector<uint8_t> opening(security_param_ / 8);
//       rgen_.getRelative(offset).random_data(opening.data(), opening.size());

//       const auto& val = preproc_.gates[circ_.outputs[i]]->mask[idx];
//       hash.put(&val, sizeof(Ring));
//       hash.put(opening.data(), opening.size() * sizeof(uint8_t));
//       hash.digest(commitment.data());
//       hash.reset();

//       for (int receiver = 0; receiver < 5; ++receiver) {
//         if (receiver != id_ && receiver != pid) {
//           jump_.jumpUpdate(id_, pid, receiver, emp::Hash::DIGEST_SIZE,
//                            commitment.data());
//         }
//       }

//       preout.openings[idx] = std::move(opening);
//     }
//   }

//   // Update jump receive info.
//   for (int pid1 = 0; pid1 < 5; ++pid1) {
//     for (int pid2 = pid1 + 1; pid2 < 5; ++pid2) {
//       if (pid1 != id_ && pid2 != id_) {
//         jump_.jumpUpdate(pid1, pid2, id_,
//                          emp::Hash::DIGEST_SIZE * circ_.outputs.size());
//       }
//     }
//   }

//   jump_.communicate(*network_ot_, *tpool_);

//   // Update preproc with received commitments.
//   for (int pid1 = 0; pid1 < 5; ++pid1) {
//     for (int pid2 = pid1 + 1; pid2 < 5; ++pid2) {
//       if (pid1 == id_ || pid2 == id_) {
//         continue;
//       }

//       auto idx = offsetFromPid(id_, pid1) + offsetFromPid(id_, pid2) - 3;
//       const auto& commitments = jump_.getValues(pid1, pid2);
//       auto start_it = commitments.begin();
//       auto end_it = commitments.begin() + emp::Hash::DIGEST_SIZE;
//       for (size_t i = 0; i < circ_.outputs.size(); ++i) {
//         auto& preout = preproc_.output[i];
//         std::copy(start_it, end_it, preout.commitments[idx].begin());
//         start_it += emp::Hash::DIGEST_SIZE;
//         end_it += emp::Hash::DIGEST_SIZE;
//       }
//     }
//   }

//   jump_.reset();
// }

// void OfflineEvaluator::computeCCrossTerms() {
//   // Prepare inputs for OT.
//   std::array<std::vector<Ring>, 3> vmask_in1;
//   std::array<std::vector<Ring>, 3> vmask_in2;
//   for (const auto& g : mult_gates_) {
//     const auto& mask_in1 = preproc_.gates[g.in1]->mask;
//     const auto& mask_in2 = preproc_.gates[g.in2]->mask;
//     for (size_t i = 0; i < 3; ++i) {
//       vmask_in1[i].push_back(mask_in1[i]);
//       vmask_in2[i].push_back(mask_in2[i]);
//     }
//   }

//   std::vector<std::future<void>> res;
//   // Combinations for computing C terms.
//   // Verbose and harder to debug when programmatically computed.
//   std::array<std::array<int, 4>, 6> combos = {{{0, 1, 2, 3},
//                                                {2, 3, 0, 1},
//                                                {0, 2, 3, 1},
//                                                {3, 1, 0, 2},
//                                                {3, 0, 2, 1},
//                                                {2, 1, 3, 0}}};
//   for (const auto& combo : combos) {
//     bool sender = true;
//     int share_pid = id_ ^ combo[0] ^ combo[1];
//     int other_pid = id_ ^ combo[0] ^ combo[2];

//     if (id_ == combo[2] || id_ == combo[3]) {
//       sender = false;
//       share_pid = id_ ^ combo[2] ^ combo[3];
//     }

//     if (id_ == combo[1] || id_ == combo[3]) {
//       other_pid = id_ ^ combo[1] ^ combo[3];
//     }

//     if (sender) {
//       res.push_back(tpool_->enqueue([&, share_pid, other_pid]() {
//         auto loc = offsetFromPid(id_, share_pid) - 1;
//         c_terms_[loc] =
//             ot_[other_pid]->multiplySend(vmask_in1[loc], rgen_.get(share_pid));
//         network_ot_->flush(other_pid);
//       }));
//     } else {
//       res.push_back(tpool_->enqueue([&, share_pid, other_pid]() {
//         auto loc = offsetFromPid(id_, share_pid) - 1;
//         c_terms_[loc + 3] = ot_[other_pid]->multiplyReceive(vmask_in2[loc]);
//       }));
//     }
//   }

//   for (auto& f : res) {
//     f.get();
//   }

//   emp::Hash hash;
//   std::array<char, emp::Hash::DIGEST_SIZE> digest{};
//   for (const auto& combo : combos) {
//     if (id_ == combo[2] || id_ == combo[3]) {
//       auto share_pid = id_ ^ combo[2] ^ combo[3];
//       auto loc = offsetFromPid(id_, share_pid) - 1;
//       hash.put(c_terms_[loc + 3].data(),
//                sizeof(Ring) * c_terms_[loc + 3].size());
//       hash.digest(digest.data());
//       hash.reset();

//       jump_.jumpUpdate(id_, share_pid, combo[0],
//                        sizeof(char) * emp::Hash::DIGEST_SIZE, digest.data());
//       jump_.jumpUpdate(id_, share_pid, combo[1],
//                        sizeof(char) * emp::Hash::DIGEST_SIZE, digest.data());
//     } else {
//       jump_.jumpUpdate(combo[2], combo[3], id_,
//                        sizeof(char) * emp::Hash::DIGEST_SIZE);
//     }
//   }

//   jump_.communicate(*network_ot_, *tpool_);
//   jump_.reset();
// }
};  // namespace HoRGod
