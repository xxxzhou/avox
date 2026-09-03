using AvoxCommon;
using AvoxNet;
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Windows.Input;

#if ANDROID
using Android.Content;
using Android.Net;
using Java.IO;
#endif

namespace AvoxControls
{
    /// <summary>
    /// 播放器 Model
    /// 封装渲染器和音频控制
    /// </summary>
    public class PlayerModel : INotifyPropertyChanged
    {
        private bool isMuted = false;
        private double volume = 1.0;
        protected SourceInfoModel sourceInfoModel;
        private PlayerState currentState = PlayerState.none;

        /// <summary>
        /// 视频渲染器（子类可重写）
        /// </summary>
        public virtual ISurfaceRender WindowRender => null;

        /// <summary>
        /// 音频渲染器（子类可重写）
        /// </summary>
        public virtual IAudioRender AudioRender => null;

        /// <summary>
        /// 媒体复用器（子类可重写）
        /// </summary>
        public virtual IMediaMuxer MediaMuxer => null;

        /// <summary>
        /// 是否静音
        /// </summary>
        public bool IsMuted
        {
            get => isMuted;
            set
            {
                if (isMuted != value)
                {
                    isMuted = value;
                    ApplyMuteState();
                    OnPropertyChanged(nameof(IsMuted));
                }
            }
        }

        /// <summary>
        /// 音量 (0.0 - 1.0)
        /// </summary>
        public double Volume
        {
            get => volume;
            set
            {
                if (volume != value)
                {
                    volume = Math.Max(0, Math.Min(1, value));
                    ApplyVolumeSetting();
                    OnPropertyChanged(nameof(Volume));
                }
            }
        }

        /// <summary>
        /// 音频控制命令
        /// </summary>
        public ICommand ToggleMuteCommand { get; }
        public ICommand SetVolumeCommand { get; }

        /// <summary>
        /// 截图命令
        /// </summary>
        public ICommand TakeScreenshotCommand { get; }

        /// <summary>
        /// 录制命令
        /// </summary>
        public ICommand StartRecordingCommand { get; }
        public ICommand StopRecordingCommand { get; }

        /// <summary>
        /// 是否正在录制
        /// </summary>
        private bool isRecording = false;
        public bool IsRecording
        {
            get => isRecording;
            private set
            {
                if (isRecording != value)
                {
                    isRecording = value;
                    OnPropertyChanged(nameof(IsRecording));
                    OnPropertyChanged(nameof(CanRecord));
                }
            }
        }

        /// <summary>
        /// 是否可以开始录制（子类可重写）
        /// </summary>
        public virtual bool CanRecord => !IsRecording;

        /// <summary>
        /// 源信息 Model（用于数据绑定）
        /// </summary>
        public SourceInfoModel SourceInfoModel => sourceInfoModel;

        /// <summary>
        /// 当前播放状态（子类需要设置此属性）
        /// </summary>
        public PlayerState CurrentState
        {
            get => currentState;
            protected set
            {
                if (currentState != value)
                {
                    var preState = currentState;
                    currentState = value;
                    OnPropertyChanged(nameof(CurrentState));
                    OnStateChanged(preState, value);
                }
            }
        }

        /// <summary>
        /// 是否可以获取源信息（状态为 Ready 或之后）
        /// </summary>
        public bool CanGetSourceInfo => CurrentState >= PlayerState.ready;

        /// <summary>
        /// 播放器状态变更处理（子类可重写）
        /// </summary>
        protected virtual void OnStateChanged(PlayerState preState, PlayerState state)
        {
            OnPropertyChanged(nameof(CanGetSourceInfo));

            // 状态变为 Ready 或之后，更新源信息
            if (state == PlayerState.ready || state == PlayerState.playing)
            {
                UpdateSourceInfo();
            }
            else if (state == PlayerState.none || state == PlayerState.stopped)
            {
                // 状态为 none 或 closed 时，清除源信息
                sourceInfoModel?.Clear();
            }
        }

        /// <summary>
        /// 更新源信息（子类需要实现此方法返回 ISourceInfo）
        /// </summary>
        protected virtual void UpdateSourceInfo()
        {
            var info = GetSourceInfo();
            if (info != null)
            {
                sourceInfoModel?.UpdateSourceInfo(info);
            }
            else
            {
                sourceInfoModel?.Clear();
            }
        }

        /// <summary>
        /// 获取源信息（子类需要实现）
        /// </summary>
        protected virtual ISourceInfo GetSourceInfo()
        {
            return null;
        }

        /// <summary>
        /// 属性变更事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 构造函数
        /// </summary>
        public PlayerModel()
        {
            // 初始化音频控制命令
            ToggleMuteCommand = new ActionCommand(() => IsMuted = !IsMuted);
            SetVolumeCommand = new ActionCommand<double>(vol => Volume = vol);
            // 初始化截图命令
            TakeScreenshotCommand = new ActionCommand(() => TakeScreenshot());
            // 初始化录制命令
            StartRecordingCommand = new ActionCommand(() => StartRecording());
            StopRecordingCommand = new ActionCommand(() => StopRecording());
            // 初始化源信息 Model
            sourceInfoModel = new SourceInfoModel();
        }

