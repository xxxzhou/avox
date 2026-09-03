#pragma once

// ============================================================================
// 附件仓接缝 (dsh attachment 模型的 avox 侧端口)。
//
// dsh 的图片消息不携带字节, 只携带 ImageAttachmentRef (sha256 内容寻址); 字节存放在
// 附件仓 (DSH_HOME/attachments/v1/objects/...)。要让两边的日志对图片的认知一致, avox
// 必须用同一个模型: 消息进日志前先 publish 换引用, 构模型请求时再 load 回字节。
//
// 放在 core 的只是一个纯虚接口 —— 字节的编解码 (经 IImageProc 取宽高)、哈希、原子写
// 全是 avox 设施关切, 归 adapter/DshAttachmentStore (compose 注入)。core 的 codec 只
// 编解码 ref (纯数据, 无 I/O), 因此不 include 本文件; 持有它的是生产者 (构
// ImageBlock 的入口) 与 LlmProviderAdapter (ref -> data URL)。
//
// 实现约定:
//   - publish 幂等: 同一字节重复 publish 返回同一引用, 不重写对象。
//   - load 找不到对象时抛 std::runtime_error (响亮失败 —— 静默丢图等于对模型撒谎)。
//   - 实现可以是 fake (测试) —— 接口没有持久化假设。
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "SessionTypes.hpp"

namespace avox {

// —— 图片准入限额 (dsh attachment-local DEFAULT_* 同源, #2629/#2623) ——
//
// dsh 的 ImageAttachmentLimits 是部署可调的 (Config 覆盖); avox 的缺省值取 dsh 默认,
// 可经 agent.json 顶层 "imageLimits":{...} 覆盖 (键名与 dsh Config 同义)。
//
// 已入仓的图会随会话历史搭每一次后续请求 (dsh 注释原话 "rides every later
// request"), 部署的路由会拒绝边长超 2000px 的历史图 —— 所以在准入线就拒, 而不是
// 等路由 400。
constexpr int64_t kImageMaxBytes = 7LL * 1024 * 1024 / 2;        // 3.5 MB (dsh DEFAULT_MAX_IMAGE_BYTES)
constexpr int64_t kImageMaxPixels = 40'000'000;                  // 宽×高 (dsh DEFAULT_MAX_IMAGE_PIXELS)
constexpr int64_t kImageMaxDimension = 2000;                     // 单边 px (dsh DEFAULT_MAX_IMAGE_DIMENSION)
// 每条消息的图片数/总字节上限 (dsh DEFAULT_MAX_IMAGES_PER_MESSAGE/MESSAGE_IMAGE_BYTES)。
// 执行点是 user 侧多图提交入口 (IAgentSession::followupImages), 对应 dsh saveImages
// 的批量准入。
constexpr int64_t kImagesMaxPerMessage = 20;
constexpr int64_t kImagesMaxMessageBytes = 100LL * 1024 * 1024;

// 准入限额的运行期载体 (缺省即上表编译期默认)。
struct ImageAdmissionLimits {
  int64_t maxImageBytes = kImageMaxBytes;
  int64_t maxImagePixels = kImageMaxPixels;
  int64_t maxImageDimension = kImageMaxDimension;
  int64_t maxImagesPerMessage = kImagesMaxPerMessage;
  int64_t maxMessageImageBytes = kImagesMaxMessageBytes;
};

class AttachmentStore {
 public:
  virtual ~AttachmentStore() = default;

  // 当前生效的准入限额 (多图提交入口按它做批量预检)。缺省即编译期默认。
  virtual ImageAdmissionLimits admissionLimits() const {
    return ImageAdmissionLimits{};
  }

  // 把一段图片编码字节存入附件仓, 返回内容寻址引用。
  //
  // mediaType: image/png | image/jpeg | image/webp | image/gif。
  // name: 展示名 (不含本地路径信息), 可省。
  virtual ImageAttachmentRef publish(const uint8_t* data, size_t size,
                                     std::string mediaType,
                                     std::optional<std::string> name) = 0;

  // 取回附件字节 (publish 的原样字节)。对象缺失时抛 std::runtime_error。
  virtual std::vector<uint8_t> load(const ImageAttachmentRef& ref) = 0;
};

}
