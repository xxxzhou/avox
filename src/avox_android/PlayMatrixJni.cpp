// 播放回归矩阵的 Android APK 宿主入口: 手写 JNI 导出自 libavox.so, 供
// platform/android/AvoxJava 的 JNIHelper/PlayMatrixActivity 调起, SurfaceView 出画面。
// 与 tests/playmatrix/HostMain.cpp (console 宿主) 共用同一份用例表与判定口径;
// 本文件由 add_sub_path GLOB 自动编进 avox (仅 ANDROID), 无需改 CMake
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>  // ANativeWindow_fromSurface

#include <filesystem>
#include <string>
#include <vector>

#include "../../../tests/playmatrix/PlayMatrix.hpp"
#include "../../../tests/playmatrix/PlayTee.hpp"
#include "avox/module/AvoxManager.hpp"

using namespace avox;
using namespace avox::playmatrix;

namespace {

std::vector<std::string> pmSplitComma(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t pos = text.find(',', start);
    std::string item =
        text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
    if (!item.empty()) {
      out.push_back(item);
    }
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  return out;
}

}  // namespace

extern "C" JNIEXPORT jint JNICALL
Java_avox_android_library_JNIHelper_pmRunMatrix(
    JNIEnv* env, jclass, jobject surface, jstring jHost, jstring jOutDir,
    jstring jFileH264, jstring jFileH265, jstring jSkip, jobject callback) {
  JavaVM* vm = nullptr;
  env->GetJavaVM(&vm);
  auto toStr = [env](jstring s) {
    std::string out;
    if (s) {
      const char* p = env->GetStringUTFChars(s, nullptr);
      if (p) {
        out = p;
        env->ReleaseStringUTFChars(s, p);
      }
    }
    return out;
  };
  Endpoints ep;
  ep.host = toStr(jHost);
  std::string fileH264 = toStr(jFileH264);
  std::string fileH265 = toStr(jFileH265);
  // 空串 = 本地源没拷进 app 目录, file-* 用例留空会 FAIL, 由宿主自行 --skip
  if (!fileH264.empty()) {
    ep.fileH264 = fileH264;
  }
  if (!fileH265.empty()) {
    ep.fileH265 = fileH265;
  }
  RunOptions opt;
  opt.outDir = toStr(jOutDir);
  opt.skip = pmSplitComma(toStr(jSkip));

  std::vector<PlayCase> cases = buildCases(ep);
  if (!opt.outDir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(opt.outDir, ec);
  }
  // APK 里 JNIHelper.initJNI 已注册模块; console 无 JNI_OnLoad 才需手动, 幂等无害
  AvoxManager::Get().init();
  std::printf("[AVOX][TEST] case=play-matrix-start result=PASS platform=android-apk "
              "host=%s cases=%d\n",
              ep.host.c_str(), (int)cases.size());

  // 判定行回调: pump 线程逐行喂给 Java 横幅, 同时 tee 落盘 pm_log.txt
  jobject cbGlobal = callback ? env->NewGlobalRef(callback) : nullptr;
  jmethodID onLineId = nullptr;
  jmethodID onDoneId = nullptr;
  if (cbGlobal) {
    jclass cls = env->GetObjectClass(callback);
    onLineId = env->GetMethodID(cls, "onLine", "(Ljava/lang/String;)V");
    onDoneId = env->GetMethodID(cls, "onDone", "(I)V");
    env->DeleteLocalRef(cls);
  }
  StdoutTee tee;
  if (!opt.outDir.empty()) {
    tee.onLine = [vm, cbGlobal, onLineId](const std::string& line) {
      if (!cbGlobal || !onLineId) {
        return;
      }
      JNIEnv* e = nullptr;
      bool attached = vm->GetEnv((void**)&e, JNI_VERSION_1_6) != JNI_OK;
      if (attached && vm->AttachCurrentThread(&e, nullptr) != JNI_OK) {
        return;
      }
      jstring s = e->NewStringUTF(line.c_str());
      if (!e->ExceptionCheck()) {
        e->CallVoidMethod(cbGlobal, onLineId, s);
      }
      if (s) {
        e->DeleteLocalRef(s);
      }
      if (e->ExceptionCheck()) {
        e->ExceptionClear();
      }
      if (attached) {
        vm->DetachCurrentThread();
      }
    };
    tee.start(joinPath(opt.outDir, opt.prefix + "log.txt"));
  }
  ANativeWindow* win = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  int code = runAll(cases, (void*)win, opt);
  tee.stop();
  if (win) {
    ANativeWindow_release(win);
  }
  if (cbGlobal && onDoneId) {
    env->CallVoidMethod(callback, onDoneId, (jint)code);
  }
  if (cbGlobal) {
    env->DeleteGlobalRef(cbGlobal);
  }
  return code;
}
