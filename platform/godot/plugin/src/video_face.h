#pragma once

#include <godot_cpp/classes/node.hpp>

#include <avox/AvoxAvatar.h>  // IVideoFace / IVideoFaceOb / VideoFaceType / VideoFaceDesc

#include "render_source.h"  // IRenderSource

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace avox {
class ISurfaceRender;
class IImageBuffer;
}

namespace godot {

class VideoSurfaceOb;  // 实现放 video_face.cpp (ISurfaceRenderOb, onRender 节流 feed)

// avox_avatar mediapipe (IVideoFace) → face_blendshape/face_landmarks/face_desc/face_ready/error
// 视频帧(经 IRenderSource 的 ISurfaceRender enableImage 零拷贝回读 rgba8)进 →
// ARKit52 blendshape(52×float[0,1]) + 478 landmarks 出, 经信号投递主线程驱动 avatar。
// 对称 FaceNode(音频驱动): 模型后台线程加载, 跨线程回调经 call_deferred; 模型对象级常驻。
// 数据源可切换: bind_source 绑任意 IRenderSource(MediaPlayer/SourcePlayer/MediaRecorder)。
// 约束: enableImage 仅 Vulkan/GPU 直通模式有帧(CPU 回退为空实现)。
class VideoFaceNode : public Node {
    GDCLASS(VideoFaceNode, Node)

public:
    VideoFaceNode() = default;
    ~VideoFaceNode();

    void bindSource(Node* p_source);  // 绑定渲染数据源(IRenderSource); 切换源旧 tap 自动解绑
    void start();                     // 建 face + image buf + observer; 后台加载模型, 就绪发 face_ready
    void stop();                      // 停止(解 tap + flush + join worker, 模型常驻); 幂等
    bool loading();                   // 模型/任务是否就绪(avox loading 语义 = running)
    void setModelLevel(int p_level);  // ModelLevel: 0=none 1=mini 2=base 3=high(默认 base)
    int getModelLevel() const;
    void setReadbackSize(int p_w, int p_h);  // enableImage 回读分辨率(默认 640×360; 须与源同比例)
    int getReadbackWidth() const;
    int getReadbackHeight() const;
    void setFeedEvery(int p_n);              // feed 节流(每 N 个渲染帧喂一帧, 默认 4)
    int getFeedEvery() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class VideoFaceOb;
    friend class VideoSurfaceOb;
    std::unique_ptr<avox::IVideoFace> face;     // 默认 deleter: ~VideoFace 内 stop+releaseEngine
    std::unique_ptr<avox::IVideoFaceOb> faceOb;
    avox::IImageBuffer* imgBuf = nullptr;        // createImageBuffer 建, 持有到 stop(delete)
    VideoSurfaceOb* surfaceOb = nullptr;        // 持有(裸指针, 节点生命周期)
    IRenderSource* renderSource = nullptr;      // 弱引用, 数据源节点拥有(存活由 sourceInstanceId 守卫)
    uint64_t sourceInstanceId = 0;              // renderSource 所属节点 id(绑定时缓存, _process 查存活)
    avox::ISurfaceRender* tappedSr = nullptr;    // 当前已挂 tap 的 surface(检测重建/切换)
    uint64_t tappedInstanceId = 0;              // tappedSr 持有者 id(untap 存活重校验, 防 UAF)
    bool tapWanted = false;                     // start 后置 true, _process 据此挂 tap
    int modelLevel = 2;                         // ModelLevel::base
    int feedEvery = 4;
    int rbW = 640;
    int rbH = 360;
    std::atomic<bool> loadStop{false};
    std::atomic<int64_t> ptsCounter{0};
    std::thread loadThread;
    void untap();  // 解当前 tappedSr 的 observer + disableImage
};

}  // namespace godot
