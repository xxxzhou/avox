#include <chrono>
#include <thread>

#include "avox/AvoxPlayer.h"
#include "avox/player/Clock.hpp"
#include "avox_vulkan/VkCommon.hpp"
#include "avox_vulkan/VkContext.hpp"
#include "avox_vulkan/VkTemplate.hpp"

using namespace avox;

struct Test {
  int32_t a = 0;
  int32_t b = 0;
  std::unique_ptr<int32_t> c = nullptr;
};

void test1() {
  Test t = {};
  t.c = std::make_unique<int32_t>(10);
  // Test t1 = t;
}

int main() {
  // vkInit();
  // VKLayerProps layerProps = {};
  // layerProps.dump();
  Clock c = {};
  c.update(0);
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    log(LogLevel::info, "clock:", c.clock());
  }

  return 0;
}
