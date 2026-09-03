package avox.samples.mediaplayer;

import androidx.fragment.app.FragmentActivity;

import avox.android.library.swig.IoPlan;
import avox.android.library.wrapper.MediaPlayer;
import avox.android.library.wrapper.VideoRender;

import android.os.Bundle;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.EditText;
import android.view.View;

public class VkActivity extends FragmentActivity implements View.OnClickListener {
    private VideoRender videoRender = null;
    private MediaPlayer mediaPlayer = null;
    private Button btnOpen = null;
    private EditText uri = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        btnOpen = findViewById(R.id.btnJoin);
        btnOpen.setOnClickListener(this);
        uri = findViewById(R.id.roomName);

        mediaPlayer = new MediaPlayer();
        SurfaceView surfaceView = findViewById(R.id.vk_surface_view);
        videoRender = new VideoRender();
        videoRender.init(mediaPlayer.Player.getSurfaceRender(),surfaceView,true);
    }

    @Override
    public void onClick(View view) {
        IoPlan ioPlan = IoPlan.swigToEnum(SettingsManager.getIoParserType(this));
        mediaPlayer.Player.setIoPlan(ioPlan);
        mediaPlayer.Player.setHardDecode(true);
        // https://tz-cmds4.gateway.zjcloud.com:50443/live/1812395_6_0_/hls.m3u8?auth_key=1781331073-0435c4c8820cf8bda1589b0d2d1a163e-c88856b7929207504aa4a20cc5697a6dd247517a4eafe2893cc415b3b86f66f73a1c2002afd9402240ae09a6e6c34f4c7fee647dbbca97f8f7f27fd98b11cb2e4588e124fb20ccf44e15e23d6f558d3a4ab286baae4729d64ce69f0593d3800b50a9dc166ef64b932b4f6528ec51a61f92fc9ec0a6d9513cd409d76c2ed10c6a024a9317c9c41d8a46c45e08ee508b0a7cfb3864e03f530471aaf9798b06856fd4d663d7dd177e8b4c4c76e2ecbcd922cb3acfff07dcb7b99e214e157c7aee4f13ff95f6fe20db25224fd195e58ac3b4856b3dbbb35be9ef4ad3c1a6fe7e3b27090145d220adec8ae8a502ffe9c1fe417ce2e0d2300421bd22bd127a32ddbb52aa4ecea7a6d9c725f02064ba3cd48d91a05ed28f4feb8f0281649c18a75ea5fda486927d689e7873&expire_type=static&enable_audio=0
        // uri.getText().toString()
        mediaPlayer.Player.open(uri.getText().toString());
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