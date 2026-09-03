using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace AvoxControls.MediaPlayer
{
    /// <summary>
    /// 播放器进度条控件
    /// 自定义白底黑字进度条
    /// </summary>
    public partial class PlayerControlBar : UserControl
    {
        private double currentProgress = 0;
        private bool isDragging = false;

        /// <summary>
        /// 构造函数
        /// </summary>
        public PlayerControlBar()
        {
            InitializeComponent();
            SetupInteractions();
        }

        /// <summary>
        /// 设置交互
        /// </summary>
        private void SetupInteractions()
        {
            if (ProgressTrack == null) return;

            // 鼠标按下 - 开始拖动
            ProgressTrack.PointerPressed += (s, e) =>
            {
                isDragging = true;
                UpdateProgressFromMousePosition(e.GetPosition(ProgressTrack));
                e.Pointer.Capture(ProgressTrack);
            };

            // 鼠标移动 - 拖动中
            ProgressTrack.PointerMoved += (s, e) =>
            {
                if (isDragging)
                {
                    UpdateProgressFromMousePosition(e.GetPosition(ProgressTrack));
                }
            };

            // 鼠标释放 - 结束拖动
            ProgressTrack.PointerReleased += (s, e) =>
            {
                if (isDragging)
                {
                    isDragging = false;
                    e.Pointer.Capture(null);
                    // 触发进度变化事件
                    ProgressValueChanged?.Invoke(this, System.EventArgs.Empty);
                }
            };

            // 鼠标离开 - 结束拖动
            ProgressTrack.PointerExited += (s, e) =>
            {
                if (isDragging)
                {
                    isDragging = false;
                    ProgressValueChanged?.Invoke(this, System.EventArgs.Empty);
                }
            };
        }

        /// <summary>
        /// 根据鼠标位置更新进度
        /// </summary>
        private void UpdateProgressFromMousePosition(Point position)
        {
            if (ProgressTrack == null) return;

            double width = ProgressTrack.Bounds.Width;
            if (width > 0)
            {
                double newProgress = System.Math.Max(0, System.Math.Min(100, position.X / width * 100));
                ProgressValue = newProgress;
            }
        }

        /// <summary>
        /// 更新进度显示
        /// </summary>
        private void UpdateProgressVisual()
        {
            if (ProgressFill != null && ProgressThumb != null && ProgressTrack != null)
            {
                double trackWidth = ProgressTrack.Bounds.Width;
                if (trackWidth > 0)
                {
                    double fillWidth = trackWidth * currentProgress / 100.0;
                    ProgressFill.Width = fillWidth;
                    ProgressThumb.Margin = new Thickness(fillWidth - 6, 0, 0, 0);
                }
            }
        }

        /// <summary>
        /// 是否正在拖动
        /// </summary>
        public bool IsDragging => isDragging;

        /// <summary>
        /// 进度值 (0-100)
        /// </summary>
        public double ProgressValue
        {
            get => currentProgress;
            set
            {
                if (currentProgress != value)
                {
                    currentProgress = System.Math.Max(0, System.Math.Min(100, value));
                    UpdateProgressVisual();
                }
            }
        }

        // 缓存的当前时间/总时长文本，用于更新组合显示
        private string currentTimeCache = "00:00";
        private string totalTimeCache = "00:00";

        /// <summary>
        /// 当前时间文本 (设置后自动更新组合显示)
        /// </summary>
        public string CurrentTimeText
        {
            get => currentTimeCache;
            set
            {
                currentTimeCache = value ?? "00:00";
                UpdateTimeDisplay();
            }
        }

        /// <summary>
        /// 总时长文本 (设置后自动更新组合显示)
        /// </summary>
        public string TotalTimeText
        {
            get => totalTimeCache;
            set
            {
                totalTimeCache = value ?? "00:00";
                UpdateTimeDisplay();
            }
        }

        /// <summary>
        /// 更新组合时间显示 "当前 / 总时长"
        /// </summary>
        private void UpdateTimeDisplay()
        {
            if (TimeTextBlock != null)
                TimeTextBlock.Text = $"{currentTimeCache} / {totalTimeCache}";
        }

        /// <summary>
        /// 进度变化事件
        /// </summary>
        public event System.EventHandler ProgressValueChanged;

        /// <summary>
        /// 尺寸变化时更新进度显示
        /// </summary>
        protected override void OnSizeChanged(SizeChangedEventArgs e)
        {
            base.OnSizeChanged(e);
            UpdateProgressVisual();
        }
    }
}
