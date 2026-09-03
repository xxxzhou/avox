package avox.samples.mediaplayer;

import android.content.Context;

public class SettingsManager {
    private static final String PREFS_NAME = "AvoxSettings";
    private static final String KEY_HARDWARE_DECODE = "hardware_decode";
    private static final String KEY_IO_PARSER = "io_parser_type";

    // 硬解配置
    public static void saveHardwareDecode(Context context, boolean enable) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit()
                .putBoolean(KEY_HARDWARE_DECODE, enable)
                .apply();
    }

    public static boolean getHardwareDecode(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .getBoolean(KEY_HARDWARE_DECODE, true);
    }

    // IO解析器配置
    public static void saveIoParserType(Context context, int ioType) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit()
                .putInt(KEY_IO_PARSER, ioType)
                .apply();
    }

    public static int getIoParserType(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .getInt(KEY_IO_PARSER, 1); // 1=默认zlmediakit解析器
    }
}
