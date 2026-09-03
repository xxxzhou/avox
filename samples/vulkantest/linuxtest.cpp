
#include <thread>
#include "avox/AvoxPlayer.h"

#ifdef __ONLY_LINUX__
#include "avox_linux/LinuxHelper.h"
#endif

using namespace avox;

int main() {
    ILinuxSurface *surface = createLinuxSurface(1280, 720, "avplay");
    if (!surface) {
        return -1;
    }
    IMediaPlayer *mp = createMediaPlayer();
    mp->setIoPlan(IoPlan::zlmediakit);
    mp->open("rtsp://192.168.1.100/live/test");
    mp->getSurfaceRender()->setSurface(surface);
    while (!surface->shouldClose()) {
        surface->pollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mp->close();
    delete surface;
  return 0;
}

