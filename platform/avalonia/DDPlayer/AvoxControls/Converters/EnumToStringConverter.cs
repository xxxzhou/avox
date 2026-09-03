using Avalonia.Data.Converters;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace AvoxControls.Converters
{
    /// <summary>
    /// 通用枚举到字符串转换器（泛型）
    /// 支持自定义转换函数，缓存结果，双向转换
    /// 适用于 ComboBox、RadioButton 等多选一场景
    /// </summary>
    /// <typeparam name="TEnum">枚举类型</typeparam>
    public class EnumToStringConverter<TEnum> : IValueConverter where TEnum : struct, Enum
    {
        private Func<TEnum, string> enumToStringFunc;
        private Dictionary<TEnum, string> enumToStringCache;
        private Dictionary<string, TEnum> stringToEnumCache;
        private List<string> stringValues;
        private bool isInitialized;

        /// <summary>
        /// 是否排除第一个枚举值（通常是 none=0）
        /// </summary>
        public bool ExcludeFirstItem { get; set; }

        /// <summary>
        /// 获取所有枚举值对应的字符串列表（用于 ItemsSource）
        /// </summary>
        public List<string> StringValues
        {
            get
            {
                EnsureInitialized();
                return stringValues;
            }
        }

        /// <summary>
        /// 获取所有枚举值（用于遍历）
        /// </summary>
        public List<TEnum> EnumValues
        {
            get
            {
                EnsureInitialized();
                return new List<TEnum>(enumToStringCache.Keys);
            }
        }

        /// <summary>
        /// 默认构造函数（使用 ToString）
        /// </summary>
        public EnumToStringConverter()
        {
            enumToStringFunc = e => e.ToString();
        }

        /// <summary>
        /// 带转换函数的构造函数
        /// </summary>
        /// <param name="converter">枚举转字符串的函数</param>
        public EnumToStringConverter(Func<TEnum, string> converter)
        {
            enumToStringFunc = converter ?? throw new ArgumentNullException(nameof(converter));
        }

        /// <summary>
        /// 设置转换函数（用于 XAML 初始化后设置）
        /// </summary>
        public void SetConverter(Func<TEnum, string> converter)
        {
            enumToStringFunc = converter ?? throw new ArgumentNullException(nameof(converter));
            isInitialized = false;
        }

        /// <summary>
        /// 确保缓存已初始化
        /// </summary>
        private void EnsureInitialized()
        {
            if (isInitialized) return;
            InitializeCache();
        }

        /// <summary>
        /// 初始化缓存
        /// </summary>
        private void InitializeCache()
        {
            enumToStringCache = new Dictionary<TEnum, string>();
            stringToEnumCache = new Dictionary<string, TEnum>();
            stringValues = new List<string>();

            var values = Enum.GetValues<TEnum>();
            bool isFirst = true;

            foreach (var value in values)
            {
                if (ExcludeFirstItem && isFirst)
                {
                    isFirst = false;
                    continue;
                }
                isFirst = false;

                string str = enumToStringFunc(value);
                enumToStringCache[value] = str;
                stringToEnumCache[str] = value;
                stringValues.Add(str);
            }

            isInitialized = true;
        }

        /// <summary>
        /// 枚举转换为字符串（用于显示）
        /// </summary>
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            EnsureInitialized();

            if (value is TEnum enumValue)
            {
                return enumToStringCache.TryGetValue(enumValue, out string str) ? str : enumToStringFunc(enumValue);
            }
            return value?.ToString() ?? string.Empty;
        }

        /// <summary>
        /// 字符串转换回枚举（用于选择后反向绑定）
        /// </summary>
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            EnsureInitialized();

            if (value is string str)
            {
                return stringToEnumCache.TryGetValue(str, out TEnum enumValue) ? enumValue : default(TEnum);
            }
            return default(TEnum);
        }

        /// <summary>
        /// 根据字符串获取枚举值
        /// </summary>
        public TEnum GetEnumFromString(string str)
        {
            EnsureInitialized();
            return stringToEnumCache.TryGetValue(str, out TEnum value) ? value : default;
        }

        /// <summary>
        /// 根据枚举值获取字符串
        /// </summary>
        public string GetStringFromEnum(TEnum enumValue)
        {
            EnsureInitialized();
            return enumToStringCache.TryGetValue(enumValue, out string str) ? str : enumToStringFunc(enumValue);
        }

        /// <summary>
        /// 初始化面板，动态创建RadioButton
        /// </summary>
        /// <param name="panel">要添加RadioButton的面板</param>
        /// <param name="dataContext">数据上下文，用于绑定配置</param>
        /// <param name="propertySetter">属性设置器，用于更新配置</param>
        public void InitPanel(Avalonia.Controls.Panel panel, object dataContext, Action<TEnum> propertySetter)
        {
            EnsureInitialized();
            if (panel == null) return;

            // 清空面板
            panel.Children.Clear();

            // 动态创建 RadioButton
            foreach (var enumValue in EnumValues)
            {
                var radioButton = new Avalonia.Controls.RadioButton
                {
                    Content = GetStringFromEnum(enumValue),
                    GroupName = typeof(TEnum).Name + "Group",
                    Tag = enumValue
                };

                // 绑定 IsChecked
                radioButton.IsCheckedChanged += (s, e) =>
                {
                    if (radioButton.IsChecked == true)
                    {
                        propertySetter?.Invoke(enumValue);
                    }
                };

                panel.Children.Add(radioButton);
            }
        }

        /// <summary>
        /// 更新UI，设置选中的RadioButton
        /// </summary>
        /// <param name="panel">包含RadioButton的面板</param>
        /// <param name="value">要选中的枚举值</param>
        public void UpdateUI(Avalonia.Controls.Panel panel, TEnum value)
        {
            if (panel == null) return;

            foreach (Avalonia.Controls.RadioButton radio in panel.Children.OfType<Avalonia.Controls.RadioButton>())
            {
                if (radio.Tag is TEnum enumValue)
                {
                    radio.IsChecked = (enumValue.Equals(value));
                }
            }
        }
    }
}
