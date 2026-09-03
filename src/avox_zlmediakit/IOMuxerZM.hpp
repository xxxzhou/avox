#pragma once

#include "ZlmHelper.hpp"
#include "avox/muxer/IOMuxer.hpp"

namespace avox {

void onMediaRegist(void* userData, mk_media_source sender, int regist);
void onMediaClose(void* userData);

// 媒体流复用器，ZLMediaKit实现
class IOMuxerZM : public IOMuxer {
 public:
  IOMuxerZM();
  virtual ~IOMuxerZM();

 public:
  bool bOnvifBackchannel = false;

 protected:
  // ZLMediaKit媒体源
  mk_media zmMedia = nullptr;
  // ZLMediaKit推流器
  mk_pusher zmPusher = nullptr;
  // 虚拟主机
  std::string vhost = "__defaultVhost__";
  // 应用名
  std::string app = "live";
  // 流ID
  std::string streamId = "";
  // 是否是本地录制
  bool bLocalRecord = false;

 protected:
  virtual void onOpen() override;
  virtual bool onInit() override;
  virtual void onPushPacket(const AvoxPacket& packet) override;
  virtual void onClose() override;

 private:
  // 媒体源注册回调
  friend void onMediaRegist(void* userData, mk_media_source sender, int regist);
  // 媒体源关闭回调
  friend void onMediaClose(void* userData);
  // 发送视频数据
  void sendVideoData(const AvoxPacket& packet);
  // 发送音频数据
  void sendAudioData(const AvoxPacket& packet);
};

}
