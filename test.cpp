#include <iostream>
#include <cstddef> // 包含 size_t 的定义
#include <memory>

struct Data {
    int value;
    Data(int v) : value(v) { std::cout << "Data(" << value << ") created\n"; }
    ~Data() { std::cout << "Data(" << value << ") destroyed\n"; }
};

int test_size_t(){
    std::cout << "size_t 的字节大小: " << sizeof(size_t) << std::endl;
    std::cout << "size_t 的位数: " << sizeof(size_t) * 8 << std::endl;
}

int main() {
    test_size_t();
    // 创建 shared_ptr
    std::shared_ptr<Data> ptr1 = std::make_shared<Data>(42);  // 引用计数 = 1

    {
        // 共享所有权
        std::shared_ptr<Data> ptr2 = ptr1;  // 引用计数 = 2
        std::cout << "ptr2->value: " << ptr2->value << "\n";
    }  // ptr2 析构，引用计数 = 1

    std::cout << "ptr1->value: " << ptr1->value << "\n";
}  // ptr1 析构，引用计数 = 0，Data 对象被销毁