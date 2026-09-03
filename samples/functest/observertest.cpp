#include <iostream>
#include <memory>

#include "avox/module/Observer.hpp"

using namespace avox;

class TestObserver {
 public:
  int32_t x = 0;

 public:
  TestObserver() { std::cout << "TestObserver" << std::endl; }
  ~TestObserver() { std::cout << "~TestObserver" << std::endl; }
  void onEvent(int32_t value) {
    std::cout << "onEvent " << x + value << std::endl;
  }
};

int main() {
  Observer<TestObserver> observer;
  std::unique_ptr<TestObserver> o1 = std::make_unique<TestObserver>();
  o1->x = 10;
  std::unique_ptr<TestObserver> o2 = std::make_unique<TestObserver>();
  o2->x = 20;
  std::unique_ptr<TestObserver> o3 = std::make_unique<TestObserver>();
  o3->x = 30;

  observer.addObserver(o1.get());
  observer.addObserver(o2.get());
  observer.addObserver(o3.get());

  observer.dispatch(&TestObserver::onEvent, 1);

  observer.removeObserver(o2.get());
  observer.dispatch(&TestObserver::onEvent, 2);

  return 0;
}
