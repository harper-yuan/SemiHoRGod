#include "sharing.h"

namespace HoRGod {
template <>
void ReplicatedShare<BoolRing>::randomize(emp::PRG& prg) {
  bool values[4];
  prg.random_bool(static_cast<bool*>(values), 4);
  for (size_t i = 0; i < 4; ++i) {
    values_[i] = values[i];
  }
}

template <>
DummyShare<BoolRing>::DummyShare(BoolRing secret, emp::PRG& prg) {
  bool values[4];
  prg.random_bool(static_cast<bool*>(values), 4);

  BoolRing sum;
  for (size_t i = 0; i < 4; ++i) {
    share_elements[i] = values[i];
    sum += share_elements[i];
  }
  share_elements[4] = secret - sum;
}

template <>
void DummyShare<BoolRing>::randomize(emp::PRG& prg) {
  bool values[5];
  prg.random_bool(static_cast<bool*>(values), 5);

  for (size_t i = 0; i < 5; ++i) {
    share_elements[i] = values[i];
  }
}
};  // namespace HoRGod
