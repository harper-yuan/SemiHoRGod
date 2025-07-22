#include <iostream>
#include <cstdint>
#include <cassert>
#include <random>
#include <vector>
// 函数：获取一个数的最高d个比特
uint64_t get_top_d_bits(uint64_t num_uint64_t, int d) {
    assert(d > 0 && d <= 64);
    // 计算需要右移的位数
    int shift = d;
    // 获取最高d个比特
    int64_t num = (int64_t) num_uint64_t;
    num = num >> shift;
    uint64_t result = (uint64_t) (num);
    return result;
}

// 函数：获取一个数的最高d个比特
uint64_t get_top_d_bits_new(uint64_t num_uint64_t, int d) {
    assert(d > 0 && d <= 64);
    return num_uint64_t >> d;
}
// 函数：比较截断结果
void compare_truncated_sums(uint64_t x, uint64_t r, int d) {
    // 计算x-r
    uint64_t x_minus_r = x - r;
    
    // 获取(x-r)的最高d比特
    uint64_t trunc_x_minus_r = get_top_d_bits(x_minus_r, d);
    
    // 获取r的最高d比特
    uint64_t trunc_r = get_top_d_bits(r, d) - 3;
    
    // 计算两个截断的和
    uint64_t sum_trunc = trunc_x_minus_r + trunc_r;
    
    // 获取x的最高d比特
    uint64_t trunc_x = get_top_d_bits(x, d);
    
    // 比较结果
    if((int64_t)(sum_trunc - trunc_x) >=2 || (int64_t)(sum_trunc - trunc_x) <= -2) {
    // if((int64_t)(sum_trunc - trunc_x) >=2 || (int64_t)(sum_trunc - trunc_x) <= -2) {
        std::cout << "x: " << x << ", r: " << r << ", d: " << d <<", x-r: "<<x-r<<"\n";
        std::cout << "int64_t x: " << (int64_t)x << ", int64_t r: " << (int64_t)r << ", d: " << d <<", int64_t x-r: "<<((int64_t)(x))-((int64_t)(r))<<"\n";
        std::cout << "Truncated (x-r): " << trunc_x_minus_r << "\n";
        std::cout << "Truncated r: " << trunc_r << "\n";
        std::cout << "Sum of truncations: " << sum_trunc << "\n";
        std::cout << "Truncated x: " << trunc_x << "\n";
        
        if (sum_trunc == trunc_x) {
            std::cout << "Result: EQUAL\n";
        } else if (sum_trunc == trunc_x + 1) {
            std::cout << "Result: Sum is trunc_x + 1\n";
        } else {
            std::cout << "Result: NOT EQUAL (difference: " << (int64_t)(sum_trunc - trunc_x) << ")\n";
        }
        std::cout << "------------------------\n";
    }
}

uint64_t generate_random_bit(int bit) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);
    
    // 掩码保留低 61 位（0x1FFFFFFFFFFFFFFF）
    return dist(gen) & ((1ULL << bit) - 1);
}

// 生成随机 x 和 r（确保 x >= r）
void generate_and_compare_random_cases(int num_cases, int d) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    for (int i = 0; i < num_cases; ++i) {
        uint64_t x = generate_random_bit(62);
        uint64_t r = generate_random_bit(62); // 确保 r <= x
        compare_truncated_sums(x, r, d);
    }
}

void five_truncated_sums(int d) {
    // 计算x-r
    std::vector<uint64_t> alpha_vec(5,0);
    
    for(int i = 0;i<5; i++) {
        alpha_vec[i] = generate_random_bit(61);
    }
    uint64_t sum = 0;
    for(int i = 0;i<5; i++) {
        sum += alpha_vec[i];
    }
    sum = get_top_d_bits(sum, d);
    uint64_t sum_trunc = 0;
    for(int i = 0;i<5; i++) {
        sum_trunc += get_top_d_bits(alpha_vec[i], d);
    }
    
    // 比较结果
    // if((int64_t)(sum_trunc - sum) >=2 || (int64_t)(sum_trunc - sum) <= -2) {
    if(1) {
        if (sum_trunc == sum) {
            std::cout << "Result: EQUAL\n";
        } else if (sum_trunc == sum + 1) {
            std::cout << "Result: Sum is trunc_x + 1\n";
        } else {
            std::cout << "Result: NOT EQUAL (difference: " << (int64_t)(sum_trunc - sum) << ")\n";
        }
        std::cout << "------------------------\n";
    }
}
int main() {
    // 测试用例
    // uint64_t x1 = 0xFFFFFFFFFFFFFFFF; // 64位全1
    // uint64_t r1 = 0x0F0F0F0F0F0F0F0F;
    // compare_truncated_sums(x1, r1, 8);
    
    // uint64_t x2 = 0x123456789ABCDEF0;
    // uint64_t r2 = 0x00FEDCBA98765432;
    // compare_truncated_sums(x2, r2, 16);
    
    // uint64_t x3 = 0x8000000000000000; // 最高位为1，其余为0
    // uint64_t r3 = 0x7FFFFFFFFFFFFFFF; // 比x3小1
    // compare_truncated_sums(x3, r3, 4);
    
    // uint64_t x4 = 0x0000000000000000;
    // uint64_t r4 = 0x0000000000000000;
    // compare_truncated_sums(x4, r4, 8);

    // uint64_t x5 = 0x0000000000000001;
    // uint64_t r5 = 0x0000000000000002;
    // compare_truncated_sums(x5, r5, 8);

    // uint64_t x6 = 2876167038901501374;
    uint64_t x6 = 609594911719872760;
    uint64_t r6 = 9380859347295784293;
    compare_truncated_sums(x6, r6, 56);
    
    // generate_and_compare_random_cases(1000, 56);
    for(int i = 0; i<100; i++) {
        five_truncated_sums(56);
    }
    return 0;
}