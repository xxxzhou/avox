package avox.android.library;

import android.app.Activity;
import android.graphics.Bitmap;
import android.util.Log;
import android.view.Surface;

import avox.android.library.swig.ILogOb;
import avox.android.library.swig.IVInputLayer;

import avox.android.library.swig.*;

public class JNIHelper {
    static{
        System.loadLibrary("c++_shared");
        System.loadLibrary("avox_java");
        System.loadLibrary("avox");
    }
    private static native void jniSetup(Activity activity);
    // 一是引起loadLibrary对应的jni_onload,二是传入当前active
    public static void initJNI(Activity activity){
        jniSetup(activity);
        // AvoxWrapper.setLogObserver(new AndLog());
    }
    private static native boolean loadBitmap(long inputLayer, Bitmap bitmap);
    public static boolean loadBitmap(IVInputLayer inputLayer, Bitmap bitmap) {
        return loadBitmap(IVInputLayer.getCPtr(inputLayer), bitmap);
    }

    private static native int add(int a,int b);
    public static int test(int a,int b){
        return add(a,b);
    }

    private static native void setRenderSurface(long vkWindow, Surface surface);
    public static void setRenderSurface(ISurfaceRender wRender, Surface surface){
        setRenderSurface(ISurfaceRender.getCPtr(wRender),surface);
    }

    private static class AndLog extends ILogOb {
        private static final String TAG = "avox native";
        @Override
        public void onLogEvent(int level,String msg){
            switch (level) {
                case 0:
                    Log.i(TAG, msg);
                    break;
                case 1:
                    Log.w(TAG, msg);
                    break;
                case 2:
                    Log.e(TAG, msg);
                    break;
                case 3:
                    Log.d(TAG, msg);
                    break;
                default:
                    Log.i(TAG, msg);
            }
        }
    }
}
