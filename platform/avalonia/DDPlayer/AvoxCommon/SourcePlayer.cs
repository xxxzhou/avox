using AvoxCommon;
using AvoxNet;
using System;

namespace AvoxCommon
{
    /// <summary>
    /// SourcePlayer 封装类
    /// 针对 ISourcePlayer 接口的封装，用于处理直出的设备源（摄像头、麦克风等）
    /// 不需要解码器，也不需要音视频同步
    /// 注意：SourcePlayer 使用 AsrMode::streaming 模式，不支持翻译
    /// </summary>
    public class SourcePlayer : BasePlayer, IDisposable
    {
        private AvoxNet.ISourcePlayer sourcePlayer;
        private ISubtitle? subtitle;
        private bool isDisposed = false;

        /// <summary>
        /// 底层 ISourcePlayer 实例
        /// </summary>
        public AvoxNet.ISourcePlayer Player => sourcePlayer;

        /// <summary>
        /// 构造函数
        /// </summary>
        public SourcePlayer()
        {
            sourcePlayer = AvoxWrapper.createDevicePlayer();
            AvoxWrapper.addSourcePlayerOb(sourcePlayer, this);
        }

        /// <summary>
        /// 获取视频渲染器
        /// </summary>
        public ISurfaceRender GetSurfaceRender()
        {
            return sourcePlayer?.getSurfaceRender();
        }

        /// <summary>
        /// 获取音频渲染器
        /// </summary>
        public IAudioRender GetAudioRender()
        {
            return sourcePlayer?.getAudioRender();
        }

        /// <summary>
        /// 配置语音识别
        /// SourcePlayer 使用 AsrMode::streaming 模式（不支持翻译）
        /// </summary>
        /// <param name="config">STT 配置</param>
        public void EnableSttWithRender(SttConfig config)
        {
            try
            {
                subtitle = sourcePlayer?.getSubtitle();
                if (subtitle == null)
                {
                    AvoxWrapper.logMsg(LogLevel.warn, "Subtitle 为空，无法启用语音识别");
                    return;
                }
                // 设置字体样式（通过 IFontLayer）
                var render = sourcePlayer?.getSurfaceRender();
                if (render != null)
                {
                    var fontLayer = AvoxWrapper.enableRenderFont(render);
                    if (fontLayer != null)
                    {
                        fontLayer.setScale(config.FontScale);
                        fontLayer.setColor(config.FontColorR, config.FontColorG, config.FontColorB, config.FontOpacity);
                    }
                }
                // 启用 ASR (SourcePlayer 内部固定使用 streaming 模式)
                subtitle.enableAsr();
                AvoxWrapper.logMsg(LogLevel.info, $"SourcePlayer 语音识别已启用: AsrMode=streaming");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"启用语音识别失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 禁用语音识别
        /// </summary>
        public void DisableSttWithRender()
        {
            try
            {
                if (subtitle != null)
                {
                    subtitle.close();
                    subtitle.Dispose();
                    subtitle = null;
                }
                AvoxWrapper.logMsg(LogLevel.info, "SourcePlayer 语音识别已禁用");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"禁用语音识别失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置音频源
        /// </summary>
        public void SetAudioSource(IAudioSource source)
        {
            try
            {
                sourcePlayer?.setAudioSource(source);
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置音频源失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置视频源
        /// </summary>
        public void SetVideoSource(IVideoSource source)
        {
            try
            {
                sourcePlayer?.setVideoSource(source);
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置视频源失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 获取复用器（用于录制或推流）
        /// </summary>
        public IMediaMuxer GetMuxer()
        {
            return sourcePlayer?.getMuxer();
        }

        /// <summary>
        /// 打开源播放
        /// </summary>
        public bool Open()
        {
            try
            {
                bool result = sourcePlayer?.open() ?? false;
                if (result)
                {
                    var sttConfig = ConfigManager.Instance.Config.Stt;
                    if (sttConfig?.EnableStt == true)
                    {
                        EnableSttWithRender(sttConfig);
                    }
                }
                return result;
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"源播放打开失败: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// 关闭源播放
        /// </summary>
        public void Close()
        {
            DisableSttWithRender();
            sourcePlayer?.close();
            AvoxWrapper.logMsg(LogLevel.info, "源播放关闭");
        }

        /// <summary>
        /// 获取当前状态
        /// </summary>
        public PlayerState GetState()
        {
            return sourcePlayer?.getState() ?? PlayerState.none;
        }

        /// <summary>
        /// 获取源信息
        /// </summary>
        public ISourceInfo GetSourceInfo()
        {
            return sourcePlayer?.getSourceInfo();
        }

        /// <summary>
        /// 重写状态变更，同步底层状态
        /// </summary>
        public override void onStateChange(PlayerState preState, PlayerState state)
        {
            base.onStateChange(preState, state);
        }

        /// <summary>
        /// 释放资源
        /// </summary>
        public override void Dispose()
        {
            if (!isDisposed)
            {
                isDisposed = true;
                if (sourcePlayer != null)
                {
                    AvoxWrapper.removeSourcePlayerOb(sourcePlayer, this);
                    sourcePlayer.close();
                    sourcePlayer.Dispose();
                    sourcePlayer = null;
                }
                base.Dispose();
                AvoxWrapper.logMsg(LogLevel.info, "SourcePlayer 资源释放完成");
            }
        }
    }
}
