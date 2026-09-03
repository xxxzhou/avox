using Avalonia.Data.Converters;
using AvoxCommon;
using System;
using System.Globalization;

namespace AvoxControls.Converters
{
    /// <summary>
    /// 枚举到布尔值的转换器，用于 RadioButton 绑定
    /// ConverterParameter 指定要比较的枚举值字符串
    /// </summary>
    public class EnumToBoolConverter : IValueConverter
    {
        public static EnumToBoolConverter Instance { get; } = new EnumToBoolConverter();

        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value == null || parameter == null)
                return false;

            string paramStr = parameter.ToString();

            // 处理 base_ 的特殊情况（SWIG 生成的枚举值用 base_ 表示 base）
            if (paramStr == "base" && value.ToString() == "base_")
                return true;

            return value.ToString().Equals(paramStr, StringComparison.OrdinalIgnoreCase);
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is bool boolValue && boolValue && parameter != null)
            {
                string paramStr = parameter.ToString();

                // 处理 base 的特殊情况
                if (paramStr == "base")
                    return AvoxNet.ModelLevel.base_;

                // 尝试解析为枚举
                if (Enum.TryParse(typeof(AvoxNet.Language), paramStr, true, out var langResult))
                    return langResult;

                if (Enum.TryParse(typeof(AvoxNet.ModelLevel), paramStr, true, out var modelResult))
                    return modelResult;
            }
            return Avalonia.Data.BindingOperations.DoNothing;
        }
    }
}