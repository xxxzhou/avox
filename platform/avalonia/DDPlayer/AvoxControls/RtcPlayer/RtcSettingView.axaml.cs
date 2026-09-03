using Avalonia.Controls;
using Avalonia.Interactivity;
using AvoxCommon;
using AvoxControls.Converters;
using AvoxNet;
using System;
using System.Linq;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RTC 设置视图
    /// 包含WebRTC相关配置
    /// </summary>
    public partial class RtcSettingView : UserControl
    {
        private PlayerConfig originalConfig;
        private EnumToStringConverter<RtcRollType> rtcRollTypeConverter;
        private EnumToStringConverter<RtcMode> rtcModeConverter;

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
        public RtcSettingView()
        {
            InitializeComponent();

            // 初始化转换器和选项
            InitializeRtcRollTypeOptions();
            InitializeRtcModeOptions();

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
                UpdateRtcRollTypeRadioButtons(config.Rtc.RtcRollType);
                UpdateRtcModeRadioButtons(config.Rtc.RtcMode);
            }
        }

        /// <summary>
        /// 初始化WebRTC角色类型选项
        /// </summary>
        private void InitializeRtcRollTypeOptions()
        {
            if (RtcRollTypePanel == null) return;

            // 创建WebRTC角色类型转换器
            rtcRollTypeConverter = new EnumToStringConverter<RtcRollType>((type) =>
            {
                return type switch
                {
                    RtcRollType.offer => "Offer",
                    RtcRollType.answer => "Answer",
                    _ => type.ToString()
                };
            });

            // 初始化面板
            rtcRollTypeConverter.InitPanel(RtcRollTypePanel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Rtc.RtcRollType = value;
                }
            });
        }

        /// <summary>
        /// 初始化WebRTC模式选项
        /// </summary>
        private void InitializeRtcModeOptions()
        {
            if (RtcModePanel == null) return;

            // 创建WebRTC模式转换器
            rtcModeConverter = new EnumToStringConverter<RtcMode>((mode) =>
            {
                return mode switch
                {
                    RtcMode.Push => "推流",
                    RtcMode.Pull => "拉流",
                    _ => mode.ToString()
                };
            });

            // 初始化面板
            rtcModeConverter.InitPanel(RtcModePanel, DataContext, (value) =>
            {
                if (DataContext is PlayerConfig config)
                {
                    config.Rtc.RtcMode = value;
                }
            });
        }

        /// <summary>
        /// 更新WebRTC角色类型RadioButton选中状态
        /// </summary>
        private void UpdateRtcRollTypeRadioButtons(RtcRollType selectedType)
        {
            var panel = this.FindControl<StackPanel>("RtcRollTypePanel");
            if (panel == null) return;

            // 使用新方法更新UI
            rtcRollTypeConverter.UpdateUI(panel, selectedType);
        }

        /// <summary>
        /// 更新WebRTC模式RadioButton选中状态
        /// </summary>
        private void UpdateRtcModeRadioButtons(RtcMode selectedMode)
        {
            var panel = this.FindControl<StackPanel>("RtcModePanel");
            if (panel == null) return;

            // 使用新方法更新UI
            rtcModeConverter.UpdateUI(panel, selectedMode);
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

            // 验证推流URI
            if (config.Rtc.RtcMode == RtcMode.Push && string.IsNullOrWhiteSpace(config.Rtc.PushUri))
            {
                ShowErrorMessage("推流模式下必须填写推流URI");
                return false;
            }

            // 验证拉流URI
            if (config.Rtc.RtcMode == RtcMode.Pull && string.IsNullOrWhiteSpace(config.Rtc.PullUri))
            {
                ShowErrorMessage("拉流模式下必须填写拉流URI");
                return false;
            }

            return true;
        }

        /// <summary>
        /// 显示错误消息
        /// </summary>
        private void ShowErrorMessage(string message)
        {
            // 避免使用弹出窗口，使用简单的控制台输出
            // 在实际应用中，可以考虑在界面上添加一个错误消息显示区域
            Console.WriteLine($"[RtcSettingView] 错误: {message}");

            // 对于调试目的，也可以使用AvoxWrapper的日志功能
            AvoxWrapper.logMsg(LogLevel.error, $"RtcSettingView: {message}");
        }
    }
}
