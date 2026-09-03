#include "VkH265Decoder.hpp"

namespace avox {

// struct VkH265DecoderReg {
//   VkH265DecoderReg() {
//     VCodecDesc vulkanDesc = {};
//     // 初始化 faadDesc 的相关信息，例如名称、是否支持硬件加速等
//     strcpy((char*)vulkanDesc.name, "vulkan");
//     vulkanDesc.bHardware = true;
//     AvoxManager::Get().vDecoders.regInitFunc(
//         VCodecId::h265, vulkanDesc,
//         []() -> VideoDecoder* { return new VkH265Decoder(); });
//     // std::cout << "faad init" << std::endl;
//   }
// };

// // 静态变量初始化在attach_process,main之前
// static VkH265DecoderReg vkH265DecoderReg;

// VkH265Decoder::VkH265Decoder() {}

// VkH265Decoder::~VkH265Decoder() {}

// bool VkH265Decoder::onParse() {
//   const auto& configs = trackContext->getConfigPackets();
//   parseConfigs(configs);
//   if (!bParse) {
//     return false;
//   }
//   profileInfo = {VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR, nullptr};
//   profileInfo.videoCodecOperation =
//       VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
//   profileInfo.chromaSubsampling = getVkYUVType(yuvType);
//   profileInfo.chromaBitDepth = getVkBitDepth(uvBitDepth);
//   profileInfo.lumaBitDepth = getVkBitDepth(yBitDepth);

//   imageExtent = {(uint32_t)width, (uint32_t)height, 1};
//   return true;
// }

}
