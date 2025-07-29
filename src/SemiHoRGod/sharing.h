#pragma once

#include <emp-tool/emp-tool.h>

#include <array>
#include <vector>

#include "helpers.h"
#include "types.h"

#define NUM_RSS 21
#define NUM_DSS 21
namespace SemiHoRGod {

template <class R>
class ReplicatedShare {
  // values_[i] will denote element common with party having my_id + i + 1.
  std::array<R, NUM_RSS> values_; //

 public:
  ReplicatedShare() = default;
  explicit ReplicatedShare(std::array<R, NUM_RSS> values)
      : values_{std::move(values)} {}

  void randomize(emp::PRG& prg) {
    prg.random_data(values_.data(), sizeof(R) * NUM_RSS);
  }

  void init_zero() {
    for(int i = 0;i<NUM_RSS;i++) {
      values_[i] = 0;
    }
  }
  // Access share elements.
  // idx = i retreives value common with party having my_id + i + 1.
  R& operator[](size_t idx) { return values_.at(idx); }

  R operator[](size_t idx) const { return values_.at(idx); }

  [[nodiscard]] R sum() const {
    R result = 0;
    for(int i = 0;i<NUM_RSS;i++) {
      result += values_[i];
    }
    return result; 
    }

  // Arithmetic operators.
  ReplicatedShare<R>& operator+=(const ReplicatedShare<R> rhs) {
    for(int i = 0;i<NUM_RSS;i++) {
      values_[i] += rhs.values_[i];
    }
    return *this;
  }

  
  ReplicatedShare<R> cosnt_add(const R& rhs) const {
    ReplicatedShare<R> result = *this;  // 复制当前对象
    for(int i = 0;i<NUM_RSS;i++) {
      result.values_[i] += rhs;
    }
    return result;                      // 返回新对象
  }

  ReplicatedShare<R> cosnt_mul(const R& rhs) const {
    ReplicatedShare<R> result = *this;  // 复制当前对象
    for(int i = 0;i<NUM_RSS;i++) {
      result.values_[i] *= rhs;
    }
    return result;
  }

  friend ReplicatedShare<R> operator+(ReplicatedShare<R> lhs,
                                      const ReplicatedShare<R>& rhs) {
    lhs += rhs;
    return lhs;
  }

  ReplicatedShare<R>& operator-=(const ReplicatedShare<R>& rhs) {
    (*this) += (rhs * -1);
    return *this;
  }

  friend ReplicatedShare<R> operator-(ReplicatedShare<R> lhs,
                                      const ReplicatedShare<R>& rhs) {
    lhs -= rhs;
    return lhs;
  }

  ReplicatedShare<R>& operator*=(const R& rhs) {
    for(int i = 0;i<NUM_RSS;i++) {
      values_[i] *= rhs;
    }
    return *this;
  }

  friend ReplicatedShare<R> operator*(ReplicatedShare<R> lhs, const R& rhs) {
    lhs *= rhs;
    return lhs;
  }

  ReplicatedShare<R>& add(R val, int pid) {
    // if (pid == 0) {
    //   values_[0] += val;
    // } else if (pid == 1) {
    //   values_[2] += val;
    // }
    values_[pid] += val;

    return *this;
  }
};

// Contains all elements of a secret sharing. Used only for generating dummy
// preprocessing data.
template <class R>
struct DummyShare {
  std::array<R, 21> share_elements;

  DummyShare() = default;

  explicit DummyShare(std::array<R, 21> share_elements)
      : share_elements(std::move(share_elements)) {}

  DummyShare(R secret, emp::PRG& prg) {
    prg.random_data(share_elements.data(), sizeof(R) * 21);

    R sum = share_elements[0];
    for (int i = 1; i < 20; ++i) {
      sum += share_elements[i]; //把前20个数相加
    }
    share_elements[20] = secret - sum; //最后一个共享的值
  }
  
  void randomize(emp::PRG& prg) {
    prg.random_data(share_elements.data(), sizeof(R) * 21); //随机化5个值
  }

  [[nodiscard]] R secret() const { //返回5个随机值的和，秘密共享\beta = x + sum即可
    R sum = share_elements[0];
    for (size_t i = 1; i < 21; ++i) {
      sum += share_elements[i];
    }
    return sum;
  }

  DummyShare<R>& operator+=(const DummyShare<R>& rhs) {
    for (size_t i = 0; i < 21; ++i) {
      share_elements[i] += rhs.share_elements[i];
    }

    return *this;
  }

  friend DummyShare<R> operator+(DummyShare<R> lhs, const DummyShare<R>& rhs) {
    lhs += rhs;
    return lhs;
  }

  DummyShare<R>& operator-=(const DummyShare<R>& rhs) {
    for (size_t i = 0; i < 21; ++i) {
      share_elements[i] -= rhs.share_elements[i];
    }

    return *this;
  }

  friend DummyShare<R> operator-(DummyShare<R> lhs, const DummyShare<R>& rhs) {
    lhs -= rhs;
    return lhs;
  }

  DummyShare<R>& operator*=(const R& rhs) {
    for (size_t i = 0; i < 21; ++i) {
      share_elements[i] *= rhs;
    }

    return *this;
  }

  friend DummyShare<R> operator*(DummyShare<R> lhs, const R& rhs) {
    lhs *= rhs;
    return lhs;
  }

  friend DummyShare<R> operator*(const R& lhs, DummyShare<R> rhs) {
    // Assumes abelian ring.
    rhs *= lhs;
    return rhs;
  }

  ReplicatedShare<R> getRSS(size_t pid) {//返回对应的冗余秘密共享，对于pid=0，返回的共享值为1,2,3,4，即不包含0
    pid = pid % NP;
    std::array<R, NUM_RSS> values;
    size_t counter = 0;
    for (size_t i = 0; i < NP; ++i) {
      for (size_t j = i+1; j < NP; ++j) {
        if (i != pid && j != pid) { 
          values[upperTriangularToArray(i, j)] = share_elements.at(upperTriangularToArray(i, j));
        }
        else {
          values[upperTriangularToArray(i, j)] = 0;
        }
      }
    }
    return ReplicatedShare<R>(values);
  }
};
}  // namespace SemiHoRGod
