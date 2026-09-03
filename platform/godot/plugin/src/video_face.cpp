// video_face.cpp — 视频驱动面部追踪 Godot 封装 (VideoFaceNode)
// 行为基准: FaceNode (face.cpp). 视频帧(经 IRenderSource 的 ISurfaceRender enableImage
// 零拷贝回读 rgba8)进 → ARKit52 blendshape + 478 landmarks 出 (face_blendshape/face_landmarks)。
//
// 线程模型:
//   VideoSurfaceOb::onRender 在 avox 渲染线程 → 节流 feed imgBuf 给 IVideoFace (非阻塞入队);
//   VideoFaceOb 回调在 avox_avatar worker 线程 → 52×float / landmarks 深拷贝成 Godot 数组,
//   经 call_deferred 投递主线程。模型加载在 loadThread (秒级), 完成发 face_ready。
//
// 数据源生命周期安全: 数据源节点(MediaPlayer 每次 play)会重建/释放其 ISurfaceRender,
// 故缓存的 tappedSr 可能悬空。untap 仅在"持有该 surface 的源仍存活且仍报告同一指针"时才
// remove/disable(否则 surface 已失效, observer 随之失效不再被回调)→ 无 UAF。

#include "video_face.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>  // ObjectDB (节点存活判定; 本版无 is_instance_valid)
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <avox/AvoxLayer.h>  // ISurfaceRender / ISurfaceRenderOb / addSurfaceRenderOb
#include <avox/AvoxVideo.h>  // createImageBuffer / IImageBuffer / ImageFormat / ImageType
#include <avox/AvoxBase.h>  // ModelLevel

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace godot {

namespace {

// 宽字面量 → Godot String (UTF-8)。插件无 /utf-8, 窄字面量是本地编码 (Win=GBK),
// 是插件内中文文案唯一可靠写法 (移植自 face.cpp)。
String wstrToStr(const wchar_t *ws) {
    if (!ws || !*ws) return String();
#ifdef _WIN32
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return String();
    std::string s((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], len, nullptr, nullptr);
    return String::utf8(s.c_str());
#else
    std::string u8;
    for (const wchar_t *p = ws; *p; ++p) {
        uint32_t c = (uint32_t)*p;
        if (c < 0x80) u8 += char(c);
        else if (c < 0x800) { u8 += char(0xC0 | (c >> 6)); u8 += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { u8 += char(0xE0 | (c >> 12)); u8 += char(0x80 | ((c >> 6) & 0x3F)); u8 += char(0x80 | (c & 0x3F)); }
        else { u8 += char(0xF0 | (c >> 18)); u8 += char(0x80 | ((c >> 12) & 0x3F)); u8 += char(0x80 | ((c >> 6) & 0x3F)); u8 += char(0x80 | (c & 0x3F)); }
    }
    return String::utf8(u8.c_str());
#endif
}

// Node 是否仍存活: 经缓存的 instance id 查 ObjectDB。本版 godot-cpp 无 is_instance_valid,
// 且不能对可能已释放的 Node* 调方法 → 缓存 id, 用 object_get_instance_from_id 判活。
bool nodeAlive(uint64_t id) {
    return id != 0 && ObjectDB::get_instance(id) != nullptr;
}

}  // namespace

// ============================================================
// VideoFaceOb (blendshape/landmarks 观察者, 实现 IVideoFaceOb)
// ============================================================

// 回调在 avox_avatar worker 线程: raw 数据深拷贝成 Godot 数组 (raw 仅回调内有效),
// 经 call_deferred 投递主线程。对齐 FaceOb。
class VideoFaceOb : public avox::IVideoFaceOb {
public:
    VideoFaceNode *owner = nullptr;
    void onFaceDesc(const avox::VideoFaceDesc &desc) override {
        if (owner)
            owner->call_deferred("emit_signal", "face_desc",
                                 desc.fps, desc.blendshapeCount, desc.landmarkCount);
    }
    void onFaceBlendshape(const avox::AvoxData &raw52, int64_t pts, bool final) override {
        if (!owner) return;
        PackedFloat32Array arr;
        int n = raw52.size / int32_t(sizeof(float));
        if (n > 0 && raw52.data) {
            arr.resize(n);
            std::memcpy(arr.ptrw(), raw52.data, raw52.size);
        }
        owner->call_deferred("emit_signal", "face_blendshape", arr, pts, final);
    }
    void onFaceLandmarks(const avox::AvoxData &rawPts, int32_t count,
                         int32_t imgW, int32_t imgH, int64_t pts) override {
        if (!owner) return;
        PackedVector2Array arr;
        if (count > 0 && imgW > 0 && imgH > 0 && rawPts.data &&
            rawPts.size >= count * 2 * int32_t(sizeof(float))) {
            const float *p = reinterpret_cast<const float *>(rawPts.data);
            arr.resize(count);
            Vector2 *dst = arr.ptrw();
            float iw = float(imgW), ih = float(imgH);
            for (int i = 0; i < count; ++i) {
                dst[i] = Vector2(p[i * 2] / iw, p[i * 2 + 1] / ih);  // 归一化 [0,1]
            }
        }
        owner->call_deferred("emit_signal", "face_landmarks", arr, imgW, imgH, pts);
    }
    void onFaceError(const char *err) override {
        if (owner && err)
            owner->call_deferred("emit_signal", "error", String::utf8(err));
    }
};

// ============================================================
// VideoSurfaceOb (ISurfaceRenderOb, onRender 节流 feed)
// ============================================================

// onRender 在 avox 渲染线程 (每解码帧触发, graph->run 已 vkWaitForFences, 同帧可读 imgBuf)。
// 节流: 3 个 ONNX(det+lm+bs) 很重, 喂太密会饿死渲染线程; RingBuffer 丢老帧, 跳帧只降刷新率。
class VideoSurfaceOb : public avox::ISurfaceRenderOb {
public:
    VideoFaceNode *owner = nullptr;
    int counter = 0;
    void onRender() override {
        if (!owner) return;
        // stop 后(tapWanted=false)或 face 未就绪则不喂; feed 非阻塞入队 face worker。
        if (!owner->tapWanted || !owner->face || !owner->imgBuf) return;
        if (owner->feedEvery > 1 && (++counter % owner->feedEvery) != 0) return;
        owner->face->feed(owner->imgBuf, owner->ptsCounter.fetch_add(1, std::memory_order_relaxed) + 1);
    }
};

// ============================================================
// VideoFaceNode
// ============================================================

VideoFaceNode::~VideoFaceNode() {
    stop();
    if (face && faceOb) avox::removeVideoFaceOb(face.get(), faceOb.get());  // 观察者先于 face 释放
    delete imgBuf;    // 须在 untap 的 disableImage 之后 (生命周期要求)
    delete surfaceOb;
}

void VideoFaceNode::bindSource(Node *p_source) {
    IRenderSource *rs = dynamic_cast<IRenderSource *>(p_source);
    if (!rs) {
        call_deferred("emit_signal", "error",
                      wstrToStr(L"数据源须为 MediaPlayer/SourcePlayer/MediaRecorder"));
        return;
    }
    sourceInstanceId = p_source->get_instance_id();
    renderSource = rs;
    // 实际解旧 tap / 挂新 tap 留给 _process (数据源此刻可能未 open, surface 还没建)。
}

void VideoFaceNode::start() {
    if (!face) {
        face.reset(avox::createVideoFace(avox::VideoFaceType::mediapipe));
        if (!face) {
            call_deferred("emit_signal", "error",
                          wstrToStr(L"创建面部推理器失败 (avox_avatar 插件未注册? 模型未下载?)"));
            return;
        }
        face->setModelLevel(static_cast<avox::ModelLevel>(modelLevel));
        faceOb = std::make_unique<VideoFaceOb>();
        static_cast<VideoFaceOb *>(faceOb.get())->owner = this;
        avox::addVideoFaceOb(face.get(), faceOb.get());
        // enableImage 回读缓冲 (rgba8, 调用方持有, 生命周期到 disableImage 之后)
        imgBuf = avox::createImageBuffer();
        avox::ImageFormat fmt = {};
        fmt.width = rbW;
        fmt.height = rbH;
        fmt.imageType = avox::ImageType::rgba8;
        imgBuf->setImageFormat(fmt);
        // onRender 节流 feed 的 surface 观察者
        surfaceOb = new VideoSurfaceOb();
        surfaceOb->owner = this;
        tapWanted = true;  // _process 据此在数据源 surface 就绪后挂 tap
        // 首次创建才开后台加载线程 (模型秒级, 不阻塞主线程)。
        // loading()==running(): face->start() 拉起 worker, 模型在 worker 内 initEngine;
        // worker 起来后发 face_ready (对齐 FaceNode; 失败由 onFaceError/error 兜底)。
        loadStop.store(false);
        loadThread = std::thread([this]() {
            face->start();
            auto t0 = std::chrono::steady_clock::now();
            while (!loadStop.load() && !face->loading() &&
                   std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (loadStop.load()) return;
            if (face->loading()) {
                call_deferred("emit_signal", "face_ready");
            } else {
                call_deferred("emit_signal", "error",
                              wstrToStr(L"面部模型加载超时 (用 fetch_assets 下载 mediapipe 模型)"));
            }
        });
    } else {
        // 后续轮: 幂等重启任务 + 重挂 tap, 不重载模型
        tapWanted = true;
        face->start();
    }
}

void VideoFaceNode::untap() {
    if (!tappedSr) return;
    // 仅当持有该 surface 的源仍存活且仍报告同一指针时才安全 remove/disable;
    // 否则源已重建/释放 surface (如 MediaPlayer 每次 play), observer 随之失效不再被回调 → 不触碰, 避免悬空。
    // 存活判定经缓存的 instance id 查 ObjectDB (本版 godot-cpp 无 is_instance_valid),
    // 不直接触碰可能已释放的 Node* → 防 UAF。
    if (nodeAlive(tappedInstanceId)) {
        IRenderSource *ownerSrc = dynamic_cast<IRenderSource *>(ObjectDB::get_instance(tappedInstanceId));
        if (ownerSrc && ownerSrc->getSurfaceRenderRaw() == tappedSr && surfaceOb) {
            avox::removeSurfaceRenderOb(tappedSr, surfaceOb);
            tappedSr->disableImage();
        }
    }
    tappedSr = nullptr;
    tappedInstanceId = 0;
}

void VideoFaceNode::stop() {
    tapWanted = false;
    loadStop.store(true);
    if (loadThread.joinable()) loadThread.join();
    untap();  // 先停喂 (移除 surface observer), 再 flush face worker
    if (face) face->stop();  // flush 已入队帧 + join worker; 模型常驻
}

bool VideoFaceNode::loading() {
    return face ? face->loading() : false;
}

void VideoFaceNode::setModelLevel(int p_level) {
    if (p_level >= 0 && p_level <= 3) modelLevel = p_level;
    if (face) face->setModelLevel(static_cast<avox::ModelLevel>(modelLevel));
}

int VideoFaceNode::getModelLevel() const {
    return modelLevel;
}

void VideoFaceNode::setReadbackSize(int p_w, int p_h) {
    if (p_w > 0) rbW = p_w;
    if (p_h > 0) rbH = p_h;
    if (imgBuf) {
        avox::ImageFormat fmt = {};
        fmt.width = rbW;
        fmt.height = rbH;
        fmt.imageType = avox::ImageType::rgba8;
        imgBuf->setImageFormat(fmt);
        // 已挂 tap: 重新 enableImage 触发图按新尺寸重建 (主线程调用, 安全; 勿在 onRender 调)。
        if (tappedSr && nodeAlive(tappedInstanceId)) {
            IRenderSource *ownerSrc = dynamic_cast<IRenderSource *>(ObjectDB::get_instance(tappedInstanceId));
            if (ownerSrc && ownerSrc->getSurfaceRenderRaw() == tappedSr) {
                tappedSr->enableImage(imgBuf);
            }
        }
    }
}

int VideoFaceNode::getReadbackWidth() const { return rbW; }
int VideoFaceNode::getReadbackHeight() const { return rbH; }

void VideoFaceNode::setFeedEvery(int p_n) {
    if (p_n >= 1) feedEvery = p_n;
}

int VideoFaceNode::getFeedEvery() const { return feedEvery; }

void VideoFaceNode::_notification(int p_what) {
    switch (p_what) {
    case NOTIFICATION_READY:
        set_process(true);  // 接通 _process: 等数据源 surface 就绪后挂/换 tap
        break;
    case NOTIFICATION_PROCESS: {
        // 数据源节点被释放则解绑(经 instance id 查存活, 不触碰可能已释放的 Node*)
        if (sourceInstanceId != 0 && !nodeAlive(sourceInstanceId)) {
            sourceInstanceId = 0;
            renderSource = nullptr;
        }
        if (!tapWanted) break;
        avox::ISurfaceRender *sr = renderSource ? renderSource->getSurfaceRenderRaw() : nullptr;
        if (sr == tappedSr) break;  // 未变(同为 null 或同一有效指针)
        untap();                     // 安全解旧(存活校验)
        if (sr && imgBuf && surfaceOb) {
            sr->enableImage(imgBuf);
            avox::addSurfaceRenderOb(sr, surfaceOb);
            tappedSr = sr;
            tappedInstanceId = sourceInstanceId;  // 缓存当前 tap 持有者的 instance id
        }
        break;
    }
    case NOTIFICATION_EXIT_TREE:
        stop();
        break;
    }
}

void VideoFaceNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bind_source", "source"), &VideoFaceNode::bindSource);
    ClassDB::bind_method(D_METHOD("start"), &VideoFaceNode::start);
    ClassDB::bind_method(D_METHOD("stop"), &VideoFaceNode::stop);
    ClassDB::bind_method(D_METHOD("loading"), &VideoFaceNode::loading);
    ClassDB::bind_method(D_METHOD("set_model_level", "level"), &VideoFaceNode::setModelLevel);
    ClassDB::bind_method(D_METHOD("get_model_level"), &VideoFaceNode::getModelLevel);
    ClassDB::bind_method(D_METHOD("set_readback_size", "width", "height"), &VideoFaceNode::setReadbackSize);
    ClassDB::bind_method(D_METHOD("get_readback_width"), &VideoFaceNode::getReadbackWidth);
    ClassDB::bind_method(D_METHOD("get_readback_height"), &VideoFaceNode::getReadbackHeight);
    ClassDB::bind_method(D_METHOD("set_feed_every", "n"), &VideoFaceNode::setFeedEvery);
    ClassDB::bind_method(D_METHOD("get_feed_every"), &VideoFaceNode::getFeedEvery);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "model_level", PROPERTY_HINT_RANGE, "0,3,1"),
                 "set_model_level", "get_model_level");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "feed_every", PROPERTY_HINT_RANGE, "1,30,1"),
                 "set_feed_every", "get_feed_every");

    // face_blendshape: blendshape=52×float[0,1] (ARKit52, 驱动 avatar 主信号),
    //                  pts=帧序号, final=本次 feed 末帧。
    // face_landmarks: points=count×Vector2 归一化[0,1] (调试叠加; 已知坐标 bug 见头注),
    //                 img_w/img_h=回读帧尺寸, pts=帧序号。
    ADD_SIGNAL(MethodInfo("face_desc",
                          PropertyInfo(Variant::INT, "fps"),
                          PropertyInfo(Variant::INT, "blendshape_count"),
                          PropertyInfo(Variant::INT, "landmark_count")));
    ADD_SIGNAL(MethodInfo("face_blendshape",
                          PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "blendshape"),
                          PropertyInfo(Variant::INT, "pts"),
                          PropertyInfo(Variant::BOOL, "final")));
    ADD_SIGNAL(MethodInfo("face_landmarks",
                          PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "points"),
                          PropertyInfo(Variant::INT, "img_w"),
                          PropertyInfo(Variant::INT, "img_h"),
                          PropertyInfo(Variant::INT, "pts")));
    ADD_SIGNAL(MethodInfo("face_ready"));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "msg")));
}

}  // namespace godot
