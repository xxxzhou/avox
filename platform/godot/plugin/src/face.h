#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <avox/AvoxAudio.h>

#include <atomic>
#include <memory>
#include <thread>

namespace godot {

// avox_avatar wav2arkit (IAudioFace) → face_blendshape/face_ready/face_desc/error 信号
// TTS/麦克风 PCM 进 (feed_pcm) → ARKit52 blendshape 出 (52×float [0,1] @30fps),
// 经 face_blendshape 信号投递主线程; GDScript 取嘴部索引 (jawOpen/mouthLowerDownL/…)
// 驱动 2D/3D 嘴形。对称 TtsNode: 模型后台线程加载, 回调跨线程经 call_deferred。
// 模型对象级常驻: 首轮 start 加载 wav2arkit (385MB), 后续轮复用 (stop 只停任务不销毁)。
class FaceNode : public Node {
    GDCLASS(FaceNode, Node)

public:
    FaceNode() = default;
    ~FaceNode();

    void start();                // 预加载模型 (后台线程, 就绪发 face_ready); 幂等
    void stop();                 // 停止推理 (flush + join worker, 模型常驻)
    bool loading();              // 模型/任务是否已就绪 (avox loading 语义 = running)
    // 告知输入 PCM 格式 (TTS 出 s16 mono; feed_pcm 前调, 默认 22050/1)。tts_desc 时更新。
    void setAudioDesc(int p_sample_rate, int p_channels);
    // 喂一段 s16 mono PCM (来自 TTS audio), 自动人队推理; pts 仅标记输出帧 (ms)
    void feedPcm(PackedByteArray p_data, int64_t p_pts);

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    friend class FaceOb;
    std::unique_ptr<avox::IAudioFace> face;      // 默认 deleter: ~AudioFace 内 stop+releaseEngine
    std::unique_ptr<avox::IAudioFaceOb> faceOb;
    int sampleRate = 22050;        // kokoro 默认; tts_desc 更新
    int channels = 1;
    std::atomic<bool> loadStop{false};
    std::thread loadThread;
};

} // namespace godot
