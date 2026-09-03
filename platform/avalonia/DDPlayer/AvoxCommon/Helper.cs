using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace AvoxCommon
{
    public static class Helper
    {
        public static string GetBasePath()
        {
            string basePath = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            return basePath;
        }

        public static bool IsNetworkPath(string path)
        {
            return path.StartsWith("http://") || path.StartsWith("https://") ||
                   path.StartsWith("rtsp://") || path.StartsWith("rtmp://") ||
                   path.StartsWith("udp://") || path.StartsWith("tcp://");
        }
    }
}
