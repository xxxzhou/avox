using Avalonia.Controls;
using Avalonia.Interactivity;
using AvoxCommon;
using AvoxControls.Converters;
using AvoxNet;
using System;
using System.Linq;

namespace AvoxControls.SourcePlayer
{
    /// <summary>
    /// SourceSettingView - 设备源设置界面
    /// 包含渲染设置和编码设置（带视频/音频编码选择）
    /// </summary>
    public partial class SourceSettingView : UserControl
    {
        private PlayerConfig originalConfig;
        private EnumToStringConverter<VCodecId> videoCodecConverter;
        private EnumToStringConverter<ACodecId> audioCodecConverter;
        private EnumToStringConverter<RecordMode> recordModeConverter;
        private EnumToStringConverter<MuxerType> muxerTypeConverter;

        /// <summary>
        /// 用户是否点击了确定按钮
        /// </summary>
        public bool IsConfirmed { get; private set; }

        #region 事件

        /// <summary>
        /// 设置确认事件 - 当用户点击确定并验证通过后触发
        /// </summary>
        public event EventHandler<PlaybackConfig> SettingsConfirmed;

        /// <summary>
        /// 设置取消事件 - 当用户点击取消时触发
        /// </summary>
        public event EventHandler SettingsCancelled;

        #endregion

        /// <summary>
        /// 构造函数
        /// </summary>
        public SourceSettingView()
        {
            InitializeComponent();

            // 初始化转换器和选项
            InitializeVideoCodecOptions();
            InitializeAudioCodecOptions();
            InitializeRecordModeOptions();
            InitializeMuxerTypeOptions();

            // 加载当前配置
            Loaded += OnLoaded;
        }

        /// <summary>
        /// 视图加载完成
        /// </summary>
        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            // 自动加载当前配置
            var config = ConfigManager.Instance.Config;
            LoadConfig(config);
        }

        /// <summary>
        /// 加载配置到视图
        /// </summary>
        public void LoadConfig(PlayerConfig config)
        {
            if (config != null)
            {
                // 保存原始配置的副本（用于取消时恢复）
                originalConfig = config.Clone();
                DataContext = config;
                IsConfirmed = false;

                // 更新 RadioButton 选中状态
                UpdateVideoCodecRadioButtons(config.Encoding.VideoCodec);
                UpdateAudioCodecRadioButtons(config.Encoding.AudioCodec);
                UpdateRecordModeRadioButtons(config.Encoding.RecordMode);
                UpdateMuxerTypeRadioButtons(config.Encoding.MuxerType);
            }
        }

