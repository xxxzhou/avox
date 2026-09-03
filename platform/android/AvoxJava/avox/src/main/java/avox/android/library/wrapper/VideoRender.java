package avox.android.library.wrapper;

import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import avox.android.library.JNIHelper;
import avox.android.library.swig.ISurfaceRender;

// 硬解的OpenGL直接渲染窗口
public class VideoRender implements SurfaceHolder.Callback{
    private ISurfaceRender windowRender = null;
    private boolean bVulkan = false;

    public void init(ISurfaceRender windowRender_, SurfaceView surface,boolean bVulkan_){
        windowRender = windowRender_;
        bVulkan = bVulkan_;
        surface.getHolder().addCallback(this);
    }
    @Override
    public void surfaceCreated(SurfaceHolder surfaceHolder) {
        Log.i("avox", "surfaceCreated create surface");
        windowRender.setVulkan(bVulkan);
        JNIHelper.setRenderSurface(windowRender,surfaceHolder.getSurface());        
    }
    @Override
    public void surfaceChanged(SurfaceHolder surfaceHolder,int format, int width, int height) {
        Log.i("avox", "surfaceChanged: width:"+width+" height:"+height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
        Log.i("avox", "surfaceDestroyed");
        JNIHelper.setRenderSurface(windowRender,null);
    }

}
