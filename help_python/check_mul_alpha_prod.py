def normalize_string(s):
    """规范化字符串：前两个元素排序，后两个元素排序"""
    parts = s.split(',')
    if len(parts) != 4:
        return s  # 如果不是4部分，直接返回（可根据需求调整）
    # 前两个元素排序
    first_part = sorted(parts[:2])
    # 后两个元素排序
    second_part = sorted(parts[2:])
    return ','.join(first_part + second_part)

NUM_PARTIES = 7
A = []

for i in range(NUM_PARTIES):
    for j in range(i+1, NUM_PARTIES):
        for k in range(NUM_PARTIES):
            for l in range(k+1, NUM_PARTIES):
                A.append(str(i)+','+str(j)+','+str(k)+','+str(l))

# 读取文件并收集所有行
file_path = "output.txt"  # 替换为你的文件路径
seen_normalized = set()

with open(file_path, 'r') as file:
    for line in file:
        cleaned_line = line.strip()
        if cleaned_line:
            normalized = normalize_string(cleaned_line)
            seen_normalized.add(normalized)

for i in range(NUM_PARTIES):
    for j in range(i+1, NUM_PARTIES):
        seen_normalized.add(str(i)+','+str(j)+','+str(i)+','+str(j))
# 检查A中哪些字符串未被匹配（考虑规范化）
remaining_elements = []
for s in A:
    normalized_s = normalize_string(s)
    if normalized_s not in seen_normalized:
        remaining_elements.append(s)

# 打印结果
print("未被匹配的原始字符串（考虑规范化）：")
for elem in remaining_elements:
    print(elem)