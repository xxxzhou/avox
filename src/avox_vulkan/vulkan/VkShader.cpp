#include "VkShader.hpp"
#include "avox/module/AvoxManager.hpp"
#ifdef __APPLE__
#include "avox_apple/IOSHelper.h"
#endif

namespace avox {

VkShader::VkShader(/* args */) {}

VkShader::~VkShader() { release(); }

void VkShader::release() {
  if (vkDevice && shaderModule) {
    vkDestroyShaderModule(vkDevice, shaderModule, nullptr);
    shaderModule = VK_NULL_HANDLE;
  }
}

void VkShader::loadShaderModule(std::string path,
                                VkShaderStageFlagBits shaderFlag) {
  release();
#ifdef __ANDROID__
  AAssetManager *assetManager = AvoxManager::Get().getAppEnv().assetManager;
  assert(assetManager != nullptr);
  shaderModule = loadShader(assetManager, path.c_str(), vkDevice);
#elif __APPLE__
  // 获取应用的主 bundle
  const char *iospath = getShaderPath(path.c_str());
  if (iospath) {
    shaderModule = loadShader(iospath, vkDevice);
  } else {
    log(LogLevel::warn, "file: " + path + " load shader failed");
  }
#else
  std::string fullPath = getAvoxPath() + "/assets/" + path;
  shaderModule = loadShader(fullPath.c_str(), vkDevice);
#endif
  LOGASSERT(shaderModule != VK_NULL_HANDLE,
            "file: " + path + " load shader failed");
  shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStage.stage = shaderFlag;
  shaderStage.pName = "main";
  shaderStage.module = shaderModule;
}

}