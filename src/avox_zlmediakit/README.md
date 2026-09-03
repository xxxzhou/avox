# ONVIF Backchannel 推流支持

## 概述

ONVIF Backchannel 是 ONVIF 标准定义的双向音频通道，允许客户端向摄像头推送音频数据，实现双向对讲功能。

**关键区别：** ONVIF Backchannel 使用 **DESCRIBE → SETUP → PLAY** 流程（类似播放器），而非普通推流的 ANNOUNCE → SETUP → RECORD。

## 协议流程对比

```
普通 RTSP 推流:          ONVIF Backchannel:
  ANNOUNCE                  DESCRIBE (获取摄像头 SDP，含 Backchannel track)
  SETUP                     SETUP (使用 sendonly track 的 control URL)
  RECORD                    PLAY
  发送数据                  发送数据
```

## SDP 结构

摄像头返回的 SDP 包含两个音频 track：

```
m=audio 0 RTP/AVP 8
a=control:trackID=1
a=rtpmap:8 PCMA/8000
a=recvonly              ← 用于接收摄像头音频（播放）

m=audio 0 RTP/AVP 8
a=control:trackID=5
a=rtpmap:8 PCMA/8000
a=sendonly              ← 用于发送音频到摄像头（Backchannel）
```

关键属性：
- `a=recvonly`: 摄像头发送，客户端接收（普通播放）
- `a=sendonly`: 客户端发送，摄像头接收（Backchannel）

## C++ 使用方式

```cpp
// IMediaMuxer 接口
class IMediaMuxer {
 public:
  virtual void setAudioCodec(ACodecId codecId) = 0;      // 设置音频编码
  virtual void setAudioDesc(const AudioDesc& adesc) = 0; // 设置音频格式
  virtual void setMuxerType(MuxerType type) = 0;         // 设置推流类型
};
```

```cpp
// 使用示例
auto muxer = createMuxer();

// 1. 设置推流类型为 ONVIF
muxer->setMuxerType(MuxerType::onvif);

// 2. 设置音频编码和格式
muxer->setAudioCodec(ACodecId::g711a);  // 或 g711u, aac
AudioDesc adesc;
adesc.sampleRate = 8000;
adesc.channels = 1;
muxer->setAudioDesc(adesc);

// 3. 打开 URL
muxer->open(url);
```

## 完整流程

```
┌─────────────────────────────────────────────────────────────┐
│               ONVIF Backchannel 推流流程                    │
├─────────────────────────────────────────────────────────────┤
│  muxer->setMuxerType(MuxerType::onvif);                     │
│  muxer->setAudioCodec(ACodecId::g711a);                     │
│  muxer->setAudioDesc({.sampleRate=8000, .channels=1});      │
│  muxer->open(url);                                          │
│                                                             │
│  内部流程:                                                   │
│  1. DESCRIBE + Require: www.onvif.org/ver20/backchannel     │
│  2. 解析 SDP，找到 sendonly track (trackID=5)               │
│  3. SETUP trackID=5                                         │
│  4. PLAY                                                    │
│  5. 开始发送 RTP 音频数据                                    │
└─────────────────────────────────────────────────────────────┘
```

## 一次性 Token URL 说明

**重要：** 很多摄像头的 RTSP URL 包含一次性 token（如 `sessionid=xxx`），这个 token 在 DESCRIBE 请求时被"消费"。

**RtspPusherOnvif 内部处理：**
- DESCRIBE → SETUP → PLAY 都在**同一个 TCP 连接**内完成
- Token 只被消费一次，后续请求复用已建立的连接
- **不需要额外处理 token**

## ZLMediaKit 实现方案

### 1. 新增 C API

```c
// api/include/mk_pusher.h
mk_pusher mk_pusher_create_onvif(mk_media_source src);
```

### 2. RtspPusherOnvif 类

```cpp
// src/Rtsp/RtspPusher.h
class RtspPusherOnvif : public RtspPlayer, public PusherBase {
public:
    using Ptr = std::shared_ptr<RtspPusherOnvif>;
    RtspPusherOnvif(const EventPoller::Ptr &poller, const RtspMediaSource::Ptr &src);
    ~RtspPusherOnvif() override;
    
    void publish(const std::string &url) override;
    void teardown() override;

protected:
    bool onCheckSDP(const std::string &sdp) override;
    void onRecvRTP(RtpPacket::Ptr rtp, const SdpTrack::Ptr &track) override;
    void onPlaySuccess();

private:
    void sendRtpPacket(const RtspMediaSource::RingDataType &pkt);
    void updateRtcpContext(const RtpPacket::Ptr &rtp);
    int getTrackIndexByTrackType(TrackType type) const;

private:
    std::weak_ptr<RtspMediaSource> _push_src;
    RtspMediaSource::RingType::RingReader::Ptr _rtsp_reader;
    std::vector<RtcpContext::Ptr> _rtcp_context;
    toolkit::Ticker _rtcp_send_ticker[2];
    std::string _keepalive_url;
    uint32_t _keepalive_cseq = 0;
};

// 工厂函数
PusherBase::Ptr createPusherOnvif(const EventPoller::Ptr &poller,
                                  const RtspMediaSource::Ptr &src);
```

