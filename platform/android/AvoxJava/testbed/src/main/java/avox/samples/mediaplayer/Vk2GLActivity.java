package avox.samples.mediaplayer;

import androidx.fragment.app.FragmentActivity;

import avox.android.library.swig.AvoxWrapper;
import avox.android.library.swig.IRenderContext;
import avox.android.library.swig.IoPlan;
import avox.android.library.wrapper.IGLRenderObserver;
import avox.android.library.wrapper.MediaPlayer;
import avox.android.library.wrapper.GLVkVideoRender;

import android.graphics.SurfaceTexture;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.view.View;

public class Vk2GLActivity extends FragmentActivity implements IGLRenderObserver, View.OnClickListener {

    private GLVkVideoRender glVkVideoRender = null;
    private MediaPlayer mediaPlayer = null;
    private Button btnOpen = null;
    private EditText uri = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_vk2gl);

        btnOpen = findViewById(R.id.btnJoin);
        btnOpen.setOnClickListener(this);
        uri = findViewById(R.id.roomName);

        mediaPlayer = new MediaPlayer();
        glVkVideoRender = new GLVkVideoRender();
        GLSurfaceView glSurfaceView = findViewById(R.id.es_surface_view);
        glVkVideoRender.init(glSurfaceView,this);
        // SurfaceTexture
    }

    @Override
    public void renderTex(IRenderContext glesContext) {
        if(mediaPlayer != null){
            // Vk输出到HarderBuffer,HarderBuffer通过glEGLImageTargetTexture2DOES到纹理
           AvoxWrapper.renderContext(mediaPlayer.Player.getSurfaceRender(),glesContext);
        }
    }

    @Override
    public void onClick(View view) {
        IoPlan ioPlan = IoPlan.swigToEnum(SettingsManager.getIoParserType(this));
        mediaPlayer.Player.setIoPlan(ioPlan);
        mediaPlayer.Player.setHardDecode(SettingsManager.getHardwareDecode(this));
        // openUri(uri.getText().toString());
        mediaPlayer.Open(uri.getText().toString());
    }
    @Override
    public void onBackPressed() {
        // 先执行播放器关闭
        if (mediaPlayer != null) {
            mediaPlayer.Close();
        }
        super.onBackPressed();
    }
}