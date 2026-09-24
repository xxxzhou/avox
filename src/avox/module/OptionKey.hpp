#pragma once

#include "../AvoxDef.h"

// 别的头文件不要引用此文件,此文件定义配置key,用来配置参数
// 在.cpp文件里引用比较key

namespace avox {

// 配置key
// 播放器是否打开低延迟模式
#define AVOX_MP_LOW_LATENCY_BOOL "mp.lowlatency"
// 低延迟下加速倍速 默认1.2倍速,需要大于1.0
#define AVOX_MP_LL_SPEED_DOUBLE "mp.lowlatency.speed"
// 播放器延迟时间 毫秒 默认1000ms
#define AVOX_MP_DELAY_MS_INT "mp.delay.ms"
// 缓冲看门狗超时 毫秒 默认10000ms(buffering持续超过即关播放器);
// torrent等渐进源seek后等数据较久, 可调大
#define AVOX_MP_BUFFERING_TIMEOUT_MS_INT "mp.buffering.timeout.ms"
// 播放器窗口是否双倍刷新 默认false
#define AVOX_MP_WINDOW_REFRESH_BOOL "mp.window.double.refresh"
// 变速类型,1 local/ 2 server,服务器的不用管,本地调整渲染间隔
#define AVOX_MP_SPEED_TYPE_INT "mp.speed.type"
// 严格大于4倍速时是否只处理I帧(快速预览,进包时丢P/B与音频,恰好4倍仍全量),默认true
#define AVOX_MP_IFRAME_ONLY_GT4_BOOL "mp.iframe.gt4"
// seek精确落点: 开=从落点I帧静默解码丢弃到目标位才显示(慢但准,长GOP软解有
// 解码burst代价); 关=落到I帧点即播(默认,快,画面从I帧起)
#define AVOX_MP_SEEK_PRECISE_BOOL "mp.seek.precise"
// 视频解码器名覆盖(测试/排障强制车道): 空=默认选型链, 覆盖首选名且回软解兜底
#define AVOX_MP_VIDEO_DECODER_NAME_STR "mp.video.decoder.name"
// 解码器故障注入(调试/测试): 值=解码器注册名, 该解码器open阶段强制失败
#define AVOX_MP_DECODER_FAILINJECT_STR "mp.decoder.failinject"
// 播放器主时钟类型 0=none 1=audio(默认) 2=video 3=external
// 音频 PTS 异常(数据量与时间对应不上)的流可切到 video 绕过
#define AVOX_MP_SYNC_TYPE_INT "mp.synctype"
// IMediaPlayer里的getOption设置的key
#define AVOX_MP_IO_TIMEOUT_MS_INT "io.timeout.ms"
// HTTP 自定义请求头, CRLF 分隔的 "Key: value" 行(云盘直链要带
// UA/Access-Token 的源用; 空=不下发)
#define AVOX_MP_IO_HTTP_HEADERS_STR "io.http.headers"
// HTTP User-Agent 覆盖(FFmpeg 默认 Lavf/x.y): 云盘直链校验 UA 的源用
#define AVOX_MP_IO_HTTP_USERAGENT_STR "io.http.useragent"
// ffmpeg拉流可以设置成udp tcp
#define AVOX_MP_IO_RTSP_TRANSPORT_STR "io.rtsp.transport"
// RTSP拉流倍速(点播/NVR回放源,PLAY带Scale;仅zlmediakit IO生效,直播源无效)
#define AVOX_MP_IO_RTSP_SPEED_DOUBLE "io.rtsp.speed"
// Track ready等待超时，毫秒，默认3000ms(只有1个Track时等第二个Track来的超时)
#define AVOX_MP_IO_TRACK_READY_MS_INT "io.trackready.ms"
// HTTP 连接复用(keep-alive): 0=关(默认, 每次 range 读新建连接, 半开故障自然隔离)
// 1=开(seek 密集的旁路转录场景必开, 否则每次 seek 一次 TCP+TLS 握手)
#define AVOX_MP_IO_HTTP_PERSISTENT_INT "io.http.persistent"
// 转码录制器是否硬解(仅TranscodeRecorder消费)
#define AVOX_REC_HARD_DECODE_BOOL "rec.hard.decode"
// 转码录制器是否硬编(默认true; 设 false 走 FFmpeg 软编, windows软编文件更小, 仅TranscodeRecorder消费)
#define AVOX_REC_HARD_ENCODE_BOOL "rec.hard.encode"

// 日志打印
// IO线程包的信息
#define AVOX_LOG_SOURCE_INPACKET_BOOL "log.source.packet"
// 解码器当前解出来的帧信息
#define AVOX_LOG_DECODER_FRAME_BOOL "log.decoder.frame"
// 渲染出来的帧信息
#define AVOX_LOG_RENDER_FRAME_BOOL "log.render.frame"

}