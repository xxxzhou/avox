// TPIpeNode 指针包装单元测试
// 从 samples/functest/cfunc.cpp 迁来: 原文件是个 24 行的语法草稿, 关键调用
// `a->bMethod()` 还是注释掉的 (shared_ptr 要先解引用才能用包装器的 operator->),
// 这里补上并断言转发确实落到了被包装对象上。
#include <doctest.h>

#include <memory>

class B {
 public:
  int32_t calls = 0;
  void bMethod() { ++calls; }
};

template <typename T>
class TPIpeNode {
 public:
  TPIpeNode(T* layer_) : nlayer(layer_) {}
  T* operator->() { return nlayer; }

 private:
  T* nlayer;
};

TEST_CASE("TPIpeNode: operator-> 转发到被包装对象") {
  auto bObj = std::make_shared<B>();

  TPIpeNode<B> node(bObj.get());
  node->bMethod();
  CHECK(bObj->calls == 1);

  // 原样例注释掉的写法: shared_ptr<TPIpeNode> 要 (*ptr)->, 直接 ptr-> 找的是
  // TPIpeNode 自己的成员, 编译不过
  std::shared_ptr<TPIpeNode<B>> holder =
      std::make_shared<TPIpeNode<B>>(bObj.get());
  (*holder)->bMethod();
  CHECK(bObj->calls == 2);

  // 两个包装器指向同一对象, 是同一份状态
  node->bMethod();
  CHECK(bObj->calls == 3);
}
