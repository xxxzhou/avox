using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using AvoxCommon;
using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;
using Avalonia.Data;

namespace AvoxControls
{
    /// <summary>
    /// 带历史记录下拉功能的文本框
    /// </summary>
    public partial class HistoryTextBox : UserControl
    {
        private TextBox mainTextBox;
        private Popup historyPopup;
        private HistoryList historyList;

        /// <summary>
        /// 历史记录列表
        /// </summary>
        public HistoryList HistoryList
        {
            get => historyList;
            set
            {
                if (historyList != null)
                {
                    // 取消之前的事件订阅
                    historyList.PropertyChanged -= OnHistoryListChanged;
                }

                if (historyList != value)
                {
                    historyList = value;
                    if (historyList != null)
                    {
                        LoadHistoryItems();
                        // 订阅 PropertyChanged 事件，监听 Items 变化
                        historyList.PropertyChanged += OnHistoryListChanged;
                    }
                }
            }
        }

        /// <summary>
        /// 文本框占位符
        /// </summary>
        public string Watermark
        {
            get => GetValue(WatermarkProperty);
            set => SetValue(WatermarkProperty, value);
        }

        public static readonly StyledProperty<string> WatermarkProperty =
            AvaloniaProperty.Register<HistoryTextBox, string>(nameof(Watermark));

        /// <summary>
        /// 文本框内容
        /// </summary>
        public string Text
        {
            get => GetValue(TextProperty);
            set => SetValue(TextProperty, value);
        }

        public static readonly StyledProperty<string> TextProperty =
            AvaloniaProperty.Register<HistoryTextBox, string>(nameof(Text), defaultBindingMode: BindingMode.TwoWay, defaultValue: "");

        /// <summary>
        /// 历史记录项集合
        /// </summary>
        public ObservableCollection<HistoryItemViewModel> HistoryItems { get; } = new ObservableCollection<HistoryItemViewModel>();

        /// <summary>
        /// 确认命令
        /// </summary>
        public ICommand ConfirmCommand { get; }

        /// <summary>
        /// 取消命令
        /// </summary>
        public ICommand CancelCommand { get; }

        /// <summary>
        /// 删除历史记录项命令
        /// </summary>
        public ICommand DeleteCommand { get; }

        /// <summary>
        /// 是否打开下拉弹窗
        /// </summary>
        public bool IsPopupOpen
        {
            get => GetValue(IsPopupOpenProperty);
            set => SetValue(IsPopupOpenProperty, value);
        }

        public static readonly StyledProperty<bool> IsPopupOpenProperty =
            AvaloniaProperty.Register<HistoryTextBox, bool>(nameof(IsPopupOpen));

        /// <summary>
        /// 历史记录项选中事件
        /// </summary>
        public static readonly RoutedEvent<HistoryItemEventArgs> HistoryItemSelectedEvent =
            RoutedEvent.Register<HistoryTextBox, HistoryItemEventArgs>(nameof(HistoryItemSelected), RoutingStrategies.Bubble);

        /// <summary>
        /// 历史记录项选中事件
        /// </summary>
        public event EventHandler<HistoryItemEventArgs> HistoryItemSelected
        {
            add => AddHandler(HistoryItemSelectedEvent, value);
            remove => RemoveHandler(HistoryItemSelectedEvent, value);
        }

        /// <summary>
        /// 历史记录项删除事件
        /// </summary>
        public static readonly RoutedEvent<HistoryItemEventArgs> HistoryItemDeletedEvent =
            RoutedEvent.Register<HistoryTextBox, HistoryItemEventArgs>(nameof(HistoryItemDeleted), RoutingStrategies.Bubble);

        /// <summary>
        /// 历史记录项删除事件
        /// </summary>
        public event EventHandler<HistoryItemEventArgs> HistoryItemDeleted
        {
            add => AddHandler(HistoryItemDeletedEvent, value);
            remove => RemoveHandler(HistoryItemDeletedEvent, value);
        }

        public HistoryTextBox()
        {
            InitializeComponent();
            mainTextBox = this.FindControl<TextBox>("MainTextBox");
            historyPopup = this.FindControl<Popup>("HistoryPopup");

            // 设置 DataContext 为自身，确保绑定正确
            this.DataContext = this;

            ConfirmCommand = new ActionCommand(() => OnConfirm());
            CancelCommand = new ActionCommand(() => OnCancel());
            DeleteCommand = new ActionCommand<HistoryItemViewModel>(vm => OnDeleteItem(vm));

            // 文本框获得焦点时显示历史记录
            mainTextBox.GotFocus += (s, e) => ShowHistoryPopup();
            mainTextBox.PointerPressed += OnTextBoxPointerPressed;
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
        }

