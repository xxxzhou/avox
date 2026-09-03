using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using AvoxCommon;
using AvoxControls.Converters;
using AvoxNet;
using System;
using System.Linq;
using System.Threading.Tasks;

namespace AvoxControls.MediaPlayer
{
    /// <summary>
    /// MediaSettingView - 播放设置视图
    /// 绑定到 PlayerConfig，包含 Playback 和 Encoding 配置
    /// </summary>
    public partial class MediaSettingView : UserControl
    {
        private PlayerConfig originalConfig;
        private EnumToStringConverter<IoPlan> ioPlanConverter;
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
        public MediaSettingView()
        {
            InitializeComponent();
            InitializeIoPlanConverter();
            InitializeRecordModeConverter();
            InitializeMuxerTypeOptions();

            // 加载当前配置
            Loaded += OnLoaded;
        }

        /// <summary>
        /// 视图加载完成
        /// </summary>
        private void OnLoaded(object sender, global::Avalonia.Interactivity.RoutedEventArgs e)
        {
            // 自动加载当前配置
            var config = ConfigManager.Instance.Config;
            LoadConfig(config);
        }

        /// <summary>
        /// 初始化 IoPlan 转换器和 RadioButton
        /// </summary>
        private void InitializeIoPlanConverter()
        {
            ioPlanConverter = new EnumToStringConverter<IoPlan>(AvoxWrapper.getIoPlanStr)
            {
                ExcludeFirstItem = true
            };

            var panel = this.FindControl<StackPanel>("IoPlanPanel");
            if (panel == null) return;

            // 使用新方法初始化面板
            ioPlanConverter.InitPanel(panel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Playback.IoPlan = value;
                }
            });
        }

        /// <summary>
        /// 初始化 RecordMode 转换器和 RadioButton
        /// </summary>
        private void InitializeRecordModeConverter()
        {
            recordModeConverter = new EnumToStringConverter<RecordMode>(GetRecordModeStr);

            var panel = this.FindControl<StackPanel>("RecordModePanel");
            if (panel == null) return;

            // 使用新方法初始化面板
            recordModeConverter.InitPanel(panel, DataContext, (value) =>
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
            if (this.FindControl<StackPanel>("MuxerTypePanel") == null) return;

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
            muxerTypeConverter.InitPanel(this.FindControl<StackPanel>("MuxerTypePanel"), DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Encoding.MuxerType = value;
                }
            });
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
        /// 获取 RecordMode 显示字符串
        /// </summary>
        private string GetRecordModeStr(RecordMode mode)
        {
            return mode switch
            {
                RecordMode.Local => "本地文件",
                RecordMode.Network => "网络推流",
                _ => mode.ToString()
            };
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
                UpdateIoPlanRadioButtons(config.Playback.IoPlan);
                UpdateRecordModeRadioButtons(config.Encoding.RecordMode);
                UpdateMuxerTypeRadioButtons(config.Encoding.MuxerType);
            }
        }

        /// <summary>
        /// 更新 IoPlan RadioButton 选中状态
        /// </summary>
        private void UpdateIoPlanRadioButtons(IoPlan selectedPlan)
        {
            var panel = this.FindControl<StackPanel>("IoPlanPanel");
            if (panel == null) return;

            // 使用新方法更新UI
            ioPlanConverter.UpdateUI(panel, selectedPlan);
        }

        /// <summary>
        /// 更新 RecordMode RadioButton 选中状态
        /// </summary>
        private void UpdateRecordModeRadioButtons(RecordMode selectedMode)
        {
            var panel = this.FindControl<StackPanel>("RecordModePanel");
            if (panel == null) return;

            // 使用新方法更新UI
            recordModeConverter.UpdateUI(panel, selectedMode);
        }

        /// <summary>
        /// 浏览本地路径按钮点击事件
        /// </summary>
        private async void OnBrowseLocalPathClick(object sender, RoutedEventArgs e)
        {
            var topLevel = TopLevel.GetTopLevel(this);
            if (topLevel == null) return;

            var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = "选择本地保存目录",
                AllowMultiple = false
            });

            if (folders.Count > 0)
            {
                var localPathTextBox = this.FindControl<TextBox>("LocalPathTextBox");
                if (localPathTextBox != null)
                {
                    localPathTextBox.Text = folders[0].Path.LocalPath;
                }
            }
        }

        /// <summary>
        /// 确定按钮点击事件
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
        /// 取消按钮点击事件
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

            // 验证缓冲时间
            if (config.Playback.BufferSizeMs < 100)
            {
                ShowErrorMessage("缓冲时间不能小于 100 毫秒");
                return false;
            }

            // 验证水印参数
            if (config.Playback.EnableWatermark)
            {
                if (config.Playback.WatermarkWidth <= 0 || config.Playback.WatermarkHeight <= 0)
                {
                    ShowErrorMessage("水印宽度和高度必须大于 0");
                    return false;
                }
            }

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

        /// <summary>
        /// 保存腾讯云翻译 API 密钥
        /// </summary>
        private void OnSaveTencentApiClick(object sender, RoutedEventArgs e)
        { 
            if (DataContext is PlayerConfig config)
            {
                var secretId = config.Stt.TencentSecretId;
                var secretKey = config.Stt.TencentSecretKey;
                if (string.IsNullOrWhiteSpace(secretId) || string.IsNullOrWhiteSpace(secretKey))
                {
                    return;
                }
                AvoxWrapper.saveTencentApi(secretId, secretKey);
                ConfigManager.Instance.SaveConfig();
            }
        }
    }
}
