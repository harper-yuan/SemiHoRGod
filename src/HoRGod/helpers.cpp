#include "helpers.h"
#include <NTL/ZZ_pX.h>
#include <cmath>


namespace HoRGod {
int pidFromOffset_N(int id, int offset, int Np) { //通过进程号pid+offset识别其编号i ∈ {1,2,3,4}
  int pid = (id + offset) % Np;
  if (pid < 0) {
    pid += Np;
  }
  return pid;
}

std::tuple<int, int, int> sortThreeNumbers(int a, int b, int c) {
    if (a > b) std::swap(a, b);
    if (b > c) std::swap(b, c);
    if (a > b) std::swap(a, b);  // 确保完全排序
    return {a, b, c};  // 返回排序后的元组
}

std::tuple<int, int> findOtherSenders(int min, int mid, int max, int id_) {
    if (id_ == min) {
        return {mid, max};  // mid < max
    } else if (id_ == mid) {
        return {min, max};  // min < max
    } else {  // id_ == max
        return {min, mid};  // min < mid
    }
}

bool isEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && 
           memcmp(a.data(), b.data(), a.size()) == 0;
}

int pidFromOffset(int id, int offset) { //通过进程号pid+offset识别其编号i ∈ {0,1,2,3,4}
  int pid = (id + offset) % 5;
  if (pid < 0) {
    pid += 5;
  }
  return pid;
}



int idxFromSenderAndReceiver(int sender_id, int receiver_id) { //确定reciever所需数据，在sender所拥有的数组中的索引
  //假如sender_id = 4, vector = {0,1,2,3},索引就是receiver_id
  if (sender_id > receiver_id) {
    return receiver_id;
  }
  //假如sender_id = 0, vector = {1,2,3,4},索引就是receiver_id-1
  else {
    return receiver_id-1;
  }
}

int offsetFromPid(int id, int pid) {
  if (id < pid) {
    return pid - id;
  }

  return 4 + pid - id;
}

size_t upperTriangularToArray(size_t i, size_t j) {
  // (i, j) co-ordinate in upper triangular matrix (without diagonal) to array
  // index in column major order.
  auto mn = std::min(i, j);
  auto mx = std::max(i, j);
  auto idx = (mx * (mx - 1)) / 2 + mn;
  return idx;
}

std::vector<uint64_t> packBool(const bool* data, size_t len) {
  std::vector<uint64_t> res;
  for (size_t i = 0; i < len;) {
    uint64_t temp = 0;
    for (size_t j = 0; j < 64 && i < len; ++j, ++i) {
      if (data[i]) {
        temp |= (0x1ULL << j);
      }
    }
    res.push_back(temp);
  }

  return res;
}

void unpackBool(const std::vector<uint64_t>& packed, bool* data, size_t len) {
  for (size_t i = 0, count = 0; i < len; count++) {
    uint64_t temp = packed[count];
    for (int j = 63; j >= 0 && i < len; ++i, --j) {
      data[i] = (temp & 0x1) == 0x1;
      temp >>= 1;
    }
  }
}

void randomizeZZpE(emp::PRG& prg, NTL::ZZ_pE& val) {
  std::vector<Ring> coeff(NTL::ZZ_pE::degree());
  prg.random_data(coeff.data(), sizeof(Ring) * coeff.size());

  NTL::ZZ_pX temp;
  temp.SetLength(NTL::ZZ_pE::degree());

  for (size_t i = 0; i < coeff.size(); ++i) {
    temp[i] = coeff[i];
  }

  NTL::conv(val, temp);
}

void randomizeZZpE(emp::PRG& prg, NTL::ZZ_pE& val, Ring rval) {
  std::vector<Ring> coeff(NTL::ZZ_pE::degree() - 1);
  prg.random_data(coeff.data(), sizeof(Ring) * coeff.size());

  NTL::ZZ_pX temp;
  temp.SetLength(NTL::ZZ_pE::degree());

  temp[0] = rval;
  for (size_t i = 1; i < coeff.size(); ++i) {
    temp[i] = coeff[i];
  }

  NTL::conv(val, temp);
}

void receiveZZpE(emp::NetIO* ios, NTL::ZZ_pE* data, size_t length) {
  auto degree = NTL::ZZ_pE::degree();
  // Assumes that every co-efficient of ZZ_pE is same range as Ring.
  std::vector<uint8_t> serialized(sizeof(Ring));

  NTL::ZZ_pX poly;
  poly.SetLength(degree);
  for (size_t i = 0; i < length; ++i) {
    for (size_t d = 0; d < degree; ++d) {
      ios->recv_data(serialized.data(), serialized.size());
      auto coeff = NTL::conv<NTL::ZZ_p>(
          NTL::ZZFromBytes(serialized.data(), serialized.size()));
      poly[d] = coeff;
    }
    NTL::conv(data[i], poly);
  }
}

void sendZZpE(emp::NetIO* ios, const NTL::ZZ_pE* data, size_t length) {
  auto degree = NTL::ZZ_pE::degree();
  // Assumes that every co-efficient of ZZ_pE is same range as Ring.
  std::vector<uint8_t> serialized(sizeof(Ring));

  for (size_t i = 0; i < length; ++i) {
    const auto& poly = NTL::rep(data[i]);
    for (size_t d = 0; d < degree; ++d) {
      const auto& coeff = NTL::rep(NTL::coeff(poly, d));
      NTL::BytesFromZZ(serialized.data(), coeff, serialized.size());
      ios->send_data(serialized.data(), serialized.size());
    }
  }
  ios->flush();
}

Ring generate_specific_bit_random(emp::PRG& prg, uint32_t a) {
  if (a == 0 || a > 64) {
      throw std::invalid_argument("a must be between 1 and 64");
  }

  // 计算需要的随机字节数（向上取整）
  const uint32_t num_bytes = (a + 7) / 8; 
  uint8_t random_bytes[16] = {0}; // 初始化为0，确保未使用的字节为0

  // 生成随机字节（最多8字节）
  prg.random_data(random_bytes, num_bytes);

  // 将字节拷贝到uint64_t（小端序处理）
  Ring result = 0;
  memcpy(&result, random_bytes, num_bytes);

  // 屏蔽超出a比特的高位（重要！）
  if (a < 64) {
      const uint64_t mask = (1ULL << a) - 1;
      result &= mask;
  }
  return result;
}
};  // namespace HoRGod
