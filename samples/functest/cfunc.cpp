#include <iostream>
#include <memory>

class B;

template <typename T>
class TPIpeNode {
public:
    TPIpeNode(T* layer_) : nlayer(layer_) {}
    T* operator->() { return nlayer; }
private:
    T* nlayer;
};

class B {
public:
    void bMethod() { std::cout << "This is B's method" << std::endl; }
};

int main() {
    auto bObj = std::make_shared<B>(); // 使用智能指针管理B的生命周期
    std::shared_ptr<TPIpeNode<B>> a = std::make_shared<TPIpeNode<B>>(bObj.get());
    // a->bMethod(); 
    return 0;
}