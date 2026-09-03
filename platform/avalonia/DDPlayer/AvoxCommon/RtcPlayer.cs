using AvoxCommon;
using AvoxNet;
using System;

namespace AvoxCommon
{
    /// <summary>
    /// RtcPlayer 封装类
    /// 针对 IRtcPlayer 接口的封装，用于 WebRTC 协议的推拉流
    /// 编码/解码/数据源使用 C++ 项目已实现的再映射成 WebRTC 接口
    /// </summary>
    public class RtcPlayer : BasePlayer, IDisposable
    {
        private AvoxNet.IRtcPlayer rtcPlayer;
        private bool isDisposed = false;

        /// <summary>
        /// 底层 IRtcPlayer 实例
        /// </summary>
        public AvoxNet.IRtcPlayer Player => rtcPlayer;

        /// <summary>
        /// 构造函数
        /// </summary>
        public RtcPlayer()
        {
            rtcPlayer = AvoxWrapper.createWebRtcPlayer();
            AvoxWrapper.addRtcPlayerOb(rtcPlayer, this);
        }

        /// <summary>
        /// 设置角色类型（Offer/Answer）
        /// </summary>
        public void SetRollType(RtcRollType type)
        {
            try
            {
                rtcPlayer?.setRollType(type);
                AvoxWrapper.logMsg(LogLevel.info, $"设置 RTC 角色: {type}");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置 RTC 角色失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置 SDP 代理
        /// </summary>
        public void SetSdpAgentOb(ISdpAgentOb ob)
        {
            try
            {
                rtcPlayer?.setSdpAgentOb(ob);
                AvoxWrapper.logMsg(LogLevel.info, "设置 SDP 代理成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置 SDP 代理失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置视频源
        /// </summary>
        public void SetVideoSource(IVideoSource videoSource)
        {
            try
            {
                rtcPlayer?.setVideoSource(videoSource);
                AvoxWrapper.logMsg(LogLevel.info, "设置 RTC 视频源成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置 RTC 视频源失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置音频源
        /// </summary>
        public void SetAudioSource(IAudioSource audioSource)
        {
            try
            {
                rtcPlayer?.setAudioSource(audioSource);
                AvoxWrapper.logMsg(LogLevel.info, "设置 RTC 音频源成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置 RTC 音频源失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 打开 RTC 连接
        /// </summary>
        public bool Open()
        {
            try
            {
                bool result = rtcPlayer?.open() ?? false;
                if (result)
                {
                    AvoxWrapper.logMsg(LogLevel.info, "RTC 连接打开成功");
                }
                else
                {
                    AvoxWrapper.logMsg(LogLevel.error, "RTC 连接打开失败");
                }
                return result;
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"RTC 连接打开失败: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// 获取远端源信息（拉流）
        /// </summary>
        public ISourceInfo GetRemoteSourceInfo()
        {
            return rtcPlayer?.getRemoteSourceInfo();
        }

        /// <summary>
        /// 获取本地源信息（推流）
        /// </summary>
        public ISourceInfo GetLocalSourceInfo()
        {
            return rtcPlayer?.getLocalSourceInfo();
        }

        /// <summary>
        /// 关闭 RTC 连接
        /// </summary>
        public void Close()
        {
            rtcPlayer?.close();
            AvoxWrapper.logMsg(LogLevel.info, "RTC 连接关闭");
        }

        /// <summary>
        /// 获取本地渲染器（用于预览）
        /// </summary>
        public ISurfaceRender GetLocalSurfaceRender()
        {
            return rtcPlayer?.getLocalSurfaceRender();
        }
        public IAudioRender GetLocalAudioRender()
        {
            return rtcPlayer?.getLocalAudioRender();
        }
        /// <summary>
        /// 获取远端渲染器（拉流显示）
        /// </summary>
        public ISurfaceRender GetRemoteSurfaceRender()
        {
            return rtcPlayer?.getRemoteSurfaceRender();
        }
        public IAudioRender GetRemoteAudioRender()
        {
            return rtcPlayer?.getRemoteAudioRender();
        }

        /// <summary>
        /// 获取本地 SDP
        /// </summary>
        public string GetLocalSdp()
        {
            return rtcPlayer?.getLocalSdp();
        }

        /// <summary>
        /// 设置远端 SDP
        /// </summary>
        public void SetRemoteSdp(string sdp)
        {
            try
            {
                rtcPlayer?.setRemoteSdp(sdp);
                AvoxWrapper.logMsg(LogLevel.info, "设置远端 SDP 成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置远端 SDP 失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 添加 ICE 候选
        /// </summary>
        public void AddIceCandidate(string candidate, string mid, int mlineIndex)
        {
            try
            {
                rtcPlayer?.addIceCandidate(candidate, mid, mlineIndex);
                AvoxWrapper.logMsg(LogLevel.info, $"添加 ICE 候选: mid={mid}, index={mlineIndex}");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"添加 ICE 候选失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 重写状态变更，同步底层状态
        /// </summary>
        public override void onStateChange(PlayerState preState, PlayerState state)
        {
            base.onStateChange(preState, state);
        }

        /// <summary>
        /// 实现 ISdpAgentOb 的 onLocalSdp
        /// </summary>
        public virtual void onLocalSdp(string localSdp)
        {
            AvoxWrapper.logMsg(LogLevel.info, $"收到本地 SDP: {localSdp?.Length ?? 0} bytes");
        }

        /// <summary>
        /// 实现 ISdpAgentOb 的 onIceCandidate
        /// </summary>
        public virtual void onIceCandidate(string candidate, string mid, int mlineIndex)
        {
            AvoxWrapper.logMsg(LogLevel.info, $"收到 ICE 候选: mid={mid}, index={mlineIndex}");
        }

        /// <summary>
        /// 释放资源
        /// </summary>
        public new void Dispose()
        {
            if (!isDisposed)
            {
                isDisposed = true;
                
                if (rtcPlayer != null)
                {
                    rtcPlayer.close();
                    rtcPlayer.Dispose();
                    rtcPlayer = null;
                }
                
                base.Dispose();
                AvoxWrapper.logMsg(LogLevel.info, "RtcPlayer 资源释放完成");
            }
        }
    }
}
