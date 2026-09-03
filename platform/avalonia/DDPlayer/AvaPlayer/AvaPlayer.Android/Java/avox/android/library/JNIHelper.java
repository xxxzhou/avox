package avox.android.library;

import android.view.Surface;
import android.app.Activity;
import android.graphics.Bitmap;

public class JNIHelper {
    static {
        System.loadLibrary("avox");
    }
    
    private static native void jniSetup(Activity activity);
    
    public static void initJNI(Activity activity) {
        jniSetup(activity);
    }

    public static native long getNativeSurface(Surface surface);
}
