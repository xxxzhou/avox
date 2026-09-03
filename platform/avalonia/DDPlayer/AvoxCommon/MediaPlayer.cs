using AvoxCommon;
using AvoxNet;
using System;

namespace AvoxCommon
{
    /// <summary>
    /// MediaPlayer 封装类
    /// 针对 IMediaPlayer 接口的封装，用于需要解码的媒体文件、直播流等
    /// </summary>
    public class MediaPlayer : BasePlayer, IDisposable
    {
        private AvoxNet.IMediaPlayer mediaPlayer;
        private ISubtitle? subtitle;
        private bool isDisposed = false;

        /// <summary>
        /// 底层 IMediaPlayer 实例
        /// </summary>
        public AvoxNet.IMediaPlayer Player => mediaPlayer;

        /// <summary>
        /// 构造函数
        /// </summary>
        public MediaPlayer()
        {
            mediaPlayer = AvoxWrapper.createMediaPlayer();
            AvoxWrapper.addMediaPlayerOb(mediaPlayer, this);
        }

        /// <summary>
        /// 获取选项接口
        /// </summary>
        public IOption GetOption()
        {
            return mediaPlayer?.getOption();
        }

        /// <summary>
        /// 设置 IO 计划（使用 ffmpeg 或 zlmediakit）
        /// </summary>
        public void SetIoPlan(IoPlan plan)
        {
            mediaPlayer?.setIoPlan(plan);
        }

        /// <summary>
        /// 设置是否使用硬件解码
        /// </summary>
        public void SetHardDecode(bool hard)
        {
            mediaPlayer?.setHardDecode(hard);
        }

        /// <summary>
        /// 获取视频渲染器
        /// </summary>
        public ISurfaceRender GetSurfaceRender()
        {
            return mediaPlayer?.getSurfaceRender();
        }

        /// <summary>
        /// 获取音频渲染器
        /// </summary>
        public IAudioRender GetAudioRender()
        {
            return mediaPlayer?.getAudioRender();
        }

        /// <summary>
        /// 获取字幕接口
        /// </summary>
        public ISubtitle GetSubtitle()
        {
            return mediaPlayer?.getSubtitle();
        }

        /// <summary>
        /// 配置并启用语音识别与翻译
        /// MediaPlayer 使用 AsrMode::ptsSync 模式（支持翻译）
        /// </summary>
        /// <param name="config">STT 配置</param>
        public void EnableSttWithRender(SttConfig config)
        {
            try
            {
                subtitle = mediaPlayer?.getSubtitle();
                if (subtitle == null)
                {
                    AvoxWrapper.logMsg(LogLevel.warn, "Subtitle 为空，无法启用语音识别");
                    return;
                }
                // 设置字体样式（通过 IFontLayer）
                var render = mediaPlayer?.getSurfaceRender();
                if (render != null)
                {
                    var fontLayer = AvoxWrapper.enableRenderFont(render);
                    if (fontLayer != null)
                    {
                        fontLayer.setScale(config.FontScale);
                        fontLayer.setColor(config.FontColorR, config.FontColorG, config.FontColorB, config.FontOpacity);
                    }
                }
                // 启用 ASR (MediaPlayer 内部固定使用 ptsSync 模式)
                subtitle.enableAsr();
                // 启用翻译（日转中）
                if (config.EnableTranslation)
                {
                    subtitle.enableTranslation();
                }
                AvoxWrapper.logMsg(LogLevel.info, $"语音识别已启用: AsrMode=ptsSync, 翻译={config.EnableTranslation}");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"启用语音识别失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 禁用语音识别和翻译
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
                AvoxWrapper.logMsg(LogLevel.info, "语音识别已禁用");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"禁用语音识别失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 获取复用器（用于录制或推流）
        /// </summary>
        public IMediaMuxer GetMuxer()
        {
            return mediaPlayer?.getMuxer(false);
        }

        /// <summary>
        /// 打开媒体文件或流
        /// </summary>
        public void Open(string url)
        {
            try
            {
                mediaPlayer?.open(url);
                var sttConfig = ConfigManager.Instance.Config.Stt;
                if (sttConfig?.EnableStt == true)
                {
                    EnableSttWithRender(sttConfig);
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"打开媒体失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 关闭媒体
        /// </summary>
        public void Close()
        {
            if (subtitle != null)
            {
                DisableSttWithRender();
            }
            mediaPlayer?.close();
        }

        /// <summary>
        /// Seek 到指定位置（毫秒）
        /// </summary>
        public void Seek(long pos)
        {
            try
            {
                mediaPlayer?.seek(pos);
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"Seek 失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 暂停播放
        /// </summary>
        public void Pause()
        {
            mediaPlayer?.pause();
        }

        /// <summary>
        /// 恢复播放
        /// </summary>
        public void Resume()
        {
            mediaPlayer?.resume();
        }

        /// <summary>
        /// 设置播放速度
        /// </summary>
        public void SetSpeed(double speed)
        {
            try
            {
                mediaPlayer?.speed(speed);
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置播放速度失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 获取当前状态
        /// </summary>
        public PlayerState GetState()
        {
            return mediaPlayer?.getState() ?? PlayerState.none;
        }

        /// <summary>
        /// 获取播放进度（0-1）
        /// </summary>
        public double GetProcess()
        {
            return mediaPlayer?.getProcess() ?? 0.0;
        }

        /// <summary>
        /// 获取媒体总时长（毫秒）
        /// </summary>
        public long GetDuration()
        {
            return mediaPlayer?.getDuration() ?? 0;
        }

        /// <summary>
        /// 获取当前播放位置（毫秒）
        /// </summary>
        public long GetPosition()
        {
            return mediaPlayer?.getPosition() ?? 0;
        }

        /// <summary>
        /// 获取源信息
        /// </summary>
        public ISourceInfo GetSourceInfo()
        {
            return mediaPlayer?.getSourceInfo();
        }

        /// <summary>
        /// 获取码率
        /// </summary>
        public double GetRate(TrackType type)
        {
            return mediaPlayer?.getRate(type,false) ?? 0.0;
        }

        /// <summary>
        /// 获取帧率
        /// </summary>
        public double GetFps()
        {
            return mediaPlayer?.getFps() ?? 0.0;
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
                if (mediaPlayer != null)
                {
                    AvoxWrapper.removeMediaPlayerOb(mediaPlayer, this);
                    mediaPlayer.close();
                    mediaPlayer.Dispose();
                    mediaPlayer = null;
                }
                base.Dispose();
            }
        }
    }
}
