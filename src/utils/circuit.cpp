#include "circuit.h"

#include <stdexcept>

namespace SemiHoRGod::utils {

Gate::Gate(GateType type, wire_t out) : type(type), out(out) {}

FIn2Gate::FIn2Gate(GateType type, wire_t in1, wire_t in2, wire_t out)
    : Gate(type, out), in1{in1}, in2{in2} {}

FIn1Gate::FIn1Gate(GateType type, wire_t in, wire_t out)
    : Gate(type, out), in{in} {}

SIMDGate::SIMDGate(GateType type, std::vector<wire_t> in1,
                   std::vector<wire_t> in2, wire_t out)
    : Gate(type, out), in1(std::move(in1)), in2(std::move(in2)) {}

//harper permu
PermGate::PermGate(GateType type, std::vector<wire_t> in1, std::vector<wire_t> in2, std::vector<wire_t> multi_out)
    : Gate(type, multi_out[0]), in1(std::move(in1)), in2(std::move(in2)), multi_out(std::move(multi_out)) {}  

std::ostream& operator<<(std::ostream& os, GateType type) {
  switch (type) {
    case kInp:
      os << "Input";
      break;

    case kAdd:
      os << "Addition";
      break;

    case kMul:
      os << "Multiplication";
      break;

    case kSub:
      os << "Subtraction";
      break;

    case kConstAdd:
      os << "Addition with constant";
      break;

    case kConstMul:
      os << "Multiplication with constant";
      break;

    case kRelu:
      os << "ReLU";
      break;

    case kMsb:
      os << "MSB";
      break;

    case kCmp:
      os << "Compare";
      break;

    case kDotprod:
      os << "Dotproduct";
      break;

    case kTrdotp:
      os << "Dotproduct with truncation";
      break;

    case kPerm:
      os << "Permutation";
      break;
      
    default:
      os << "Invalid";
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const LevelOrderedCircuit& circ) {
  for (size_t i = 0; i < GateType::NumGates; ++i) {
    os << GateType(i) << ": " << circ.count[i] << "\n";
  }
  os << "Total: " << circ.num_gates << "\n";
  os << "Depth: " << circ.gates_by_level.size() << "\n";
  return os;
}
};  // namespace SemiHoRGod::utils
