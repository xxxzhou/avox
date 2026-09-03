package avox.android.library;

import android.graphics.SurfaceTexture;

public class AvoxSurfaceTextureOb implements SurfaceTexture.OnFrameAvailableListener {
    // 指向C++对象的指针
    private long nativePtr = 0;
    private final Object lock = new Object();
    public AvoxSurfaceTextureOb(long ptr) {
        nativePtr = ptr;
    }
    // 底层JniSurfaceTexture在关闭前一定要调用
    // 因为回调是SurfaceTexture里线程队列在调用 
    // 不同步的话,关闭时回调时C++指针可能是野指针crash 
    public void detach() {
        synchronized (lock) {
            nativePtr = 0;
        }
    }
    @Override
    public void onFrameAvailable(SurfaceTexture surfaceTexture) {
        synchronized (lock) {
            if (nativePtr != 0) {
                nativeOnFrameAvailable(nativePtr);
            }           
        }
    }
    // 转JNI方法
    private static native void nativeOnFrameAvailable(long nativePtr);
}
