#include "rand_gen_pool.h"

#include <algorithm>

#include "helpers.h"

namespace HoRGod {

RandGenPool::RandGenPool(int my_id, uint64_t seed) : id_{my_id} {
  auto seed_block = emp::makeBlock(seed, 0);

  //将一组参与方ID编码成一个唯一的整数，用于生成不同的随机数流。
  //例如 {1, 2} → 1 * 1 + 2 * 5 = 9
  auto encode = [](std::vector<int> parties) {   
    // Ordering of parties should not impact encoding.
    std::sort(parties.begin(), parties.end());

    int res = 0;
    int pow = 1;
    for (int party : parties) {
      res += pow * party;
      pow *= 5;
    }

    return res;
  };
  //压入5个随机数生成器，分别是{id_,0},{id_,1},{id_,2},{id_,3},{id_,4}
  for (int i = 0; i < 5; ++i) {
    v_rgen_.emplace_back(&seed_block, encode({id_, i}));
  }

  for (int i = 0; i < 5; ++i) {
    std::vector<int> parties = {id_};
    for (int j = 0; j < 5; ++j) {
      if (j == i || j == id_) {
        continue;
      }
      parties.push_back(j);
    }
    //压入的第i个随机数生成器，即v_rgen_[i + 5]代表除了i其他人都有的随机数生成器
    v_rgen_.emplace_back(&seed_block, encode(parties));
  }
}

emp::PRG& RandGenPool::self() { return v_rgen_[id_]; }

emp::PRG& RandGenPool::all() { return v_rgen_[id_ + 5]; }

emp::PRG& RandGenPool::get(int pid) { return v_rgen_.at(pid); }

emp::PRG& RandGenPool::getRelative(int offset) {
  return v_rgen_.at(pidFromOffset(id_, offset));
}

emp::PRG& RandGenPool::get(int pid1, int pid2) {
  return v_rgen_.at(5 + (pid1 ^ pid2 ^ id_));
}

emp::PRG& RandGenPool::getRelative(int offset1, int offset2) {
  return get(pidFromOffset(id_, offset1), pidFromOffset(id_, offset2));
}

emp::PRG& RandGenPool::getComplement(int pid) { return v_rgen_.at(5 + pid); }

emp::PRG& RandGenPool::getComplementRelative(int offset) {
  return getComplement(pidFromOffset(id_, offset));
}

}  // namespace HoRGod