        /// <summary>
        /// 显示历史记录弹窗
        /// </summary>
        private void ShowHistoryPopup()
        {
            if (historyList != null && historyList.Items.Count > 0)
            {
                LoadHistoryItems();
                IsPopupOpen = true;
                System.Diagnostics.Debug.WriteLine($"[HistoryTextBox] ShowHistoryPopup: count={historyList.Items.Count}");
            }
            else
            {
                System.Diagnostics.Debug.WriteLine($"[HistoryTextBox] ShowHistoryPopup skipped: historyList={historyList != null}, count={historyList?.Items.Count ?? 0}");
            }
        }

        /// <summary>
        /// 加载历史记录项
        /// </summary>
        private void LoadHistoryItems()
        {
            if (historyList == null) return;

            HistoryItems.Clear();
            foreach (var item in historyList.Items)
            {
                HistoryItems.Add(new HistoryItemViewModel(item));
            }
        }

        /// <summary>
        /// 历史记录列表属性变化事件处理
        /// </summary>
        private void OnHistoryListChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            System.Diagnostics.Debug.WriteLine($"[HistoryTextBox] OnHistoryListChanged: {e.PropertyName}, items count={historyList?.Items.Count ?? 0}");

            if (e.PropertyName == nameof(HistoryList.Items))
            {
                LoadHistoryItems();
                System.Diagnostics.Debug.WriteLine($"[HistoryTextBox] Items reloaded, UI HistoryItems count={HistoryItems.Count}");
            }
        }

        /// <summary>
        /// 文本框点击事件
        /// </summary>
        private void OnTextBoxPointerPressed(object sender, PointerPressedEventArgs e)
        {
            ShowHistoryPopup();
        }

        /// <summary>
        /// 历史记录项点击事件
        /// </summary>
        private void OnHistoryItemTapped(object sender, PointerPressedEventArgs e)
        {
            if (sender is Border border && border.DataContext is HistoryItemViewModel vm)
            {
                Text = vm.OriginalItem.Id;
                IsPopupOpen = false;

                // 触发历史记录项选中事件
                RaiseEvent(new HistoryItemEventArgs(HistoryItemSelectedEvent, vm.OriginalItem));
            }
        }

        /// <summary>
        /// 确认（添加到历史记录）
        /// </summary>
        private void OnConfirm()
        {
            var text = Text?.Trim();
            if (!string.IsNullOrEmpty(text))
            {
                // 添加或更新历史记录
                if (historyList != null)
                {
                    historyList.AddOrUpdate(text, text);
                }

                // 触发确认事件
                RaiseEvent(new RoutedEventArgs(ConfirmedEvent));

                IsPopupOpen = false;
            }
        }

        /// <summary>
        /// 取消
        /// </summary>
        private void OnCancel()
        {
            IsPopupOpen = false;
        }

        /// <summary>
        /// 删除历史记录项
        /// </summary>
        private void OnDeleteItem(HistoryItemViewModel vm)
        {
            if (vm != null && historyList != null)
            {
                historyList.Remove(vm.OriginalItem.Id);
                LoadHistoryItems();

                // 触发删除事件
                RaiseEvent(new HistoryItemEventArgs(HistoryItemDeletedEvent, vm.OriginalItem));
            }
        }

        /// <summary>
        /// 确认事件
        /// </summary>
        public static readonly RoutedEvent<RoutedEventArgs> ConfirmedEvent =
            RoutedEvent.Register<HistoryTextBox, RoutedEventArgs>(nameof(Confirmed), RoutingStrategies.Bubble);

        /// <summary>
        /// 确认事件
        /// </summary>
        public event EventHandler<RoutedEventArgs> Confirmed
        {
            add => AddHandler(ConfirmedEvent, value);
            remove => RemoveHandler(ConfirmedEvent, value);
        }
    }

    /// <summary>
    /// 历史记录项视图模型
    /// </summary>
    public class HistoryItemViewModel
    {
        /// <summary>
        /// 显示文本
        /// </summary>
        public string DisplayText { get; }

        /// <summary>
        /// 原始历史记录项
        /// </summary>
        public AvoxCommon.HistoryItem OriginalItem { get; }

        /// <summary>
        /// 访问时间
        /// </summary>
        public string AccessTime => OriginalItem.LastAccessed.ToString("yyyy-MM-dd HH:mm:ss");

        public HistoryItemViewModel(AvoxCommon.HistoryItem item)
        {
            OriginalItem = item ?? throw new ArgumentNullException(nameof(item));
            DisplayText = !string.IsNullOrEmpty(item.Name) ? $"{item.Name} ({item.Id})" : item.Id;
        }
    }

    /// <summary>
    /// 历史记录项事件参数
    /// </summary>
    public class HistoryItemEventArgs : RoutedEventArgs
    {
        /// <summary>
        /// 历史记录项
        /// </summary>
        public AvoxCommon.HistoryItem HistoryItem { get; }

        public HistoryItemEventArgs(RoutedEvent routedEvent, AvoxCommon.HistoryItem historyItem)
            : base(routedEvent)
        {
            HistoryItem = historyItem ?? throw new ArgumentNullException(nameof(historyItem));
        }
    }
}

