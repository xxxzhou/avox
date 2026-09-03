#include "avox_agent/adapter/DshAttachmentStore.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>  // _commit / _fileno (MSVC)
#endif

#include "avox/AvoxImage.h"
#include "avox/module/Sha256.hpp"

namespace avox {

namespace fs = std::filesystem;

namespace {

// 暂存名的一次性序号 (跨进程无关紧要 —— 同名覆盖也只是内容寻址的同一字节)。
long long tmpCounter() {
  static std::atomic<long long> counter{0};
  return ++counter;
}

std::FILE* openFile(const fs::path& path, const char* mode) {
#ifdef _WIN32
  std::FILE* file = nullptr;
  fopen_s(&file, path.string().c_str(), mode);
  return file;
#else
  return std::fopen(path.string().c_str(), mode);
#endif
}

// 嗅探容器格式 (只看魔数): dsh 的 detectImage 同样先于任何解码。
//
// 返回空串 = 不是 dsh v1 接受的四种图片容器。
std::string sniffMediaType(const uint8_t* data, size_t size) {
  const auto startsWith = [&](const char* prefix, size_t len) {
    return size >= len && std::memcmp(data, prefix, len) == 0;
  };
  if (startsWith("\x89PNG\r\n\x1a\n", 8)) return "image/png";
  if (startsWith("\xFF\xD8\xFF", 3)) return "image/jpeg";
  if (startsWith("GIF87a", 6) || startsWith("GIF89a", 6)) return "image/gif";
  // WEBP: "RIFF" <size:4> "WEBP"。
  if (startsWith("RIFF", 4) && size >= 12 && std::memcmp(data + 8, "WEBP", 4) == 0) {
    return "image/webp";
  }
  return std::string();
}

std::string hexDigest(const uint8_t* data, size_t size) {
  Sha256 sha;
  sha.update(data, size);
  uint8_t digest[32];
  sha.finalize(digest);
  return hexEncode(std::vector<uint8_t>(digest, digest + 32));
}

// dsh 的 displayName: 剥掉两种路径分隔符 (POSIX 主机不认 \ 是分隔符), 去控制字符,
// 截到 255, 空则视为没有名字 —— 防止客户端本地全路径泄漏进引用与会话日志。
std::optional<std::string> cleanDisplayName(const std::optional<std::string>& name) {
  if (!name.has_value()) return std::nullopt;
  const std::string& value = *name;
  size_t leaf = value.find_last_of("/\\");
  if (leaf == std::string::npos) leaf = 0;
  else ++leaf;
  std::string clean;
  for (size_t i = leaf; i < value.size() && clean.size() < 255; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (c < 0x20 || c == 0x7f) continue;
    clean.push_back(value[i]);
  }
  // 去首尾空白。
  const size_t begin = clean.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return std::nullopt;
  const size_t end = clean.find_last_not_of(" \t\r\n");
  clean = clean.substr(begin, end - begin + 1);
  if (clean.empty()) return std::nullopt;
  return clean;
}

// 从 "sha256:<hex>" 引用里取出裸 hex (dsh 的 ID_PATTERN)。
std::string shaOfRef(const ImageAttachmentRef& ref) {
  static const char* kPrefix = "sha256:";
  const size_t prefixLen = std::strlen(kPrefix);
  const std::string& id = ref.attachmentId;
  if (id.size() != prefixLen + 64 || id.compare(0, prefixLen, kPrefix) != 0) {
    throw std::runtime_error("附件引用格式非法 (应为 sha256:<64 hex>): "
                             + id);
  }
  for (size_t i = prefixLen; i < id.size(); ++i) {
    const char c = id[i];
    const bool hexLower = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!hexLower) {
      throw std::runtime_error("附件引用哈希必须是小写 hex: " + id);
    }
  }
  return id.substr(prefixLen);
}

fs::path objectPathOf(const std::string& root, const std::string& shaHex) {
  return fs::path(root) / "objects" / shaHex.substr(0, 2) / shaHex;
}

// 落盘并冲刷 (Windows 用 _commit 达成 dsh handle.sync() 的语义)。
void writeDurable(const fs::path& path, const uint8_t* data, size_t size) {
  std::FILE* file = openFile(path, "wb");
  if (file == nullptr) {
    throw std::runtime_error("附件暂存文件打不开: " + path.string());
  }
  bool ok = std::fwrite(data, 1, size, file) == size;
  if (ok) ok = std::fflush(file) == 0;
#ifdef _WIN32
  if (ok) ok = _commit(_fileno(file)) == 0;
#endif
  std::fclose(file);
  if (!ok) {
    std::error_code ignored;
    fs::remove(path, ignored);
    throw std::runtime_error("附件暂存文件写入失败: " + path.string());
  }
}

std::vector<uint8_t> readAll(const fs::path& path) {
  std::FILE* file = openFile(path, "rb");
  if (file == nullptr) {
    throw std::runtime_error("附件对象读取失败 (缺失或不可读): " + path.string());
  }
  std::vector<uint8_t> bytes;
  char buffer[65536];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    bytes.insert(bytes.end(), buffer, buffer + read);
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) throw std::runtime_error("附件对象读取失败: " + path.string());
  return bytes;
}

}  // namespace

