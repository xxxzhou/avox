#pragma once

#include <map>
#include <string>

#include "../VkContext.hpp"
namespace avox {

class VkShader : public VkContextRef {
 private:
  /* data */
  // VkDevice device = VK_NULL_HANDLE;
  VkShaderModule shaderModule = VK_NULL_HANDLE;

 public:
  VkShader(/* args */);
  virtual ~VkShader();

 public:
  VkPipelineShaderStageCreateInfo shaderStage = {};

 private:
  void release();

 public:
  void loadShaderModule(std::string path, VkShaderStageFlagBits shaderFlag =
                                              VK_SHADER_STAGE_COMPUTE_BIT);
};

}