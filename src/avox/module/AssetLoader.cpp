#include "AssetLoader.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <sys/stat.h>
#include <dirent.h>
#endif

#ifdef __APPLE__
#include "avox_ios/IOSHelper.h"
#endif

#ifdef __linux__
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#endif

#include "../Avox.hpp"
#include "AvoxManager.hpp"

namespace avox {

static std::string g_lastError;

#ifdef __ANDROID__
// 递归删除目录及其内容
static void deleteDirectory(const std::string& path) {
  DIR* dir = opendir(path.c_str());
  if (!dir) return;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    std::string fullPath = path + "/" + entry->d_name;
    struct stat st;
    if (stat(fullPath.c_str(), &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        deleteDirectory(fullPath);
      } else {
        remove(fullPath.c_str());
      }
    }
  }
  closedir(dir);
  rmdir(path.c_str());
}
#endif

const char* AssetLoader::getLastError() { return g_lastError.c_str(); }

std::string AssetLoader::getSystemConfigPath() {
#ifdef _WIN32
  char path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
    return std::string(path) + "/avox";
  }
  return "";
#elif defined(__linux__)
  const char* home = getenv("HOME");
  if (!home) {
    struct passwd* pw = getpwuid(getuid());
    if (pw) home = pw->pw_dir;
  }
  if (home) {
    return std::string(home) + "/.local/share/avox";
  }
  return "";
#elif defined(__ANDROID__)
  return getCacheDir();
#elif defined(__APPLE__)
  // iOS: Documents 目录
  NSString* docPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
  return std::string([docPath UTF8String]);
#else
  return "";
#endif
}

bool AssetLoader::isAssetPath(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
    return false;
  }
  return true;
}

std::vector<uint8_t> AssetLoader::loadToMemory(const char* relativePath) {
  g_lastError.clear();
  if (!relativePath || !*relativePath) {
    g_lastError = "null path";
    return {};
  }
  std::string path(relativePath);
  // 先尝试从系统目录读取
  std::string systemPath = getSystemConfigPath();
  if (!systemPath.empty()) {
    std::string fullPath = systemPath + "/" + path;
    auto data = loadFromFile(fullPath);
    if (!data.empty()) {
      return data;
    }
  }
#ifdef __ANDROID__
  auto& androidEnv = AvoxManager::Get().getAppEnv();
  if (androidEnv.assetManager) {
    return loadFromAndroidAssets(relativePath);
  }
#endif
#ifdef __APPLE__
  return loadFromIOSBundle(relativePath);
#endif
  return loadFromFile(getAvoxPath() + "/assets/" + path);
}

bool AssetLoader::saveToFile(const char* relativePath, const std::vector<uint8_t>& data) {
  g_lastError.clear();
  if (!relativePath || !*relativePath) {
    g_lastError = "null path";
    return false;
  }
  if (data.empty()) {
    g_lastError = "empty data";
    return false;
  }
  std::string path(relativePath);
  bool success = false;
  // 辅助函数：创建目录并写入文件
  auto writeToFile = [](const std::string& fullPath, const std::vector<uint8_t>& fileData) -> bool {
    // 创建父目录
    size_t pos = fullPath.find_last_of("/\\");
    if (pos != std::string::npos) {
      std::string dir = fullPath.substr(0, pos);
#ifdef _WIN32
      // Windows: 递归创建目录，跳过驱动器号 (C:)
      size_t idx = 0;
      // 跳过驱动器号部分，如 "C:\"
      if (dir.size() > 2 && dir[1] == ':') {
        idx = 2;
        if (idx < dir.size() && (dir[idx] == '\\' || dir[idx] == '/')) idx++;
      }
      while ((idx = dir.find_first_of("/\\", idx + 1)) != std::string::npos) {
        std::string subDir = dir.substr(0, idx);
        CreateDirectoryA(subDir.c_str(), nullptr);
      }
      CreateDirectoryA(dir.c_str(), nullptr);
#else
      system(("mkdir -p \"" + dir + "\"").c_str());
#endif
    }
    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
    file.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
    return true;
  };
  // Windows/Linux: 双写到系统目录和当前目录
#if defined(_WIN32) || defined(__linux__)
  // 写入系统目录
  std::string systemPath = getSystemConfigPath();
  if (!systemPath.empty()) {
    std::string fullPath = systemPath + "/" + path;
    if (writeToFile(fullPath, data)) {
      success = true;
    }
  }
  // 写入当前目录
  std::string localPath = "assets/" + path;
  if (writeToFile(localPath, data)) {
    success = true;
  }
#elif defined(__ANDROID__)
  std::string systemPath = getSystemConfigPath();
  if (!systemPath.empty()) {
    std::string fullPath = systemPath + "/" + path;
    if (writeToFile(fullPath, data)) {
      success = true;
    }
  }
#elif defined(__APPLE__)
  std::string systemPath = getSystemConfigPath();
  if (!systemPath.empty()) {
    std::string fullPath = systemPath + "/" + path;
    if (writeToFile(fullPath, data)) {
      success = true;
    }
  }
#endif
  if (!success) {
    g_lastError = "failed to save file: " + path;
  }
  return success;
}

