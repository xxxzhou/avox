#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <avox/AvoxMuxer.h>

#include "render_source.h"

#include <atomic>
#include <memory>

namespace godot {

// IRecorder 释放需先 close() (拉流线程/编码器清理), 自定义 deleter 保证 RAII 下不跳过
struct RecorderDeleter {
    void operator()(avox::IRecorder *p) const {
        if (p) {
            p->close();
            delete p;
        }
    }
};

/// 流录制器 (Node, avox::IRecorder)。open(input, output) 开始录制/转码; IRecorderOb→信号。
/// 转码模式(transcode=true) 先解码再编码, 支持 getSurfaceRender 图像处理 (水印/LUT 等, Phase 5 接入);
/// 直录模式(false) 直接封装原始流 (RTSP→mp4 等, 不解码, 快)。
/// 对应 swig/python/avox/muxer.py (IRecorder)。
///
/// 用法 (直录 RTSP→mp4):
///   var r = MediaRecorder.new()
///   add_child(r)
///   r.open("rtsp://...", "out.mp4")
///   # ... r.progress / r.completed 信号接 UI
///   r.close()
///
/// 用法 (仅音频, 转码):
///   r.transcode = true
///   r.video_codec = -1   # none: 丢弃视频轨 (仅音频录制见 [[avox-recorder-audio-only-mp4-output]]: 输出必须 .mp4)
///   r.open("rtsp://...", "out.mp4")
class MediaRecorder : public Node, public IRenderSource {
    GDCLASS(MediaRecorder, Node)

public:
    MediaRecorder();
    ~MediaRecorder();

    bool open(const String &p_input, const String &p_output);
    void close();
    bool seek(int64_t p_pos_ms);

    int get_state() const;          // RecorderState (none=0 opening=1 recording=2 completed=3)
    int64_t get_duration() const;   // 源总时长 ms (<=0 直播/未知); recording 后有效
    Dictionary get_source_info() const;

    // IRenderSource: 仅 transcode 模式有 surface(直录 StreamRecorder 不解码, 返回 null)。
    avox::ISurfaceRender* getSurfaceRenderRaw() override { return recorder ? recorder->getSurfaceRender() : nullptr; }

    // ── 配置 (open 前设置; transcode 改变会重建 recorder) ──
    // 注: IRecorder 接口无 setHardEncode (该方法在 IMediaMuxer, recorder 内部自带), 故不暴露
    void set_transcode(bool p_b);
    bool get_transcode() const;
    void set_video_codec(int64_t p_codec);   // VCodecId: h264=1 h265=2 none=-1(丢弃视频轨)
    int64_t get_video_codec() const;
    void set_audio_codec(int64_t p_codec);   // ACodecId: aac=0 g711a=1 opus=3 ... none=-1(丢弃音频轨)
    int64_t get_audio_codec() const;
    void set_muxer_type(int64_t p_t);        // MuxerType: ffmpeg=1 zlmediakit=2 onvif=3
    int64_t get_muxer_type() const;
    void set_io_plan(int64_t p_p);           // IoPlan: zlmediakit=1 ffmpeg=2
    int64_t get_io_plan() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class RecorderOb;
    std::unique_ptr<avox::IRecorder, RecorderDeleter> recorder;
    std::unique_ptr<avox::IRecorderOb> recorderOb;

    bool bTranscode = false;
    int64_t videoCodec = 2;   // h265 (avox 默认)
    int64_t audioCodec = 0;   // aac (avox 默认)
    int64_t muxerType = 1;    // ffmpeg
    int64_t ioPlan = 2;       // ffmpeg

    std::atomic<int> stateCache{0};

    void ensureRecorder();    // 懒创建 + 应用缓存配置 + 挂 ob
    void destroyRecorder();
};

} // namespace godot