std::string resolveDshHome(const std::string& configuredRoot) {
  std::string selected = configuredRoot;
  if (selected.empty()) {
    const char* fromEnv = std::getenv("DSH_HOME");
    if (fromEnv != nullptr && *fromEnv != '\0') {
      // 与 dsh 一致: 空白环境值视同未设。
      bool blank = true;
      for (const char* p = fromEnv; *p != '\0'; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
          blank = false;
          break;
        }
      }
      if (!blank) selected = fromEnv;
    }
  }
  if (selected.empty()) {
    const char* home = std::getenv("USERPROFILE");
    if (home == nullptr) home = std::getenv("HOME");
    if (home == nullptr) {
      throw std::runtime_error("无法定位用户主目录 (USERPROFILE/HOME 均未设)");
    }
    selected = std::string(home) + "/.dsh";
  }
  // 展开开头的 ~ / ~/ (dsh expandHomePath 只支持这两种前缀)。
  if (selected == "~") {
    const char* home = std::getenv("USERPROFILE");
    if (home == nullptr) home = std::getenv("HOME");
    if (home == nullptr) throw std::runtime_error("无法展开 ~ (无用户主目录)");
    selected = home;
  } else if (selected.rfind("~/", 0) == 0 || selected.rfind("~\\", 0) == 0) {
    const char* home = std::getenv("USERPROFILE");
    if (home == nullptr) home = std::getenv("HOME");
    if (home == nullptr) throw std::runtime_error("无法展开 ~/ (无用户主目录)");
    selected = std::string(home) + selected.substr(1);
  }
  return fs::weakly_canonical(fs::path(selected)).string();
}

DshAttachmentStore::DshAttachmentStore(std::string attachmentRoot,
                                       ImageAdmissionLimits limits)
    : limits(limits) {
  const std::string base =
      attachmentRoot.empty() ? resolveDshHome() + "/attachments/v1"
                             : std::move(attachmentRoot);
  root = fs::weakly_canonical(fs::path(base)).string();
}

