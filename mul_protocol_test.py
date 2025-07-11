def findRemainingNumbers3(min_num, mid, max_num):
    remaining = []
    for num in [0, 1, 2, 3, 4]:
        if num != min_num and num != mid and num != max_num:
            remaining.append(num)
    return (remaining[0], remaining[1])

def findRemainingNumbers2(min_num, max_num):
    remaining = []
    for num in [0, 1, 2, 3, 4]:
        if num != min_num and num != max_num:
            remaining.append(num)
    return (remaining[0], remaining[1], remaining[2])

def sortThreeNumbers(a, b, c):
    # 使用列表排序并解包
    sorted_nums = sorted([a, b, c])
    return (sorted_nums[0], sorted_nums[1], sorted_nums[2])

def main():
    for id_ in range(5):
        print(f"##############################id={id_}##############################")
        for i in range(5):
            for j in range(i + 1, 5):
                if i == id_ or j == id_:
                    # 无法生成 α_{xyij}，需要接受消息
                    min_, mid_, max_ = findRemainingNumbers2(i, j)  # 假设这个函数已定义
                    print(f"Received : min={min_}, mid={mid_}, max={max_}, recievier={id_}")
                    # jump_.jumpUpdate(min_, mid_, max_, id_, sizeof(Ring), None)
                else:
                    # 发送消息
                    min_, mid_, max_ = sortThreeNumbers(id_, i, j)  # 假设这个函数已定义
                    other1, other2 = findRemainingNumbers3(min_, mid_, max_)  # 假设这个函数已定义
                    print(f"Send : min={min_}, mid={mid_}, max={max_}, recievier={other1}")
                    print(f"Send : min={min_}, mid={mid_}, max={max_}, recievier={other2}")
                    # jump_.jumpUpdate(min_, mid_, max_, other1, sizeof(Ring), &x_m)
                    # jump_.jumpUpdate(min_, mid_, max_, other2, sizeof(Ring), &x_m)

if __name__ == "__main__":
    main()