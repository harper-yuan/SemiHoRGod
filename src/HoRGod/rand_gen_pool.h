#pragma once
#include <emp-tool/emp-tool.h>

#include <vector>

namespace HoRGod {

// Collection of PRGs.
class RandGenPool {
  int id_;
  //最后会包含
  //for i<5 v_rgen_[i] 的随机种子为 {id_, i} v_rgen[i+5] 的随机种子为 ({id_}∪{0，1，2，3，4})\{i}
  // v_rgen_[i] denotes PRG common with party i.
  // v_rgen_[id_] is PRG not common with any party.
  // v_rgen_[i + 5] denotes PRG common with all parties except i.
  //   v_rgen_[id_ + 5] is PRG common with all parties.
  
  std::vector<emp::PRG> v_rgen_;

 public:
  explicit RandGenPool(int my_id, uint64_t seed = 200);

  emp::PRG& self();
  emp::PRG& all();

  emp::PRG& get(int pid);
  emp::PRG& getRelative(int offset);

  emp::PRG& get(int pid1, int pid2);
  emp::PRG& getRelative(int offset1, int offset2);

  emp::PRG& getComplement(int pid);
  emp::PRG& getComplementRelative(int offset);
};

};  // namespace HoRGod