std::vector<uint8_t> AssetLoader::loadFromAssets(const char* relativePath) {
  g_lastError.clear();
  if (!relativePath || !*relativePath) {
    g_lastError = "null path";
    return {};
  }
#ifdef __ANDROID__
  auto& androidEnv = AvoxManager::Get().getAppEnv();
  if (androidEnv.assetManager) {
    return loadFromAndroidAssets(relativePath);
  }
#endif
#ifdef __APPLE__
  return loadFromIOSBundle(relativePath);
#endif
  // 只从 assets/ 加载, 不查系统目录
  return loadFromFile(getAvoxPath() + "/assets/" + std::string(relativePath));
}

bool AssetLoader::saveToAssets(const char* relativePath, const std::vector<uint8_t>& data) {
  g_lastError.clear();
  if (!relativePath || !*relativePath) {
    g_lastError = "null path";
    return false;
  }
  if (data.empty()) {
    g_lastError = "empty data";
    return false;
  }
  std::string path(relativePath);
  bool success = false;
  // 创建父目录并写入文件
  auto writeToFile = [](const std::string& fullPath, const std::vector<uint8_t>& fileData) -> bool {
    size_t pos = fullPath.find_last_of("/\\");
    if (pos != std::string::npos) {
      std::string dir = fullPath.substr(0, pos);
#ifdef _WIN32
      size_t idx = 0;
      if (dir.size() > 2 && dir[1] == ':') {
        idx = 2;
        if (idx < dir.size() && (dir[idx] == '\\' || dir[idx] == '/')) idx++;
      }
      while ((idx = dir.find_first_of("/\\", idx + 1)) != std::string::npos) {
        std::string subDir = dir.substr(0, idx);
        CreateDirectoryA(subDir.c_str(), nullptr);
      }
      CreateDirectoryA(dir.c_str(), nullptr);
#else
      system(("mkdir -p \"" + dir + "\"").c_str());
#endif
    }
    std::ofstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
    return true;
  };
  // 只写到 assets/ 目录, 不写系统目录
  std::string localPath = "assets/" + path;
  if (writeToFile(localPath, data)) {
    success = true;
  }
  if (!success) {
    g_lastError = "failed to save file: " + path;
  }
  return success;
}

#ifdef __ANDROID__

std::vector<uint8_t> AssetLoader::loadFromAndroidAssets(const char* assetPath) {
  auto& androidEnv = AvoxManager::Get().getAppEnv();
  AAssetManager* mgr = androidEnv.assetManager;
  if (!mgr) {
    g_lastError = "assetManager is null";
    return {};
  }
  AAsset* asset = AAssetManager_open(mgr, assetPath, AASSET_MODE_BUFFER);
  if (!asset) {
    g_lastError = "failed to open asset: ";
    g_lastError += assetPath;
    return {};
  }
  off_t length = AAsset_getLength(asset);
  if (length <= 0) {
    g_lastError = "asset is empty";
    AAsset_close(asset);
    return {};
  }
  std::vector<uint8_t> data(length);
  int readBytes = AAsset_read(asset, data.data(), length);
  AAsset_close(asset);
  if (readBytes != length) {
    g_lastError = "failed to read asset: ";
    g_lastError += assetPath;
    return {};
  }
  return data;
}