ImageAttachmentRef DshAttachmentStore::publish(const uint8_t* data, size_t size,
                                               std::string mediaType,
                                               std::optional<std::string> name) {
  if (size == 0) throw std::runtime_error("图片字节为空, 拒绝入仓");
  // 单图字节限额 (dsh IMAGE_TOO_LARGE): 已入仓的图会随历史搭每一次后续请求,
  // 在准入线拒, 不等路由 400。dsh saveImageFile 同样先查字节再碰解码。
  if (static_cast<int64_t>(size) > limits.maxImageBytes) {
    throw std::runtime_error("图片超过单图字节上限 (IMAGE_TOO_LARGE): "
                             + std::to_string(size) + " > "
                             + std::to_string(limits.maxImageBytes)
                             + "; 先缩小再入上下文");
  }
  // 声明的媒体类型必须与字节相符 (dsh IMAGE_TYPE_MISMATCH); 指引同 dsh read_image
  // 的 mismatch 改写 —— 重命名或转格式, 对模型是可行动的恢复路径。
  const std::string detected = sniffMediaType(data, size);
  if (detected.empty()) {
    throw std::runtime_error("字节不是 dsh v1 接受的图片容器 "
                             "(png/jpeg/webp/gif)");
  }
  if (detected != mediaType) {
    throw std::runtime_error("声明的媒体类型 " + mediaType
                             + " 与字节的实际容器 " + detected
                             + " 不符 (IMAGE_TYPE_MISMATCH); 按真实格式重命名,"
                               " 或转成 png/jpeg/webp/gif 再读");
  }
  // 解码取宽高: stb 认 png/jpeg/gif, webp 要 avox_opencv 插件在场 —— 解不动就响亮
  // 失败, 不猜测尺寸 (ref 的宽高是校验字段, 错值会让 dsh 读侧判 ATTACHMENT_CORRUPT)。
  fs::path staging = fs::path(root) / "tmp";
  std::error_code ec;
  fs::create_directories(staging, ec);
  if (ec) {
    throw std::runtime_error("附件仓目录建不起来: " + staging.string());
  }
  const fs::path probeFile =
      staging / (std::to_string(tmpCounter()) + "_probe.img");
  writeDurable(probeFile, data, size);
  struct ProbeGuard {
    fs::path path;
    ~ProbeGuard() {
      std::error_code ignored;
      fs::remove(path, ignored);
    }
  } probeGuard{probeFile};
  int64_t width = 0;
  int64_t height = 0;
  {
    IImageBuffer* buffer = createImageBuffer();
    if (buffer == nullptr) throw std::runtime_error("createImageBuffer 失败");
    // createImageBuffer 返回裸指针, 手工释放。
    struct BufferGuard {
      IImageBuffer* ptr;
      ~BufferGuard() { delete ptr; }
    } bufferGuard{buffer};
    if (!loadImagePath(probeFile.string().c_str(), buffer)) {
      throw std::runtime_error("图片解码失败 (webp 需要 avox_opencv 插件): "
                               + mediaType);
    }
    const ImageFormat format = buffer->getImageFormat();
    width = format.width;
    height = format.height;
  }
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("图片解码成功但尺寸非法");
  }
  // 解码像素/单边限额 (dsh IMAGE_TOO_MANY_PIXELS / IMAGE_DIMENSION_TOO_LARGE)。
  // 部署的路由会拒绝边长超限的历史图, 而入仓图随历史搭每一次后续请求 ——
  // 拒在准入线, 指引同 dsh read_image: 缩小后读小图 (可恢复的工具错误, 不进历史)。
  if (width * height > limits.maxImagePixels) {
    throw std::runtime_error("图片解码像素超限 (IMAGE_TOO_MANY_PIXELS): "
                             + std::to_string(width) + "x" + std::to_string(height)
                             + " > " + std::to_string(limits.maxImagePixels)
                             + "; 缩小图片后读小图");
  }
  if (width > limits.maxImageDimension || height > limits.maxImageDimension) {
    throw std::runtime_error("图片单边超限 (IMAGE_DIMENSION_TOO_LARGE): "
                             + std::to_string(width) + "x" + std::to_string(height)
                             + " 超过 " + std::to_string(limits.maxImageDimension)
                             + "px; 缩小图片后读小图");
  }
  const std::string shaHex = hexDigest(data, size);
  const fs::path bucket = fs::path(root) / "objects" / shaHex.substr(0, 2);
  fs::create_directories(bucket, ec);
  if (ec) {
    throw std::runtime_error("附件对象目录建不起来: " + bucket.string());
  }
  const fs::path target = objectPathOf(root, shaHex);
  // 内容寻址幂等: 对象已在 (本进程或 dsh 先前写入) 就不重写, 只做一次摘要核对。
  if (!fs::exists(target, ec)) {
    const fs::path temporary =
        staging / (shaHex + "_" + std::to_string(tmpCounter()));
    writeDurable(temporary, data, size);
    std::error_code renameEc;
    fs::rename(temporary, target, renameEc);
    if (renameEc) {
      // 竞态: 别的进程先到。核对在位对象的摘要, 一致即可。
      std::error_code ignored;
      fs::remove(temporary, ignored);
      if (!fs::exists(target, ec) || ec) {
        throw std::runtime_error("附件对象落位失败: " + target.string());
      }
    }
  }
  const std::vector<uint8_t> stored = readAll(target);
  if (stored.size() != size
      || hexDigest(stored.data(), stored.size()) != shaHex) {
    throw std::runtime_error("附件对象在位内容与摘要不符 (ATTACHMENT_CORRUPT): "
                             + target.string());
  }
  ImageAttachmentRef ref;
  ref.attachmentId = "sha256:" + shaHex;
  ref.mediaType = std::move(mediaType);
  ref.bytes = static_cast<int64_t>(size);
  ref.width = width;
  ref.height = height;
  ref.name = cleanDisplayName(name);
  return ref;
}

std::vector<uint8_t> DshAttachmentStore::load(const ImageAttachmentRef& ref) {
  const std::string shaHex = shaOfRef(ref);
  const std::vector<uint8_t> bytes = readAll(objectPathOf(root, shaHex));
  if (hexDigest(bytes.data(), bytes.size()) != shaHex) {
    throw std::runtime_error("附件对象摘要校验失败 (ATTACHMENT_CORRUPT): "
                             + ref.attachmentId);
  }
  if (ref.bytes >= 0 && static_cast<size_t>(ref.bytes) != bytes.size()) {
    throw std::runtime_error("附件引用的字节数与对象不符: " + ref.attachmentId);
  }
  return bytes;
}

}
