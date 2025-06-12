#pragma once

#include "../utils/circuit.h"
#include "sharing.h"
#include "types.h"

namespace HoRGod {
// Preprocessed data for a gate.
template <class R>
struct PreprocGate {
  // Secret shared mask for the output wire of the gate.
  ReplicatedShare<R> mask{};

  PreprocGate() = default;

  explicit PreprocGate(const ReplicatedShare<R>& mask) : mask(mask) {}

  virtual ~PreprocGate() = default;
};

template <class R>
using preprocg_ptr_t = std::unique_ptr<PreprocGate<R>>;

template <class R>
struct PreprocInput : public PreprocGate<R> {
  // ID of party providing input on wire.
  int pid{};
  // Plaintext value of mask on input wire. Non-zero for all parties except
  // party with id 'pid'.
  R mask_value{};

  PreprocInput() = default;
  PreprocInput(const ReplicatedShare<R>& mask, int pid, R mask_value = 0)
      : PreprocGate<R>(mask), pid(pid), mask_value(mask_value) {}
};

template <class R>
struct PreprocMultGate : public PreprocGate<R> {
  // Secret shared product of inputs masks.
  ReplicatedShare<R> mask_prod{};

  PreprocMultGate() = default;
  PreprocMultGate(const ReplicatedShare<R>& mask,
                  const ReplicatedShare<R>& mask_prod)
      : PreprocGate<R>(mask), mask_prod(mask_prod) {}
};

template <class R>
struct PreprocCmpGate : public PreprocGate<R> {
  // Secret shared product of inputs masks.
  ReplicatedShare<R> mask_prod{};
  ReplicatedShare<R> mask_mu_1{};
  ReplicatedShare<R> mask_mu_2{};
  R beta_mu_1;
  R beta_mu_2;
  ReplicatedShare<R> prev_mask{}; 
  //比较运算比较特殊，涉及到多个门，通常来说α要在离线提前计算完成，但是Cmp需要一个乘法和一个const 加法，乘法的中间变量的alpha是需要用的，所以需要保存下来用来计算乘法
  PreprocCmpGate() = default;
  PreprocCmpGate(const ReplicatedShare<R>& mask,
                  const ReplicatedShare<R>& mask_prod,
                  const ReplicatedShare<R>& mask_mu_1,
                  const ReplicatedShare<R>& mask_mu_2,
                  const Ring beta_mu_1,
                  const Ring beta_mu_2,
                  const ReplicatedShare<R>& prev_mask)
      : PreprocGate<R>(mask), mask_prod(mask_prod), 
      mask_mu_1(mask_mu_1), mask_mu_2(mask_mu_2), beta_mu_1(beta_mu_1), beta_mu_2(beta_mu_2), prev_mask(prev_mask) {}
};

template <class R>
struct PreprocReluGate : public PreprocGate<R> {
  // Secret shared product of inputs masks.
  ReplicatedShare<R> mask_prod{};
  ReplicatedShare<R> mask_mu_1{};
  ReplicatedShare<R> mask_mu_2{};
  R beta_mu_1;
  R beta_mu_2;
  ReplicatedShare<R> prev_mask{}; //如果比较结果大于0，那么直接返回原始值，所以这里需要保存一个输入mask
  ReplicatedShare<R> mask_prod2{};
  ReplicatedShare<R> mask_for_mul{};
  PreprocReluGate() = default;
  PreprocReluGate(const ReplicatedShare<R>& mask,
                  const ReplicatedShare<R>& mask_prod,
                  const ReplicatedShare<R>& mask_mu_1,
                  const ReplicatedShare<R>& mask_mu_2,
                  const Ring beta_mu_1,
                  const Ring beta_mu_2,
                  const ReplicatedShare<R>& prev_mask,
                  const ReplicatedShare<R>& mask_prod2,
                  const ReplicatedShare<R>& mask_for_mul)
      : PreprocGate<R>(mask), mask_prod(mask_prod), 
      mask_mu_1(mask_mu_1), mask_mu_2(mask_mu_2), beta_mu_1(beta_mu_1), 
      beta_mu_2(beta_mu_2), prev_mask(prev_mask), mask_prod2(mask_prod2),
      mask_for_mul(mask_for_mul) {}
};

template <class R>
struct PreprocDotpGate : public PreprocGate<R> {
  ReplicatedShare<Ring> mask_prod{};

  PreprocDotpGate() = default;
  PreprocDotpGate(const ReplicatedShare<Ring>& mask,
                  const ReplicatedShare<Ring>& mask_prod)
      : PreprocGate<R>(mask), mask_prod(mask_prod) {}
};

template <class R>
struct PreprocTrDotpGate : public PreprocGate<R> {
  ReplicatedShare<Ring> mask_prod{};
  ReplicatedShare<Ring> mask_d{};

  PreprocTrDotpGate() = default;
  PreprocTrDotpGate(const ReplicatedShare<Ring>& mask,
                    const ReplicatedShare<Ring>& mask_prod,
                    const ReplicatedShare<Ring>& mask_d)
      : PreprocGate<R>(mask), mask_prod(mask_prod), mask_d(mask_d) {}
};

template <class R>
struct PreprocMsbGate : public PreprocGate<R> {
  std::vector<preprocg_ptr_t<BoolRing>> msb_gates;
  ReplicatedShare<R> mask_msb;
  ReplicatedShare<R> mask_w;

  PreprocMsbGate() = default;
  PreprocMsbGate(ReplicatedShare<R> mask,
                 std::vector<preprocg_ptr_t<BoolRing>> msb_gates,
                 ReplicatedShare<R> mask_msb, ReplicatedShare<R> mask_w)
      : PreprocGate<R>(mask),
        msb_gates(std::move(msb_gates)),
        mask_msb(mask_msb),
        mask_w(mask_w) {}
};


// Preprocessed data for output wires.
struct PreprocOutput {
  // Commitment corresponding to share elements not available with the party
  // for the output wire. If party's ID is 'i' then array is of the form
  // {s[i+1, i+2], s[i+1, i+3], s[i+2, i+3]}.
  std::array<std::array<char, emp::Hash::DIGEST_SIZE>, 3> commitments{};

  // Opening info for commitments to party's output shares.
  // If party's ID is 'i' then array is of the form
  // {o[i, i+1], o[i, i+2], o[i, i+3]} where o[i, j] is the opening info for
  // share common to parties with ID i and j.
  std::array<std::vector<uint8_t>, 3> openings;
};

// Preprocessed data for the circuit.
template <class R>
struct PreprocCircuit {
  std::vector<preprocg_ptr_t<R>> gates;
  std::vector<PreprocOutput> output;

  PreprocCircuit() = default;
  PreprocCircuit(size_t num_gates, size_t num_output)
      : gates(num_gates), output(num_output) {}
};
};  // namespace HoRGod
