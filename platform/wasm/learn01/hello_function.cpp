#include <emscripten/emscripten.h>
#include <math.h>

#include <iostream>
#include <thread>

extern "C" {

// 经测试 EMSCRIPTEN_KEEPALIVE与命令EXPORTED_RUNTIME_METHODS=ccall就可以导出函数
EMSCRIPTEN_KEEPALIVE int int_sqrt(int x) {
  std::cout << "x:" << x << std::endl;
  std::thread xx = std::thread([]() {
    int32_t i = 0;
    while (i < 10) {
      /* code */
      std::cout << "x:" << i << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "hello" << std::endl;
  });
  std::thread xx2 = std::thread([]() {
    int32_t i = 0;
    while (i < 10) {
      /* code */
      std::cout << "x2:" << i << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "hello2" << std::endl;
  });
  std::cout << "x1:" << x << std::endl;
  xx.join();
  xx2.join();
  std::cout << "x3:" << x << std::endl;
  return sqrt(x);
}

int main() {
  int_sqrt(10);
  return 0;
}
}

// emcc hello_function.cpp -o function.html -s EXPORTED_FUNCTIONS=_int_sqrt -s
// EXPORTED_RUNTIME_METHODS=ccall,cwrap
