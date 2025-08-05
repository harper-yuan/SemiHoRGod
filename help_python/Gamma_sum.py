def sum_numbers_from_file(filename):
    """
    读取文件中的数字并计算它们的总和。
    
    参数:
        filename (str): 输入的文件名（.txt 文件）
    
    返回:
        float: 所有数字的总和
    """
    total = 0.0  # 使用浮点数以支持整数和小数
    
    try:
        with open(filename, 'r') as file:
            for line in file:
                line = line.strip()  # 去除首尾空白字符
                if line:  # 确保行不为空
                    try:
                        num = float(line)  # 转换为浮点数（支持整数和小数）
                        total += num
                    except ValueError:
                        print(f"警告: 无法解析行 '{line}'，跳过")
        
        print(f"所有数字的总和: {int(total/3) % (2**64)}")
        return total
    
    except FileNotFoundError:
        print(f"错误: 文件 '{filename}' 不存在")
        return None

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) != 2:
        print("用法: python sum_numbers.py <filename>")
        sys.exit(1)
    
    filename = sys.argv[1]
    sum_numbers_from_file(filename)