        /// <summary>
        /// 初始化视频编码选项
        /// </summary>
        private void InitializeVideoCodecOptions()
        {
            if (VideoCodecPanel == null) return;

            // 创建视频编码转换器，使用AvoxWrapper.getVCodecName获取显示名称
            videoCodecConverter = new EnumToStringConverter<VCodecId>(AvoxWrapper.getVCodecName);

            // 初始化面板
            videoCodecConverter.InitPanel(VideoCodecPanel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Encoding.VideoCodec = value;
                }
            });
        }

        /// <summary>
        /// 初始化音频编码选项
        /// </summary>
        private void InitializeAudioCodecOptions()
        {
            if (AudioCodecPanel == null) return;

            // 创建音频编码转换器，使用AvoxWrapper.getACodecName获取显示名称
            audioCodecConverter = new EnumToStringConverter<ACodecId>(AvoxWrapper.getACodecName);

            // 初始化面板
            audioCodecConverter.InitPanel(AudioCodecPanel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Encoding.AudioCodec = value;
                }
            });
        }

        /// <summary>
        /// 初始化录制模式选项
        /// </summary>
        private void InitializeRecordModeOptions()
        {
            if (RecordModePanel == null) return;

            // 创建录制模式转换器
            recordModeConverter = new EnumToStringConverter<RecordMode>((mode) =>
            {
                return mode switch
                {
                    RecordMode.Local => "本地录制",
                    RecordMode.Network => "网络推流",
                    _ => mode.ToString()
                };
            });

            // 初始化面板
            recordModeConverter.InitPanel(RecordModePanel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Encoding.RecordMode = value;
                }
            });
        }

        /// <summary>
        /// 初始化复用器类型选项
        /// </summary>
        private void InitializeMuxerTypeOptions()
        {
            if (MuxerTypePanel == null) return;

            // 创建复用器类型转换器
            muxerTypeConverter = new EnumToStringConverter<MuxerType>((type) =>
            {
                return type switch
                {
                    MuxerType.none => "其他",
                    MuxerType.ffmpeg => "FFmpeg",
                    MuxerType.zlmediakit => "ZLMediaKit",
                    _ => type.ToString()
                };
            });

            // 初始化面板
            muxerTypeConverter.InitPanel(MuxerTypePanel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Encoding.MuxerType = value;
                }
            });
        }

        /// <summary>
        /// 更新视频编码RadioButton选中状态
        /// </summary>
        private void UpdateVideoCodecRadioButtons(VCodecId selectedCodec)
        {
            var panel = this.FindControl<StackPanel>("VideoCodecPanel");
            if (panel == null) return;

            // 使用新方法更新UI
            videoCodecConverter.UpdateUI(panel, selectedCodec);
        }

        /// <summary>
        /// 更新音频编码RadioButton选中状态
        /// </summary>
        private void UpdateAudioCodecRadioButtons(ACodecId selectedCodec)
        {
            var panel = this.FindControl<StackPanel>("AudioCodecPanel");
            if (panel == null) return;

            // 使用新方法更新UI
            audioCodecConverter.UpdateUI(panel, selectedCodec);
        }

        /// <summary>
        /// 更新录制模式RadioButton选中状态
        /// </summary>
        private void UpdateRecordModeRadioButtons(RecordMode selectedMode)
        {
            var panel = this.FindControl<StackPanel>("RecordModePanel");
            if (panel == null) return;

            // 使用新方法更新UI
            recordModeConverter.UpdateUI(panel, selectedMode);
        }

        /// <summary>
        /// 更新复用器类型RadioButton选中状态
        /// </summary>
        private void UpdateMuxerTypeRadioButtons(MuxerType selectedType)
        {
            var panel = this.FindControl<StackPanel>("MuxerTypePanel");
            if (panel == null) return;

            // 使用新方法更新UI
            muxerTypeConverter.UpdateUI(panel, selectedType);
        }

        /// <summary>
        /// 浏览本地保存目录
        /// </summary>
        private async void OnBrowseLocalPathClick(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFolderDialog
            {
                Title = "选择本地保存目录"
            };

            var window = TopLevel.GetTopLevel(this) as Window;
            if (window != null)
            {
                var result = await dialog.ShowAsync(window);
                if (!string.IsNullOrEmpty(result))
                {
                    LocalPathTextBox.Text = result;
                }
            }
        }

        /// <summary>
        /// 确定按钮点击
        /// </summary>
        private void OKButton_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                // 验证设置值
                if (!ValidateSettings())
                {
                    return;
                }

                IsConfirmed = true;

                // 触发设置确认事件，传递当前配置
                var currentConfig = DataContext as PlayerConfig;
                if (currentConfig != null)
                {
                    SettingsConfirmed?.Invoke(this, currentConfig.Playback);
                }
            }
            catch (Exception ex)
            {
                ShowErrorMessage($"应用设置失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 取消按钮点击
        /// </summary>
        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            IsConfirmed = false;

            // 恢复原始配置
            if (originalConfig != null && DataContext is PlayerConfig currentConfig)
            {
                currentConfig.CopyFrom(originalConfig);
            }

            // 触发设置取消事件
            SettingsCancelled?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>
        /// 验证设置值是否有效
        /// </summary>
        private bool ValidateSettings()
        {
            if (DataContext is not PlayerConfig config)
                return false;

            // 验证网络推流地址
            if (config.Encoding.RecordMode == RecordMode.Network && string.IsNullOrWhiteSpace(config.Encoding.PushUrl))
            {
                ShowErrorMessage("网络推流模式下必须填写推流地址");
                return false;
            }

            return true;
        }

        /// <summary>
        /// 显示错误消息
        /// </summary>
        private void ShowErrorMessage(string message)
        {
            var parentWindow = TopLevel.GetTopLevel(this) as Window;
            if (parentWindow != null)
            {
                var messageBox = new MessageBox("设置错误", message);
                messageBox.ShowDialog(parentWindow);
            }
        }

        #region 字体颜色设置

        /// <summary>
        /// 设置字体颜色为白色
        /// </summary>
        private void OnFontColorWhiteClick(object sender, RoutedEventArgs e)
        {
            if (DataContext is PlayerConfig config)
            {
                config.Stt.FontColorR = 1.0f;
                config.Stt.FontColorG = 1.0f;
                config.Stt.FontColorB = 1.0f;
            }
        }

        /// <summary>
        /// 设置字体颜色为红色
        /// </summary>
        private void OnFontColorRedClick(object sender, RoutedEventArgs e)
        {
            if (DataContext is PlayerConfig config)
            {
                config.Stt.FontColorR = 1.0f;
                config.Stt.FontColorG = 0.0f;
                config.Stt.FontColorB = 0.0f;
            }
        }

        /// <summary>
        /// 设置字体颜色为黄色
        /// </summary>
        private void OnFontColorYellowClick(object sender, RoutedEventArgs e)
        {
            if (DataContext is PlayerConfig config)
            {
                config.Stt.FontColorR = 1.0f;
                config.Stt.FontColorG = 1.0f;
                config.Stt.FontColorB = 0.0f;
            }
        }

        /// <summary>
        /// 设置字体颜色为绿色
        /// </summary>
        private void OnFontColorGreenClick(object sender, RoutedEventArgs e)
        {
            if (DataContext is PlayerConfig config)
            {
                config.Stt.FontColorR = 0.0f;
                config.Stt.FontColorG = 1.0f;
                config.Stt.FontColorB = 0.0f;
            }
        }

        #endregion
    }
}