std::string AssetLoader::getCacheDir() {
  auto& androidEnv = AvoxManager::Get().getAppEnv();
  bool bAttach = false;
  JNIEnv* env = AvoxManager::Get().getEnv(&bAttach);
  if (!env) {
    g_lastError = "getCacheDir: failed to get JNIEnv";
    return "";
  }
  jobject context = nullptr;
  if (androidEnv.activity) {
    context = androidEnv.activity;
  } else if (androidEnv.application) {
    context = androidEnv.application;
  }
  if (!context) {
    g_lastError = "getCacheDir: both activity and application are null";
    if (bAttach) AvoxManager::Get().detachThread();
    return "";
  }
  jclass contextClass = env->GetObjectClass(context);
  jmethodID getCacheDir =
      env->GetMethodID(contextClass, "getCacheDir", "()Ljava/io/File;");
  if (!getCacheDir) {
    g_lastError = "getCacheDir: failed to get getCacheDir method";
    env->DeleteLocalRef(contextClass);
    if (bAttach) AvoxManager::Get().detachThread();
    return "";
  }
  jobject cacheDir = env->CallObjectMethod(context, getCacheDir);
  if (!cacheDir) {
    g_lastError = "getCacheDir: cacheDir is null";
    env->DeleteLocalRef(contextClass);
    if (bAttach) AvoxManager::Get().detachThread();
    return "";
  }
  jclass fileClass = env->FindClass("java/io/File");
  jmethodID getAbsolutePath =
      env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
  jstring pathStr = (jstring)env->CallObjectMethod(cacheDir, getAbsolutePath);
  const char* path = env->GetStringUTFChars(pathStr, nullptr);
  std::string result(path);
  env->ReleaseStringUTFChars(pathStr, path);
  env->DeleteLocalRef(pathStr);
  env->DeleteLocalRef(cacheDir);
  env->DeleteLocalRef(fileClass);
  env->DeleteLocalRef(contextClass);
  if (bAttach) {
    AvoxManager::Get().detachThread();
  }
  return result;
}

bool AssetLoader::copyAssetDirRecursive(const std::string& assetDir,
                                         const std::string& targetDir) {
  auto& androidEnv = AvoxManager::Get().getAppEnv();
  AAssetManager* mgr = androidEnv.assetManager;
  if (!mgr) {
    g_lastError = "assetManager is null";
    return false;
  }
  AAssetDir* dir = AAssetManager_openDir(mgr, assetDir.c_str());
  if (!dir) {
    g_lastError = "failed to open asset dir: ";
    g_lastError += assetDir;
    LOGFLF(LogLevel::error, g_lastError.c_str());
    return false;
  }
  // 创建目标目录
  if (mkdir(targetDir.c_str(), 0755) != 0 && errno != EEXIST) {
    g_lastError = "failed to create dir: " + targetDir;
    LOGFLF(LogLevel::error, g_lastError.c_str());
    AAssetDir_close(dir);
    return false;
  }
  int copiedCount = 0;
  const char* filename = nullptr;
  while ((filename = AAssetDir_getNextFileName(dir)) != nullptr) {
    // 跳过隐藏文件/目录
    if (filename[0] == '.') continue;
    std::string assetPath = assetDir + "/" + filename;
    std::string targetPath = targetDir + "/" + filename;
    // 尝试作为文件打开
    AAsset* asset =
        AAssetManager_open(mgr, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (asset) {
      // 是文件，复制内容
      off_t length = AAsset_getLength(asset);
      if (length > 0) {
        std::vector<uint8_t> data(length);
        AAsset_read(asset, data.data(), length);
        std::ofstream file(targetPath, std::ios::binary);
        if (!file.is_open()) {
          g_lastError = "failed to create file: " + targetPath;
          LOGFLF(LogLevel::error, g_lastError.c_str());
          AAsset_close(asset);
          continue;
        }
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!file.good()) {
          g_lastError = "failed to write file: " + targetPath;
          LOGFLF(LogLevel::error, g_lastError.c_str());
          AAsset_close(asset);
          continue;
        }
        copiedCount++;
      }
      AAsset_close(asset);
    } else {
      // 可能是子目录，递归处理
      if (copyAssetDirRecursive(assetPath, targetPath)) {
        copiedCount++;
      }
    }
  }
  AAssetDir_close(dir);
  if (copiedCount == 0) {
    g_lastError = "no files copied from: " + assetDir;
    LOGFLF(LogLevel::error, g_lastError.c_str());
    return false;
  }
  return true;
}

