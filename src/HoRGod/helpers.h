#pragma once

#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pE.h>
#include <emp-tool/emp-tool.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../io/netmp.h"
#include "types.h"


#define CMP_GREATER_RESULT 1
#define CMP_lESS_RESULT 0
#define BITS_BETA 4
#define BITS_GAMMA 20

namespace HoRGod {
int pidFromOffset_N(int id, int offset, int Np);
int pidFromOffset(int id, int offset);
std::tuple<int, int, int> sortThreeNumbers(int a, int b, int c);
std::tuple<int, int> findRemainingNumbers(int min, int mid, int max);
std::tuple<int, int, int> findRemainingNumbers(int min, int max);
std::tuple<int, int> findOtherSenders(int min, int mid, int max, int id_);
bool isEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);
int offsetFromPid(int id, int pid);
int idxFromSenderAndReceiver(int sender_id, int receiver_id);
size_t upperTriangularToArray(size_t i, size_t j);
Ring generate_specific_bit_random(emp::PRG& prg, uint32_t a);
// Supports only native int type.
template <class R>
std::vector<BoolRing> bitDecompose(R val) {
  auto num_bits = sizeof(val) * 8;
  std::vector<BoolRing> res(num_bits);
  for (size_t i = 0; i < num_bits; ++i) {
    res[i] = ((val >> i) & 1ULL) == 1;
  }

  return res;
}

std::vector<uint64_t> packBool(const bool* data, size_t len);
void unpackBool(const std::vector<uint64_t>& packed, bool* data, size_t len);
void randomizeZZpE(emp::PRG& prg, NTL::ZZ_pE& val);
void randomizeZZpE(emp::PRG& prg, NTL::ZZ_pE& val, Ring rval);

void sendZZpE(emp::NetIO* ios, const NTL::ZZ_pE* data, size_t length);
void receiveZZpE(emp::NetIO* ios, NTL::ZZ_pE* data, size_t length);
};  // namespace HoRGod
