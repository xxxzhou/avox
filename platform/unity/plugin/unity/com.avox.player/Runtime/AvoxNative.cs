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
        [DllImport(Lib)] public static extern IntPtr avoxGetRenderEventFunc();
        [DllImport(Lib)] public static extern int avoxGetGpuPassthroughAvailable();
        [DllImport(Lib)] public static extern int avoxGetGpuFlavor();
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
        [DllImport(Lib)] public static extern void avoxPlayerSetDx11Target(IntPtr player, IntPtr nativeTex);
        [DllImport(Lib)] public static extern void avoxPlayerGetDx11Debug(IntPtr player, out int events, out int copies, out int targetNull, out long fenceVal, out int opens);

        // ── Option ──
        [DllImport(Lib)] public static extern int avoxPlayerGetOptionType(IntPtr player, string key);
        [DllImport(Lib)] public static extern int avoxPlayerSetOptionBool(IntPtr player, string key, int value);
        [DllImport(Lib)] public static extern int avoxPlayerSetOptionInt(IntPtr player, string key, long value);
        [DllImport(Lib)] public static extern int avoxPlayerSetOptionNumber(IntPtr player, string key, double value);
        [DllImport(Lib)] public static extern int avoxPlayerSetOptionString(IntPtr player, string key, string value);
        [DllImport(Lib)] public static extern long avoxPlayerGetOptionInt(IntPtr player, string key);
        [DllImport(Lib)] public static extern double avoxPlayerGetOptionNumber(IntPtr player, string key);
        [DllImport(Lib)] public static extern int avoxPlayerGetOptionString(IntPtr player, string key, byte[] buf, int bufSize);

        // ── 录制 ──
        [DllImport(Lib)] public static extern int avoxPlayerStartRecord(IntPtr player, string path, int bTranscode);
        [DllImport(Lib)] public static extern void avoxPlayerStopRecord(IntPtr player);
        [DllImport(Lib)] public static extern int avoxPlayerGetRecordState(IntPtr player);

        // ── 字幕 ──
        [DllImport(Lib)] public static extern int avoxPlayerLoadSrt(IntPtr player, string path);
        [DllImport(Lib)] public static extern void avoxPlayerCloseSubtitle(IntPtr player);

        // ArgType 数值 (avox::ArgType)
        public const int ArgTypeNull = 0;
        public const int ArgTypeBool = 1;
        public const int ArgTypeInt = 2;
        public const int ArgTypeNumber = 3;
        public const int ArgTypeString = 4;

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
