using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Avox
{
    // avox_unity.dll P/Invoke 绑定 (与 platform/unity/plugin/src/AvoxUnityApi.h 一一对应)
    internal static class AvoxNative
    {
        const string Lib = "avox_unity";

        // ── 全局 ──
        [DllImport(Lib)] public static extern IntPtr avoxGetTextureUpdateCallback();
        [DllImport(Lib)] public static extern int avoxGetGpuPassthroughAvailable();
        [DllImport(Lib)] public static extern IntPtr avoxGetVersion();

        // ── 播放器 ──
        [DllImport(Lib)] public static extern IntPtr avoxPlayerCreate();
        [DllImport(Lib)] public static extern void avoxPlayerDestroy(IntPtr player);
        [DllImport(Lib)] public static extern uint avoxPlayerGetId(IntPtr player);
        [DllImport(Lib)] public static extern void avoxPlayerSetHardDecode(IntPtr player, int enable);
        [DllImport(Lib)] public static extern void avoxPlayerSetVolume(IntPtr player, float volume);
        [DllImport(Lib)] public static extern void avoxPlayerSetIoPlan(IntPtr player, int plan);
        [DllImport(Lib)] public static extern void avoxPlayerSetSpeed(IntPtr player, double speed);
        [DllImport(Lib)] public static extern void avoxPlayerOpen(IntPtr player, string url);
        [DllImport(Lib)] public static extern void avoxPlayerClose(IntPtr player);
        [DllImport(Lib)] public static extern void avoxPlayerPause(IntPtr player);
        [DllImport(Lib)] public static extern void avoxPlayerResume(IntPtr player);
        [DllImport(Lib)] public static extern void avoxPlayerSeek(IntPtr player, long pos);
        [DllImport(Lib)] public static extern int avoxPlayerGetState(IntPtr player);
        [DllImport(Lib)] public static extern long avoxPlayerGetDuration(IntPtr player);
        [DllImport(Lib)] public static extern long avoxPlayerGetPosition(IntPtr player);
        [DllImport(Lib)] public static extern double avoxPlayerGetProgress(IntPtr player);
        [DllImport(Lib)] public static extern int avoxPlayerPollEvent(IntPtr player, ref NativeEvent ev);
        [DllImport(Lib)] public static extern int avoxPlayerGetFrameInfo(IntPtr player, out int w, out int h);
        [DllImport(Lib)] public static extern int avoxPlayerIsGpuMode(IntPtr player);
        [DllImport(Lib)] public static extern ulong avoxPlayerGetExternalTexture(IntPtr player);
        [DllImport(Lib)] public static extern void avoxPlayerUpdateGpu(IntPtr player);

        // 事件类型 (AvoxUnityEvent::EType)
        public const int EventTypeState = 1;
        public const int EventTypeReady = 2;
        public const int EventTypeComplete = 3;
        public const int EventTypeError = 4;

        // 与 native AvoxUnityEvent 布局一致
        [StructLayout(LayoutKind.Sequential)]
        public unsafe struct NativeEvent
        {
            public int type;
            public int state;
            public int code;
            public int reserved;
            public fixed byte msg[240];

            public string Message
            {
                get
                {
                    fixed (byte* p = msg)
                    {
                        int n = 0;
                        while (n < 240 && p[n] != 0) n++;
                        return Encoding.UTF8.GetString(p, n);
                    }
                }
            }
        }
    }
}
