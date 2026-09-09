// body.cpp — 视频驱动身体姿态追踪 Godot 封装 (BodyNode)
// 行为基准: VideoFaceNode (video_face.cpp). 视频帧(经 IRenderSource 的 ISurfaceRender
// enableImage 零拷贝回读 rgba8)进 → MediaPipe Pose 33 点身体 landmark 出 (body_landmarks)。
//
// 线程模型:
//   BodySurfaceOb::onRender 在 avox 渲染线程 → 节流 feed imgBuf 给 IBody (非阻塞入队);
//   BodyOb 回调在 avox_avatar worker 线程 → 33×4 float (xyz+conf) 深拷贝成 Godot 数组,
//   经 call_deferred 投递主线程。模型加载在 loadThread (秒级), 完成发 body_ready。
//
// 数据源生命周期安全: 数据源节点(MediaPlayer 每次 play)会重建/释放其 ISurfaceRender,
// 故缓存的 tappedSr 可能悬空。untap 仅在"持有该 surface 的源仍存活且仍报告同一指针"时才
// remove/disable(否则 surface 已失效, observer 随之失效不再被回调)→ 无 UAF。

#include "body.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>  // ObjectDB (节点存活判定; 本版无 is_instance_valid)
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <avox/AvoxLayer.h>  // ISurfaceRender / ISurfaceRenderOb / addSurfaceRenderOb
#include <avox/AvoxVideo.h>  // createImageBuffer / IImageBuffer / ImageFormat / ImageType

#include <chrono>
#include <cstdint>
#include <cstring>

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
// BodyOb (身体 landmark 观察者, 实现 IBodyOb)
// ============================================================

// 回调在 avox_avatar worker 线程: raw 数据深拷贝成 Godot 数组 (raw 仅回调内有效),
// 经 call_deferred 投递主线程。对齐 VideoFaceOb。
class BodyOb : public avox::IBodyOb {
public:
    BodyNode *owner = nullptr;
    void onBodyDesc(const avox::BodyDesc &desc) override {
        if (owner)
            owner->call_deferred("emit_signal", "body_desc",
                                 desc.fps, desc.landmarkCount);
    }
    void onBodyLandmarks(const avox::AvoxData &raw, int32_t count,
                         int32_t imgW, int32_t imgH, int64_t pts) override {
        if (!owner) return;
        PackedVector3Array arr;
        PackedFloat32Array conf;  // 每点置信度 = spatial softmax 后热图峰值 (0~1, 低=遮挡/模型外推)
        if (count > 0 && raw.data && raw.size >= count * 4 * int32_t(sizeof(float))) {
            const float *p = reinterpret_cast<const float *>(raw.data);
            arr.resize(count);
            conf.resize(count);
            Vector3 *dst = arr.ptrw();
            float *cdst = conf.ptrw();
            float iw = float(imgW), ih = float(imgH);
            for (int i = 0; i < count; ++i) {
                // x,y 归一化 [0,1]; z 为相对深度 (相对髋部, 与 x/y 同尺度, 保留原值); conf=热图峰值
                dst[i] = Vector3(p[i * 4] / iw, p[i * 4 + 1] / ih, p[i * 4 + 2]);
                cdst[i] = p[i * 4 + 3];
            }
        }
        owner->call_deferred("emit_signal", "body_landmarks", arr, conf, imgW, imgH, pts);
    }
    void onBodyError(const char *err) override {
        if (owner && err)
            owner->call_deferred("emit_signal", "error", String::utf8(err));
    }
};

// ============================================================
// BodySurfaceOb (ISurfaceRenderOb, onRender 节流 feed)
// ============================================================

// onRender 在 avox 渲染线程 (每解码帧触发, graph->run 已 vkWaitForFences, 同帧可读 imgBuf)。
// 节流: 2 个 ONNX(det+lm) 较重, 喂太密会饿死渲染线程; RingBuffer 丢老帧, 跳帧只降刷新率。
class BodySurfaceOb : public avox::ISurfaceRenderOb {
public:
    BodyNode *owner = nullptr;
    int counter = 0;
    void onRender() override {
        if (!owner) return;
        // stop 后(tapWanted=false)或 body 未就绪则不喂; feed 非阻塞入队 body worker。
        if (!owner->tapWanted || !owner->body || !owner->imgBuf) return;
        if (owner->feedEvery > 1 && (++counter % owner->feedEvery) != 0) return;
        owner->body->feed(owner->imgBuf, owner->ptsCounter.fetch_add(1, std::memory_order_relaxed) + 1);
    }
};

// ============================================================
// BodyNode
// ============================================================

BodyNode::~BodyNode() {
    stop();
    if (body && bodyOb) avox::removeBodyOb(body.get(), bodyOb.get());  // 观察者先于 body 释放
    delete imgBuf;    // 须在 untap 的 disableImage 之后 (生命周期要求)
    delete surfaceOb;
}

