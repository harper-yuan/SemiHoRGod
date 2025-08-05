from collections import defaultdict
import re

def parse_line(line):
    """解析一行数据，提取i,j,k的值"""
    pattern = r'i\s*=\s*(\d+),\s*j\s*=\s*(\d+),\s*k\s*=\s*(\d+)'
    match = re.search(pattern, line)
    if match:
        return tuple(map(int, match.groups()))
    return None

def check_combinations(filename, target_count=4):
    """检查所有i<j<k组合的出现次数"""
    combination_counts = defaultdict(int)
    
    with open(filename, 'r') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
                
            values = parse_line(line)
            if not values:
                print(f"无法解析行: {line}")
                continue
                
            i, j, k = values
            if i < j < k:  # 确保i<j<k的顺序
                combination_counts[(i, j, k)] += 1
    
    # 分类统计
    valid_combinations = [comb for comb, count in combination_counts.items() if count == target_count]
    invalid_combinations = {comb: count for comb, count in combination_counts.items() if count != target_count}
    
    # 打印结果
    if valid_combinations:
        print(f"以下组合出现 {target_count} 次:")
        for comb in sorted(valid_combinations):
            print(f"({comb[0]}, {comb[1]}, {comb[2]})")
    else:
        print(f"没有组合出现 {target_count} 次。")
    
    if invalid_combinations:
        print(f"\n以下组合未出现 {target_count} 次:")
        for comb, count in sorted(invalid_combinations.items()):
            print(f"({comb[0]}, {comb[1]}, {comb[2]}): 出现 {count} 次")
    else:
        print("\n所有组合均出现 4 次。")

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) != 2:
        print("用法: python check_combinations.py <filename>")
        sys.exit(1)
    
    filename = sys.argv[1]
    check_combinations(filename)