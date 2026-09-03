#pragma once

// ============================================================================
// DSH 附件仓的 avox 侧实现 (core/AttachmentStore.hpp 接缝的实现方)。
//
// 与 dsh 的 attachment-local (packages/attachment/attachment-local/src/store.ts)
// 共用同一物理布局, 于是两边读写同一份对象:
//   <DSH_HOME>/attachments/v1/objects/<sha256 前 2 hex>/<sha256 hex>
// 引用形如 "sha256:<64 hex>", mediaType ∈ image/png|jpeg|webp|gif, 带宽高与字节数。
//
// DSH_HOME 解析顺序与 dsh home-paths 相同: 显式 root > $DSH_HOME (空白视同未设)
// > ~/.dsh。avox 不读 dsh 的 config 文件, 「显式 root」由 compose 注入。
//
// 与 dsh 的已知偏离 (注释逐条说明, 不影响互读):
//   - 目录 fsync 的崩溃持久化仪式省略 (Windows 本就无法 fsync 目录, NTFS 日志卷
//     自理; POSIX 侧 avox 场景少一层仪式可接受)。
//   - load 只校验 sha256 与字节数, 不重新探测宽高: 摘要匹配即字节一致, 而 ref 的
//     宽高是 publish 期从这同一字节解出来的 —— dsh 的 probeImage 也只重推头部字段。
//   - 准入限额 (单图字节/像素/单边, #2629/#2623) 取 dsh 默认值做编译期常量
//     (core/AttachmentStore.hpp 的 kImage*), dsh 侧是 Config 可调的 —— 接入
//     DeploymentLoader 配置化时升级。
//   - 限额拒绝抛 std::runtime_error 带 dsh 错误码字样, 不引入类型化错误码 ——
//     avox 的工具层按 message 如实转达, 语义 (可恢复 + 缩小指引) 与 dsh 对齐。
// ============================================================================

#include <string>

#include "avox_agent/core/AttachmentStore.hpp"

namespace avox {

// DSH_HOME 解析 (与 dsh home-paths/src/index.ts 的 resolveDshHome 对齐)。
// 返回绝对路径; 显式 root 优先, 其次非空白 $DSH_HOME, 最后 ~/.dsh。
std::string resolveDshHome(const std::string& configuredRoot = std::string());

class DshAttachmentStore : public AttachmentStore {
 public:
  // attachmentRoot: 附件根 (即 <DSH_HOME>/attachments/v1); 空串 = 自动解析。
  // limits: 图片准入限额 (agent.json imageLimits{}); 缺省 = dsh 默认值。
  explicit DshAttachmentStore(std::string attachmentRoot = std::string(),
                              ImageAdmissionLimits limits = ImageAdmissionLimits{});

  ImageAttachmentRef publish(const uint8_t* data, size_t size,
                             std::string mediaType,
                             std::optional<std::string> name) override;

  std::vector<uint8_t> load(const ImageAttachmentRef& ref) override;

  ImageAdmissionLimits admissionLimits() const override { return limits; }

 private:
  std::string root;
  ImageAdmissionLimits limits;
};

}