        /// <summary>
        /// 应用静音状态到音频渲染器
        /// </summary>
        private void ApplyMuteState()
        {
            try
            {
                if (AudioRender != null)
                {
                    float targetVolume = isMuted ? 0.0f : (float)volume;
                    AudioRender.setVolume(targetVolume);
                    AvoxWrapper.logMsg(LogLevel.info, $"设置静音状态: {isMuted}, 音量: {targetVolume}");
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"设置静音状态失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 应用音量设置到音频渲染器
        /// </summary>
        private void ApplyVolumeSetting()
        {
            try
            {
                if (AudioRender != null)
                {
                    float targetVolume = isMuted ? 0.0f : (float)volume;
                    AudioRender.setVolume(targetVolume);
                    AvoxWrapper.logMsg(LogLevel.info, $"设置音量: {targetVolume}");
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"设置音量失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 获取当前音量
        /// </summary>
        public double GetVolume()
        {
            try
            {
                if (AudioRender != null)
                {
                    volume = AudioRender.getVolume();
                    OnPropertyChanged(nameof(Volume));
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"获取音量失败: {ex.Message}");
            }
            return volume;
        }

        /// <summary>
        /// 截图功能
        /// </summary>
        public virtual void TakeScreenshot()
        {
            try
            {
                if (WindowRender != null)
                {
                    Task.Run(() =>
                    {
                        IImageBuffer imageBuffer = AvoxWrapper.createImageBuffer();
                        // ISurfaceRender 的截图方法叫 screenShot
                        bool bGet = WindowRender.screenShot(imageBuffer);
                        if (bGet)
                        {
                            // 获取平台特定的默认截图路径
                            string basePath = PlatformHelper.GetSystemPicturesPath();
                            string fileName = $"screenshot_{DateTime.Now:yyyyMMdd_HHmmss}.png";
                            string filePath = Path.Combine(basePath, "DDPlayer", fileName);
                            // 确保目录存在
                            Directory.CreateDirectory(Path.GetDirectoryName(filePath));
                            // 保存截图
                            ImageUtils.SaveAsPng(imageBuffer, filePath);
                            AvoxWrapper.logMsg(LogLevel.info, $"截图已保存:{filePath}");
                            NotifySystemGallery(filePath);
                        }
                        else
                        {
                            AvoxWrapper.logMsg(LogLevel.warn, "渲染线程没能拿到数据");
                        }
                    });
                }
                else
                {
                    AvoxWrapper.logMsg(LogLevel.warn, "视频渲染器未设置，无法截图");
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"截图失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 开始录制
        /// </summary>
        /// <param name="uri">录制文件路径或URI</param>
        public virtual void StartRecording()
        {
            try
            {
                if (MediaMuxer != null && !IsRecording)
                {
                    EncodingConfig encoding = ConfigManager.Instance.Config.Encoding;
                    string uri = encoding.PushUrl;
                    if (encoding.RecordMode == RecordMode.Local)
                    {
                        string basePath = encoding.LocalPath;
                        string fileName = $"recording_{DateTime.Now:yyyyMMdd_HHmmss}.mp4";
                        uri = Path.Combine(basePath, fileName);
                        // 确保目录存在
                        Directory.CreateDirectory(Path.GetDirectoryName(uri));
                    }
                    MediaMuxer.setHardEncode(encoding.EnableHardwareEncoding);
                    MediaMuxer.setMuxerType(encoding.MuxerType);
                    MediaMuxer.setVideoCodec(encoding.VideoCodec);
                    MediaMuxer.setAudioCodec(encoding.AudioCodec);
                    // 开始录制
                    MediaMuxer.open(uri);
                    IsRecording = true;
                }
                else if (MediaMuxer == null)
                {
                    AvoxWrapper.logMsg(LogLevel.warn, "媒体复用器未设置，无法录制");
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"开始录制失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 停止录制
        /// </summary>
        public virtual void StopRecording()
        {
            try
            {
                if (MediaMuxer != null && IsRecording)
                {
                    MediaMuxer.close();
                    IsRecording = false;
                    AvoxWrapper.logMsg(LogLevel.info, "停止录制");
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"停止录制失败: {ex.Message}");
            }
        }

        public static void NotifySystemGallery(string filePath)
        {
#if ANDROID
            try
            {
                // 1. 获取 Android Context
                var context = Android.App.Application.Context;
                // 2. 使用完全限定名：Java.IO.File 消除与 System.IO.File 的歧义
                Java.IO.File javaFile = new Java.IO.File(filePath);
                // 3. 创建扫描 Intent
                var mediaScanIntent = new Android.Content.Intent(Android.Content.Intent.ActionMediaScannerScanFile);        
                // 4. 使用完全限定名：Android.Net.Uri 消除与 System.Uri 的歧义
                Android.Net.Uri contentUri = Android.Net.Uri.FromFile(javaFile);        
                mediaScanIntent.SetData(contentUri);
                // 5. 发送广播
                context.SendBroadcast(mediaScanIntent);        
                AvoxWrapper.logMsg(LogLevel.info, $"[Android] 已通知系统相册扫描新文件: {filePath}");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"[Android] 刷新相册失败: {ex.Message}");
            }
#elif IOS
    try
    {
        // 1. 确保路径存在
        if (!System.IO.File.Exists(filePath))
        {
            AvoxWrapper.logMsg(LogLevel.error, $"[iOS] 文件不存在: {filePath}");
            return;
        }
        // 2. 使用 Foundation 转换路径为 NSUrl
        var url = Foundation.NSUrl.FromFilename(filePath);
        // 3. 调用 Photos 框架将图片保存到相册
        Photos.PHPhotoLibrary.SharedPhotoLibrary.PerformChanges(() =>
        {
            Photos.PHAssetChangeRequest.FromImage(url);
        }, (success, error) =>
        {
            if (success)
            {
                AvoxWrapper.logMsg(LogLevel.info, $"[iOS] 已成功保存到系统相册: {filePath}");
            }
            else
            {
                AvoxWrapper.logMsg(LogLevel.error, $"[iOS] 保存相册失败: {error?.LocalizedDescription}");
            }
        });
    }
    catch (Exception ex)
    {
        AvoxWrapper.logMsg(LogLevel.error, $"[iOS] 刷新相册异常: {ex.Message}");
    }
#endif
        }

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
