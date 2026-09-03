package avox.samples.mediaplayer;

import android.Manifest;
import android.content.pm.PackageManager;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.Toast;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.fragment.app.FragmentActivity;

import avox.android.library.swig.ADeviceSdk;
import avox.android.library.swig.AvoxWrapper;
import avox.android.library.swig.IAVSource;
import avox.android.library.swig.IAudioManager;
import avox.android.library.swig.IAudioRender;
import avox.android.library.swig.IAudioSource;
import avox.android.library.swig.IMediaMuxer;
import avox.android.library.swig.ISourcePlayer;
import avox.android.library.swig.ISubtitle;
import avox.android.library.swig.IVideoManager;
import avox.android.library.swig.IVideoSource;
import avox.android.library.swig.ISurfaceRender;
import avox.android.library.swig.IoPlan;
import avox.android.library.swig.MuxerType;
import avox.android.library.swig.PlayerState;
import avox.android.library.swig.VCodecId;
import avox.android.library.swig.VDeviceSdk;
import avox.android.library.wrapper.VideoRender;

public class AVSource extends FragmentActivity implements View.OnClickListener {

    private Button btnAudioStart = null;
    private Button btnAudioStop = null;
    private Button btnPushStart = null;
    private Button btnPushStop = null;
    private Button btnSttStart = null;
    private Button btnSttStop = null;

    private IAudioSource audioSource = null;
    private IVideoSource videoSource = null;
    private IAVSource deviceSource = null;
    private ISourcePlayer sourcePlayer = null;
    private IMediaMuxer mediaMuxer = null;
    private VideoRender videoRender = null;
    private ISubtitle subtitle = null;
    private IAudioRender audioRender = null;
    private ISurfaceRender windowRender = null;

    private int vindex = 1;

    // 在类的顶部添加这些变量定义
    private static final int REQUEST_RECORD_AUDIO_PERMISSION = 200;
    // 定义相机权限请求码
    private static final int CAMERA_PERMISSION_REQUEST_CODE = 100;
    private boolean permissionToRecordAccepted = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_avsource);
        // 检查录音权限
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.RECORD_AUDIO},
                    REQUEST_RECORD_AUDIO_PERMISSION);
        }
        // 检查相机权限
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED) {
            // 请求相机权限
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.CAMERA},
                    CAMERA_PERMISSION_REQUEST_CODE);
        }

        btnAudioStart = findViewById(R.id.btnAudioStart);
        btnAudioStart.setOnClickListener(this);
        btnAudioStop = findViewById(R.id.btnAudioStop);
        btnAudioStop.setOnClickListener(this);
        btnPushStart = findViewById(R.id.btnPushStart);
        btnPushStart.setOnClickListener(this);
        btnPushStop = findViewById(R.id.btnPushStop);
        btnPushStop.setOnClickListener(this);
        btnSttStart = findViewById(R.id.btnSttStart);
        btnSttStart.setOnClickListener(this);
        btnSttStop = findViewById(R.id.btnSttStop);
        btnSttStop.setOnClickListener(this);
        //
        SurfaceView surfaceView = findViewById(R.id.surface_view);
        // 声明源播放器并绑定设备
        sourcePlayer = AvoxWrapper.createDevicePlayer();
        videoRender = new VideoRender();
        videoRender.init(sourcePlayer.getSurfaceRender(),surfaceView,true);
        // AudioRecord
        IAudioManager audioManager = AvoxWrapper.getAudioManager(ADeviceSdk.android);
        audioSource = audioManager.getDevice(0);
        // Camera
        IVideoManager videoManager = AvoxWrapper.getVideoManager(VDeviceSdk.and_ndkcamer2);
        videoSource = videoManager.getDevice(0);
        // 声明设备并绑定音频设备
        sourcePlayer.setVideoSource(videoSource);
        sourcePlayer.setAudioSource(audioSource);
        // 媒体复用
        mediaMuxer = sourcePlayer.getMuxer();
        audioRender = sourcePlayer.getAudioRender();
        // 初始化 SherpaText 语音识别
        subtitle = sourcePlayer.getSubtitle();
    }

    @Override
    public void onClick(View v) {
        if(v.getId() == R.id.btnAudioStart){
            if(sourcePlayer.getState() == PlayerState.playing){
                IVideoManager videoManager = AvoxWrapper.getVideoManager(VDeviceSdk.and_ndkcamer2);
                sourcePlayer.setVideoSource(videoManager.getDevice(vindex));
                vindex = (++vindex)%2;
            }else {
                sourcePlayer.open();
            }
        }else if(v.getId() == R.id.btnAudioStop){
            sourcePlayer.close();
        }else if(v.getId() == R.id.btnPushStart){
            mediaMuxer.setHardEncode(true);
            IoPlan ioPlan = IoPlan.swigToEnum(SettingsManager.getIoParserType(this));
            if(ioPlan == IoPlan.ffmpeg){
                mediaMuxer.setMuxerType(MuxerType.ffmpeg);
            }else {
                mediaMuxer.setMuxerType(MuxerType.zlmediakit);
            }
            mediaMuxer.setVideoCodec(VCodecId.h265);
            mediaMuxer.open(getString(R.string.rtmp_text));
        }else if(v.getId() == R.id.btnPushStop){
            mediaMuxer.close();
        }else if(v.getId() == R.id.btnSttStart){
            // 启动语音识别
            subtitle.enableAsr();
        }else if(v.getId() == R.id.btnSttStop){
            subtitle.close();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_RECORD_AUDIO_PERMISSION) {
            permissionToRecordAccepted = grantResults[0] == PackageManager.PERMISSION_GRANTED;
            if (!permissionToRecordAccepted) {
                Toast.makeText(this, "录音权限被拒绝", Toast.LENGTH_LONG).show();
            }
        }
        if (requestCode == CAMERA_PERMISSION_REQUEST_CODE) {
            permissionToRecordAccepted = grantResults[0] == PackageManager.PERMISSION_GRANTED;
            if (!permissionToRecordAccepted) {
                Toast.makeText(this, "相机权限被拒绝", Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override
    public void onBackPressed() {
        //
        sourcePlayer.close();
        super.onBackPressed();
    }

}