std::string AssetLoader::copyDirectoryToCache(const char* assetDirPath,
                                               const char* cacheSubDir) {
  g_lastError.clear();
  std::string cacheDir = getCacheDir();
  if (cacheDir.empty()) {
    return "";
  }
  std::string targetDir = cacheDir + "/" + cacheSubDir;
  // 检查目录是否已存在且包含实际文件 (非子目录)
  DIR* dir = opendir(targetDir.c_str());
  if (dir) {
    bool hasRealFiles = false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (entry->d_name[0] == '.') continue;
      std::string fullPath = targetDir + "/" + entry->d_name;
      struct stat st;
      if (stat(fullPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        hasRealFiles = true;
        break;
      }
    }
    closedir(dir);
    if (hasRealFiles) {
      return targetDir;
    }
    // 目录存在但没有普通文件 (可能是旧 copyAssetToCache 留下的嵌套子目录)
    // 删除旧目录后重新复制
    deleteDirectory(targetDir);
  }

  // 递归复制
  if (!copyAssetDirRecursive(assetDirPath, targetDir)) {
    return "";
  }

  return targetDir;
}

std::string AssetLoader::copyAssetToCache(const char* assetPath,
                                          const char* cacheSubDir) {
  g_lastError.clear();
  std::string cacheDir = getCacheDir();
  if (cacheDir.empty()) {
    return "";
  }
  std::string cachePath = cacheDir + "/" + cacheSubDir + "/" + assetPath;
  // 检查文件是否已存在
  struct stat st;
  if (stat(cachePath.c_str(), &st) == 0) {
    return cachePath;
  }
  // 递归创建目录
  size_t pos = cacheDir.size();
  while ((pos = cachePath.find('/', pos + 1)) != std::string::npos) {
    std::string dir = cachePath.substr(0, pos);
    mkdir(dir.c_str(), 0755);
  }
  // 从 assets 加载
  auto data = loadToMemory(assetPath);
  if (data.empty()) {
    return "";
  }
  // 写入缓存
  std::ofstream file(cachePath, std::ios::binary);
  if (!file.is_open()) {
    g_lastError = "failed to open cache file: " + cachePath;
    return "";
  }
  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  file.close();
  return cachePath;
}
#endif

#ifdef __APPLE__
std::vector<uint8_t> AssetLoader::loadFromIOSBundle(const char* relativePath) {
  std::string path(relativePath);
  const char* fullPath = nullptr;
  if (path.rfind("models/", 0) == 0 || path.rfind("models\\", 0) == 0) {
    std::string name = path.substr(7);
    fullPath = getModelPath(name.c_str());
  } else if (path.rfind("fonts/", 0) == 0 || path.rfind("fonts\\", 0) == 0) {
    std::string name = path.substr(6);
    fullPath = getFontPath(name.c_str());
  } else if (path.rfind("images/", 0) == 0 || path.rfind("images\\", 0) == 0) {
    std::string name = path.substr(7);
    fullPath = getImagePath(name.c_str());
  } else if (path.rfind("config/", 0) == 0 || path.rfind("config\\", 0) == 0) {
    // config 目录在 avox.bundle 根目录下
    fullPath = getBundlePath(@"avox.bundle", [NSString stringWithUTF8String:path.c_str()]);
  } else {
    fullPath = getModelPath(relativePath);
  }
  if (!fullPath) {
    g_lastError = "iOS: resource not found: ";
    g_lastError += relativePath;
    return {};
  }
  return loadFromFile(fullPath);
}
#endif

std::vector<uint8_t> AssetLoader::loadFromFile(const std::string& fullPath) {
  std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    g_lastError = "failed to open file: " + fullPath;
    return {};
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    g_lastError = "failed to read file";
    return {};
  }
  return buffer;
}

}