void BodyNode::bindSource(Node *p_source) {
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

void BodyNode::start() {
    if (!body) {
        body.reset(avox::createBody("mediapipe_body"));
        if (!body) {
            call_deferred("emit_signal", "error",
                          wstrToStr(L"创建身体推理器失败 (avox_avatar 插件未注册? 模型未下载?)"));
            return;
        }
        bodyOb = std::make_unique<BodyOb>();
        static_cast<BodyOb *>(bodyOb.get())->owner = this;
        avox::addBodyOb(body.get(), bodyOb.get());
        // enableImage 回读缓冲 (rgba8, 调用方持有, 生命周期到 disableImage 之后)
        imgBuf = avox::createImageBuffer();
        avox::ImageFormat fmt = {};
        fmt.width = rbW;
        fmt.height = rbH;
        fmt.imageType = avox::ImageType::rgba8;
        imgBuf->setImageFormat(fmt);
        // onRender 节流 feed 的 surface 观察者
        surfaceOb = new BodySurfaceOb();
        surfaceOb->owner = this;
        tapWanted = true;  // _process 据此在数据源 surface 就绪后挂 tap
        // 首次创建才开后台加载线程 (模型秒级, 不阻塞主线程)。
        loadStop.store(false);
        loadThread = std::thread([this]() {
            body->start();
            auto t0 = std::chrono::steady_clock::now();
            while (!loadStop.load() && !body->loading() &&
                   std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (loadStop.load()) return;
            if (body->loading()) {
                call_deferred("emit_signal", "body_ready");
            } else {
                call_deferred("emit_signal", "error",
                              wstrToStr(L"身体模型加载超时 (用 fetch_assets 下载 mediapipe pose 模型)"));
            }
        });
    } else {
        // 后续轮: 幂等重启任务 + 重挂 tap, 不重载模型
        tapWanted = true;
        body->start();
    }
}

void BodyNode::untap() {
    if (!tappedSr) return;
    // 仅当持有该 surface 的源仍存活且仍报告同一指针时才安全 remove/disable;
    // 否则源已重建/释放 surface (如 MediaPlayer 每次 play), observer 随之失效不再被回调 → 不触碰, 避免悬空。
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

void BodyNode::stop() {
    tapWanted = false;
    loadStop.store(true);
    if (loadThread.joinable()) loadThread.join();
    untap();  // 先停喂 (移除 surface observer), 再 flush body worker
    if (body) body->stop();  // flush 已入队帧 + join worker; 模型常驻
}

bool BodyNode::loading() {
    return body ? body->loading() : false;
}

void BodyNode::setReadbackSize(int p_w, int p_h) {
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

int BodyNode::getReadbackWidth() const { return rbW; }
int BodyNode::getReadbackHeight() const { return rbH; }

void BodyNode::setFeedEvery(int p_n) {
    if (p_n >= 1) feedEvery = p_n;
}

int BodyNode::getFeedEvery() const { return feedEvery; }

void BodyNode::_notification(int p_what) {
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

void BodyNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bind_source", "source"), &BodyNode::bindSource);
    ClassDB::bind_method(D_METHOD("start"), &BodyNode::start);
    ClassDB::bind_method(D_METHOD("stop"), &BodyNode::stop);
    ClassDB::bind_method(D_METHOD("loading"), &BodyNode::loading);
    ClassDB::bind_method(D_METHOD("set_readback_size", "width", "height"), &BodyNode::setReadbackSize);
    ClassDB::bind_method(D_METHOD("get_readback_width"), &BodyNode::getReadbackWidth);
    ClassDB::bind_method(D_METHOD("get_readback_height"), &BodyNode::getReadbackHeight);
    ClassDB::bind_method(D_METHOD("set_feed_every", "n"), &BodyNode::setFeedEvery);
    ClassDB::bind_method(D_METHOD("get_feed_every"), &BodyNode::getFeedEvery);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "feed_every", PROPERTY_HINT_RANGE, "1,30,1"),
                 "set_feed_every", "get_feed_every");

    // body_desc: fps/landmark_count=33
    // body_landmarks: points=33×Vector3 (x,y 归一化[0,1], z=相对深度),
    //                 confidence=33×float spatial softmax 后热图峰值 (0~1, 低=遮挡/外推, 供低可信不驱动),
    //                 img_w/img_h=回读帧尺寸, pts=帧序号。未检出人时 points/confidence 为空数组。
    // body_ready: 模型加载完成可开始推理
    ADD_SIGNAL(MethodInfo("body_desc",
                          PropertyInfo(Variant::INT, "fps"),
                          PropertyInfo(Variant::INT, "landmark_count")));
    ADD_SIGNAL(MethodInfo("body_landmarks",
                          PropertyInfo(Variant::PACKED_VECTOR3_ARRAY, "points"),
                          PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "confidence"),
                          PropertyInfo(Variant::INT, "img_w"),
                          PropertyInfo(Variant::INT, "img_h"),
                          PropertyInfo(Variant::INT, "pts")));
    ADD_SIGNAL(MethodInfo("body_ready"));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "msg")));
}

}  // namespace godot
