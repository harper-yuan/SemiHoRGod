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

namespace SemiHoRGod {
int pidFromOffset_N(int id, int offset, int Np);
int pidFromOffset(int id, int offset);
std::tuple<int, int, int> sortThreeNumbers(int a, int b, int c);
std::tuple<int, int, int, int> findRemainingNumbers_7PC(int i, int j, int k);
std::tuple<int, int, int, int, int> findRemainingNumbers_7PC(int i, int j);
std::tuple<int, int, int> findRemainingNumbers_7PC(int i, int j, int k, int id);
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


template<typename PermType, typename DataType>
void applyPermutation(const std::vector<PermType>& perm, std::vector<DataType>& data_vec) {
    if (perm.size() != data_vec.size()) {
        throw std::invalid_argument("Permutation vector and data vector must have the same size");
    }
    
    std::vector<DataType> temp_vec = data_vec;
    
    for (size_t i = 0; i < perm.size(); ++i) {
        size_t new_position = static_cast<size_t>(perm[i]);
        if (new_position >= data_vec.size()) {
            throw std::out_of_range("Permutation index out of range");
        }
        data_vec[new_position] = temp_vec[i];
    }
}

template<typename T>
std::vector<T> composePermutations(const std::vector<T>& p1, const std::vector<T>& p2) {
    if (p1.size() != p2.size()) {
        throw std::invalid_argument("Permutations must have the same size");
    }
    
    int n = p1.size();
    std::vector<T> result(n);
    
    for (int i = 0; i < n; i++) {
        // 检查索引范围
        if (p2[i] < 0 || p2[i] >= n) {
            throw std::out_of_range("Index in first permutation out of range");
        }
        if (p1[p2[i]] < 0 || p1[p2[i]] >= n) {
            throw std::out_of_range("Index in second permutation out of range");
        }
        
        // 复合置换：result[i] = p1[p2[i]]
        result[i] = p1[p2[i]];
    }
    
    return result;
}


template<typename T>
std::vector<T> inversePermutation(const std::vector<T>& perm) {
    static_assert(std::is_integral<T>::value, "T must be an integral type for indexing");
    
    int n = perm.size();
    std::vector<T> inv(n);
    
    for (T i = 0; i < n; i++) {
        inv[perm[i]] = i;
    }
    
    return inv;
}

std::vector<uint64_t> packBool(const bool* data, size_t len);
void unpackBool(const std::vector<uint64_t>& packed, bool* data, size_t len);
void randomizeZZpE(emp::PRG& prg, NTL::ZZ_pE& val);
void randomizeZZpE(emp::PRG& prg, NTL::ZZ_pE& val, Ring rval);

void sendZZpE(emp::NetIO* ios, const NTL::ZZ_pE* data, size_t length);
void receiveZZpE(emp::NetIO* ios, NTL::ZZ_pE* data, size_t length);
};  // namespace SemiHoRGod
