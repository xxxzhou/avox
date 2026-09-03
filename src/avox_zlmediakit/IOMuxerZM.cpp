#include "IOMuxerZM.hpp"

#include <cstring>

#include "ZlmHelper.hpp"
#include "avox/Avox.hpp"
#include "avox/module/AvoxManager.hpp"
#include "avox/module/LogHelper.hpp"
#include "avox/muxer/Muxer.hpp"
#include "avox/source/PacketBuf.hpp"

namespace avox {

void regZmMuxer() {
  RegFunc zmMuxerReg = {"zlmediakit muxer init", []() {
                          MuxerDesc zmDesc = {};
                          zmDesc.name = "zlmediakit";
                          AvoxManager::Get().muxers.regInitFunc(
                              MuxerType::zlmediakit, zmDesc,
                              []() -> IOMuxer* { return new IOMuxerZM(); });
                        }};
  AvoxManager::Get().initFuncs.push_back(zmMuxerReg);
  // ONVIF Backchannel 推流
  RegFunc onvifMuxerReg = {"onvif muxer init", []() {
                             MuxerDesc onvifDesc = {};
                             onvifDesc.name = "onvif";
                             AvoxManager::Get().muxers.regInitFunc(
                                 MuxerType::onvif, onvifDesc, []() -> IOMuxer* {
                                   IOMuxerZM* zm = new IOMuxerZM();
                                   zm->bOnvifBackchannel = true;
                                   return zm;
                                 });
                           }};
  AvoxManager::Get().initFuncs.push_back(onvifMuxerReg);
}

IOMuxerZM::IOMuxerZM() {}

IOMuxerZM::~IOMuxerZM() { onClose(); }

void IOMuxerZM::onOpen() {
  // ONVIF Backchannel 模式由外部通过 MuxerType::onvif 指定
  // 不需要自动检测，也不需要解析 URL 参数
}

bool IOMuxerZM::onInit() {
  // 如果流ID为空，生成一个默认的
  if (streamId.empty()) {
    streamId = "stream_" + std::to_string(reinterpret_cast<uintptr_t>(this));
  }
  // 检测是否是本地录制
  bLocalRecord = checkLocalPath(url.c_str());
  // 创建媒体源，本地录制时不自动开启MP4（由 mk_recorder_start 手动控制）
  // 网络推流时也不自动开启
  zmMedia =
      mk_media_create(vhost.c_str(), app.c_str(), streamId.c_str(), 0.0f, 0, 0);
  // MKCodecAAC
  int32_t vCodecId = getZlCodecId(vDesc.codecId);
  if (bHaveVideo && vCodecId >= 0) {
    float fps = vDesc.desc.fps;
    if (fps <= 0) {
      fps = 25;
    }
    // 计算默认码率，单位kbps
    int32_t bitrate = vDesc.desc.width * vDesc.desc.height * fps * 8 / 1000;
    bool bInit = mk_media_init_video(zmMedia, vCodecId, vDesc.desc.width,
                                     vDesc.desc.height, fps, bitrate);
    if (!bInit) {
      log(LogLevel::warn, "zlmediakit init video track failed");
      bHaveVideo = false;
    }
  }
  int32_t aCodecId = getZlCodecId(aDesc.codecId);
  if (bHaveAudio && aCodecId >= 0) {
    bool bInit = mk_media_init_audio(zmMedia, aCodecId, aDesc.desc.sampleRate,
                                     aDesc.desc.channels, 16);
    if (!bInit) {
      log(LogLevel::warn, "zlmediakit init audio track failed");
      bHaveAudio = false;
    }
  }
  mk_media_init_complete(zmMedia);
  // 设置媒体源注册回调（本地录制和网络推流都需要，等 MediaSource
  // 注册成功后再操作）
  mk_media_set_on_regist(zmMedia, onMediaRegist, this);
  // 设置媒体源关闭回调
  mk_media_set_on_close(zmMedia, onMediaClose, this);
  // 写vps sps pps配置
  if (bHaveVideo && vDesc.codecId != VCodecId::none) {
    for (auto& packet : videoConfigs) {
      PacketBufPtr tempPacket = std::make_shared<PacketBuf>(packet);
      pushPacket(tempPacket);
    }
  }
  // aac音频需要发送asc头
  if (bHaveAudio && aDesc.codecId == ACodecId::aac) {
    std::vector<uint8_t> adtsHeader;
    adtsHeader.resize(7);
    addAdtsHeader(aDesc.desc, adtsHeader.data(), 0, 2);
    mk_media_input_aac(zmMedia, adtsHeader.data(), 7, 0, adtsHeader.data());
  }
  return true;
}

void onMediaRegist(void* userData, mk_media_source sender, int regist) {
  IOMuxerZM* muxer = static_cast<IOMuxerZM*>(userData);
  if (!muxer || !regist) {
    return;
  }
  if (muxer->bLocalRecord) {
    // 本地录制：MediaSource 注册成功后启动 MP4 录制
    // 如果是完整文件路径（如 D:/1.mp4），直接传完整路径
    std::string recordDir;
    if (checkDirectFile(muxer->url)) {
      recordDir = muxer->url;
    } else {
      recordDir = parentDir(muxer->url);
    }
    int result =
        mk_recorder_start(1, muxer->vhost.c_str(), muxer->app.c_str(),
                          muxer->streamId.c_str(), recordDir.c_str(), 0);
    log(LogLevel::info, "zlmediakit local record start, dir:", recordDir,
        " result:", result);
  } else {
    // 网络推流：检查 URL 是否以 schema 开头（官方示例用法）
    const char* schema = mk_media_source_get_schema(sender);
    size_t schemaLen = strlen(schema);
    if (muxer->url.compare(0, schemaLen, schema) == 0) {
      // 先释放已有的 pusher（避免重复创建）
      if (muxer->zmPusher) {
        mk_pusher_release(muxer->zmPusher);
        muxer->zmPusher = nullptr;
      }
      // ONVIF Backchannel 使用专用的推流器
      if (muxer->bOnvifBackchannel) {
        muxer->zmPusher = mk_pusher_create_onvif(sender);
      } else {
        muxer->zmPusher = mk_pusher_create_src(sender);
      }
      mk_pusher_set_on_result(
          muxer->zmPusher,
          [](void* userData, int errCode, const char* errMsg) {
            if (errCode == 0) {
              return;
            }
            IOMuxerZM* zmuxer = static_cast<IOMuxerZM*>(userData);
            AVError err = zlIoError(errCode);
            zmuxer->onError(err, errMsg);
            log(LogLevel::warn, "zlmediakit pusher regist, errCode:", errCode,
                " errMsg:", errMsg ? errMsg : "");
          },
          muxer);
      mk_pusher_set_on_shutdown(
          muxer->zmPusher,
          [](void* userData, int errCode, const char* errMsg) {
            if (errCode == 0) {
              return;
            }
            IOMuxerZM* zmuxer = static_cast<IOMuxerZM*>(userData);
            AVError err = zlIoError(errCode);
            zmuxer->onError(err, errMsg);
            log(LogLevel::warn, "zlmediakit pusher shutdown, errCode:", errCode,
                " errMsg:", errMsg ? errMsg : "");
          },
          muxer);
      mk_pusher_publish(muxer->zmPusher, muxer->url.c_str());
    }
  }
}

void onMediaClose(void* userData) {
  log(LogLevel::warn, "zlmediakit media close");
}

void IOMuxerZM::onPushPacket(const AvoxPacket& packet) {
  if (!zmMedia) {
    return;
  }
  PackType packType = (PackType)packet.packtype;
  bool bVideo = packType == PackType::video || packType == PackType::vconfig;
  bool bAudio = packType == PackType::audio || packType == PackType::aconfig;
  // 发送媒体数据
  if (bVideo) {
    sendVideoData(packet);
  } else {
    sendAudioData(packet);
  }
}

void IOMuxerZM::sendVideoData(const AvoxPacket& packet) {
  // 根据视频编码类型发送数据
  // mk_media_input_h264/h265 签名: (ctx, data, len, dts, pts)
  if (vDesc.codecId == VCodecId::h264) {
    mk_media_input_h264(zmMedia, packet.data.data, packet.data.size, packet.dts,
                        packet.pts);
  } else if (vDesc.codecId == VCodecId::h265) {
    mk_media_input_h265(zmMedia, packet.data.data, packet.data.size, packet.dts,
                        packet.pts);
  }
}

void IOMuxerZM::sendAudioData(const AvoxPacket& packet) {
  if (packet.data.size == 0) {
    return;
  }
  // 根据音频编码类型发送数据
  if (aDesc.codecId == ACodecId::aac) {
    // 对于AAC，需要处理ADTS头
    mk_media_input_aac(zmMedia, packet.data.data, packet.data.size, packet.pts,
                       nullptr);
  } else if (aDesc.codecId == ACodecId::g711a ||
             aDesc.codecId == ACodecId::g711u) {
    mk_media_input_audio(zmMedia, packet.data.data, packet.data.size,
                         packet.pts);    
  } else if (aDesc.codecId == ACodecId::opus) {
    mk_media_input_audio(zmMedia, packet.data.data, packet.data.size,
                         packet.pts);
  }
  // log(LogLevel::info, "pts:", packet.pts);
}

void IOMuxerZM::onClose() {
  if (bLocalRecord) {
    mk_recorder_stop(1, vhost.c_str(), app.c_str(), streamId.c_str());
    log(LogLevel::info, "zlmediakit local record stop:", url);
  }
  if (zmPusher) {
    mk_pusher_release(zmPusher);
    zmPusher = nullptr;
  }
  if (zmMedia) {
    mk_media_release(zmMedia);
    zmMedia = nullptr;
  }
}

}
