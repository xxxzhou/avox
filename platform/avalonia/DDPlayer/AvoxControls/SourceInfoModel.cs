using AvoxNet;
using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace AvoxControls
{
    /// <summary>
    /// 源信息 Model
    /// 用于显示数据源的音频和视频信息（简化版，只显示第一个轨道）
    /// </summary>
    public class SourceInfoModel : INotifyPropertyChanged
    {
        private ISourceInfo sourceInfo;

        // 视频信息
        private string videoCodec = "";
        private string videoResolution = "";
        private int videoFrameRate = 0;
        private bool hasVideo = false;

        // 音频信息
        private string audioCodec = "";
        private int audioSampleRate = 0;
        private string audioChannelInfo = "";
        private bool hasAudio = false;

        /// <summary>
        /// 是否有视频
        /// </summary>
        public bool HasVideo => hasVideo;

        /// <summary>
        /// 是否有音频
        /// </summary>
        public bool HasAudio => hasAudio;

        /// <summary>
        /// 视频编码
        /// </summary>
        public string VideoCodec => videoCodec;

        /// <summary>
        /// 视频分辨率
        /// </summary>
        public string VideoResolution => videoResolution;

        /// <summary>
        /// 视频帧率
        /// </summary>
        public int VideoFrameRate => videoFrameRate;

        /// <summary>
        /// 音频编码
        /// </summary>
        public string AudioCodec => audioCodec;

        /// <summary>
        /// 音频采样率
        /// </summary>
        public int AudioSampleRate => audioSampleRate;

        /// <summary>
        /// 音频声道信息
        /// </summary>
        public string AudioChannelInfo => audioChannelInfo;

        public SourceInfoModel()
        {
        }

        /// <summary>
        /// 更新源信息
        /// </summary>
        public void UpdateSourceInfo(ISourceInfo info)
        {
            sourceInfo = info;
            RefreshInfo();
        }

        /// <summary>
        /// 刷新媒体信息
        /// </summary>
        private void RefreshInfo()
        {
            if (sourceInfo == null)
            {
                Clear();
                return;
            }
            // 获取第一个视频轨道信息
            int videoSize = sourceInfo.videoSize();
            if (videoSize > 0)
            {
                using (var vTrack = sourceInfo.getVideoDesc(0))
                {
                    if (vTrack != null)
                    {
                        videoCodec = AvoxWrapper.getVCodecName(vTrack.codecId);
                        if (vTrack.desc != null)
                        {
                            videoResolution = $"{vTrack.desc.width}x{vTrack.desc.height}";
                            videoFrameRate = (int)vTrack.desc.fps;
                        }
                        hasVideo = true;
                    }
                }
            }
            else
            {
                videoCodec = "";
                videoResolution = "";
                videoFrameRate = 0;
                hasVideo = false;
            }

            // 获取第一个音频轨道信息
            int audioSize = sourceInfo.audioSize();
            if (audioSize > 0)
            {
                using (var aTrack = sourceInfo.getAudioDesc(0))
                {
                    if (aTrack != null)
                    {
                        audioCodec = AvoxWrapper.getACodecName(aTrack.codecId);
                        if (aTrack.desc != null)
                        {
                            audioSampleRate = aTrack.desc.sampleRate;
                            audioChannelInfo = aTrack.desc.channels switch
                            {
                                1 => "单声道",
                                2 => "立体声",
                                _ => $"{aTrack.desc.channels}声道"
                            };
                        }
                        hasAudio = true;
                    }
                }
            }
            else
            {
                audioCodec = "";
                audioSampleRate = 0;
                audioChannelInfo = "";
                hasAudio = false;
            }
            // 通知所有属性变化
            OnPropertyChanged(nameof(HasVideo));
            OnPropertyChanged(nameof(HasAudio));
            OnPropertyChanged(nameof(VideoCodec));
            OnPropertyChanged(nameof(VideoResolution));
            OnPropertyChanged(nameof(VideoFrameRate));
            OnPropertyChanged(nameof(AudioCodec));
            OnPropertyChanged(nameof(AudioSampleRate));
            OnPropertyChanged(nameof(AudioChannelInfo));
        }

        /// <summary>
        /// 清除源信息
        /// </summary>
        public void Clear()
        {
            sourceInfo = null;
            videoCodec = "";
            videoResolution = "";
            videoFrameRate = 0;
            hasVideo = false;
            audioCodec = "";
            audioSampleRate = 0;
            audioChannelInfo = "";
            hasAudio = false;

            OnPropertyChanged(nameof(HasVideo));
            OnPropertyChanged(nameof(HasAudio));
            OnPropertyChanged(nameof(VideoCodec));
            OnPropertyChanged(nameof(VideoResolution));
            OnPropertyChanged(nameof(VideoFrameRate));
            OnPropertyChanged(nameof(AudioCodec));
            OnPropertyChanged(nameof(AudioSampleRate));
            OnPropertyChanged(nameof(AudioChannelInfo));
        }

        public event PropertyChangedEventHandler PropertyChanged;

        protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
