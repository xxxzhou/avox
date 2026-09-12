
#include <thread>
#include <cstring>
#include "avox/AvoxPlayer.h"

#ifdef __ONLY_LINUX__
#include "avox_linux/LinuxHelper.h"
#endif

// Linux X11 播放宿主: 窗口由宿主创建, SDK 内部 Vulkan 管线渲染上屏
// 用法: linuxtest [url]  (本地文件走 ffmpeg IO, 网络流走 zlmediakit)
using namespace avox;

int main(int argc, char** argv) {
    ILinuxSurface *surface = createLinuxSurface(1280, 720, "avox");
    if (!surface) {
        return -1;
    }
    IMediaPlayer *mp = createMediaPlayer();
    const char* url = argc > 1 ? argv[1] : nullptr;
    if (url) {
        // 简单区分: 以 rtsp/rtmp/srt 等协议头开头的网络流走 zlmediakit
        const char* proto = strstr(url, "://");
        bool bNetwork = proto && (proto - url) > 1 &&
                        strncmp(url, "file", proto - url) != 0;
        if (bNetwork) {
            mp->setIoPlan(IoPlan::zlmediakit);
        } else {
            mp->setIoPlan(IoPlan::ffmpeg);
        }
        mp->open(url);
    }
    mp->getSurfaceRender()->setSurface(surface);
    while (!surface->shouldClose()) {
        surface->pollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mp->close();
    delete surface;
  return 0;
}
