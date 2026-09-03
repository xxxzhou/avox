using AvoxNet;
using System.Diagnostics;

namespace AvoxCommon
{
    /// <summary>
    /// 日志观察者实现
    /// 不依赖特定 UI 框架，可直接在控制台或调试输出
    /// </summary>
    public class AvoxLog : ILogOb
    {
        /// <summary>
        /// 日志级别前缀
        /// </summary>
        private static string GetLevelPrefix(LogLevel level)
        {
            return level switch
            {
                LogLevel.debug => "[DEBUG]",
                LogLevel.info => "[INFO]",
                LogLevel.warn => "[WARN]",
                LogLevel.error => "[ERROR]",
                _ => "[UNKNOWN]"
            };
        }

        public override void onLogEvent(int level, string message)
        {
            // 格式化日志消息，包含时:分:秒:毫秒
            string timestamp = DateTime.Now.ToString("HH:mm:ss.fff");
            string logMessage = $"{timestamp} {GetLevelPrefix((LogLevel)level)} {message}";

            // 输出到调试窗口
            Trace.WriteLine(logMessage);
        }
    }
}