### 3. 关键实现细节

#### Track 选择逻辑

```cpp
bool RtspPusherOnvif::onCheckSDP(const std::string &sdp) {
    // 从 SDP 中找到 sendonly track (Backchannel)
    for (auto &track : _sdp_track) {
        auto it = track->_attr.find("sendonly");
        if (it != track->_attr.end()) {
            // 找到 sendonly track，只保留这个
            _sdp_track = {track};
            break;
        }
    }
    // ...
}
```

#### TCP Interleaved Channel 处理

服务器在 SETUP 响应中返回实际的 interleaved channel：

```
SETUP Response:
Transport: RTP/AVP/TCP;unicast;interleaved=10-11;ssrc=00000000
```

发送 RTP 时使用服务器返回的 channel：

```cpp
void RtspPusherOnvif::sendRtpPacket(...) {
    // 使用 SETUP 响应中的 interleaved channel
    auto &track = _sdp_track[track_index];
    send(makeRtpOverTcpPrefix(rtp_size, track->_interleaved));
    send(rtp_data);
}
```

#### Session 保活

ONVIF Backchannel 的 Session 通常有 60 秒超时，需要定期发送 OPTIONS 保活：

```cpp
void RtspPusherOnvif::updateRtcpContext(const RtpPacket::Ptr &rtp) {
    if (ticker.elapsedTime() > 30 * 1000) {
        // 每 30 秒发送 OPTIONS 保活
        send("OPTIONS " + _keepalive_url + " RTSP/1.0\r\n...");
    }
}
```

### 4. 关键改动点

**RtspPlayer.h:**
```cpp
protected:
    bool _bOnvifBackchannel = false;  // 标记 ONVIF Backchannel 模式
```

**RtspPlayer.cpp:**
```cpp
void RtspPlayer::sendDescribe() {
    if (_bOnvifBackchannel) {
        // ONVIF Backchannel 需要加 Require 头
        sendRtspRequest("DESCRIBE", _play_url, 
            {"Accept", "application/sdp", "Require", "www.onvif.org/ver20/backchannel"});
    } else {
        sendRtspRequest("DESCRIBE", _play_url, {"Accept", "application/sdp"});
    }
}

void RtspPlayer::handleResDESCRIBE(...) {
    if (_bOnvifBackchannel) {
        // ONVIF 需要获取所有 track（包括 Backchannel）
        _sdp_track = sdpParser.getAllTracks();
    } else {
        _sdp_track = sdpParser.getAvailableTrack();
    }
}

void RtspPlayer::onPlayResult_l(...) {
    if (!_bOnvifBackchannel) {
        // ONVIF 不需要 RTP 接收超时检查
        _rtp_check_timer = ...;
    }
}
```

**Rtsp.h (SdpParser):**
```cpp
class SdpParser {
public:
    // 新增：获取所有 track（包括被 getAvailableTrack 过滤的）
    std::vector<SdpTrack::Ptr> getAllTracks() const { return _track_vec; }
};
```

### 5. 不影响原有功能

所有改动都有条件判断：

| 改动 | 条件 | 影响原有功能 |
|------|------|-------------|
| DESCRIBE 加 Require 头 | `_bOnvifBackchannel == true` | 否 |
| 使用 getAllTracks() | `_bOnvifBackchannel == true` | 否 |
| 跳过 RTP 超时检查 | `_bOnvifBackchannel == true` | 否 |
| RtspPusherOnvif 类 | 独立新类 | 否 |

## 支持的音频编码

| 编码 | ACodecId | RTP PT | 采样率 |
|------|----------|--------|--------|
| G.711A (PCMA) | g711a | 8 | 8000 |
| G.711U (PCMU) | g711u | 0 | 8000 |
| AAC | aac | 97 | 16000 |

## 常见问题

### 1. 对方没有声音

检查项：
- 是否正确选择了 sendonly track（trackID=5）
- TCP 模式下是否使用了服务器返回的 interleaved channel
- 音频数据是否正确生成（采样率、编码格式匹配）

### 2. 连接几秒后断开

检查项：
- 是否发送了 RTSP OPTIONS 保活命令（建议 30 秒一次）
- Session timeout 值（通常 60 秒）

### 3. Token 失效

- 不要在推流前单独发送 DESCRIBE（会消费 token）
- 使用 `MuxerType::onvif` 让内部流程处理全部逻辑
