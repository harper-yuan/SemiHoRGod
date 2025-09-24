#pragma once
#include <cstdint>
#include <iostream>
#include <vector>

#define NUM_RSS 21
#define NUM_DSS 21
#define NUM_PARTIES 7
namespace SemiHoRGod {
using Ring = uint64_t;
constexpr uint64_t FRACTION = 16;
constexpr size_t N = 64; //bit size of Ring, 64 bits
constexpr int INPUT_PERMUTATION = 0; //bit size of Ring, 64 bits

class BoolRing {
  bool val_;

 public:
  BoolRing();
  BoolRing(bool val);
  BoolRing(int val);

  [[nodiscard]] bool val() const;

  bool operator==(const BoolRing& rhs) const;

  BoolRing& operator+=(const BoolRing& rhs);
  BoolRing& operator-=(const BoolRing& rhs);
  BoolRing& operator*=(const BoolRing& rhs);

  static std::vector<uint8_t> pack(const BoolRing* data, size_t len);
  static std::vector<BoolRing> unpack(const uint8_t* packed, size_t len);

  friend BoolRing operator+(BoolRing lhs, const BoolRing& rhs);
  friend BoolRing operator-(BoolRing lhs, const BoolRing& rhs);
  friend BoolRing operator*(BoolRing lhs, const BoolRing& rhs);

  friend std::ostream& operator<<(std::ostream& os, const BoolRing& b);
};
};  // namespace SemiHoRGod
