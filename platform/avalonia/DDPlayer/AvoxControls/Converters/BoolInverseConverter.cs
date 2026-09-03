using Avalonia.Data.Converters;
using System;
using System.Globalization;

namespace AvoxControls.Converters
{
    /// <summary>
    /// 布尔值反转转换器
    /// 将 true 转换为 false，false 转换为 true
    /// </summary>
    public class BoolInverseConverter : IValueConverter
    {
        /// <summary>
        /// 单例实例
        /// </summary>
        public static readonly BoolInverseConverter Instance = new BoolInverseConverter();

        /// <summary>
        /// 将源值转换为目标值
        /// </summary>
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is bool boolValue)
            {
                return !boolValue;
            }
            return value;
        }

        /// <summary>
        /// 将目标值转换回源值
        /// </summary>
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is bool boolValue)
            {
                return !boolValue;
            }
            return value;
        }
    }
}
