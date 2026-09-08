package avox.samples.mediaplayer;

import androidx.fragment.app.FragmentActivity;

import avox.android.library.JNIHelper;
import avox.android.library.swig.AvoxWrapper;
import avox.android.library.swig.IRtcPlayer;
import avox.android.library.swig.IRtcEventOb;
import avox.android.library.swig.RtcRollType;
import avox.android.library.wrapper.VideoRender;

import android.os.Bundle;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.EditText;
import android.view.View;
import android.util.Log;

public class RtcActivity extends FragmentActivity implements View.OnClickListener {
    private static final String TAG = "RtcActivity";
    private VideoRender videoRender = null;
    private IRtcPlayer rtcPlayer = null;
    private Button btnOpen = null;
    private Button btnClose = null;
    private EditText uri = null;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_rtc);

        btnOpen = findViewById(R.id.btnJoin);
        btnOpen.setOnClickListener(this);
        btnClose = findViewById(R.id.btnClose);
        btnClose.setOnClickListener(this);
        uri = findViewById(R.id.roomName);
        String baseUrl = getString(R.string.rtc_pull);
        String uri = baseUrl + "/index/api/webrtc?app=live&stream=test&type=play";

        try {
            // 初始化RtcPlayer
            rtcPlayer = AvoxWrapper.createWebRtcPlayer();
            Log.d(TAG, "RtcPlayer created successfully");

            // 设置为offer模式
            rtcPlayer.setRollType(RtcRollType.offer);
            Log.d(TAG, "Set roll type to offer");

            // 初始化视频渲染
            SurfaceView surfaceView = findViewById(R.id.vk_surface_view);
            videoRender = new VideoRender();
            videoRender.init(rtcPlayer.getRemoteSurfaceRender(), surfaceView, false);
            Log.d(TAG, "Video render initialized");
        } catch (Exception e) {
            Log.e(TAG, "Error initializing RtcPlayer: " + e.getMessage(), e);
        }
    }

    @Override
    public void onClick(View view) {
        if (view.getId() == R.id.btnClose) {
            // 执行关闭操作
            rtcPlayer.close();
        } else if (view.getId() == R.id.btnJoin) {
            try {
                String pullUri = uri.getText().toString().trim();
                if (pullUri.isEmpty()) {
                    Log.e(TAG, "Pull URI is empty");
                    return;
                }
                String uri = pullUri + "/index/api/webrtc?app=live&stream=test&type=play";
                Log.d(TAG, "Opening RTC connection with URI: " + uri);

                // 创建并挂载SDP Agent
                IRtcEventOb sdpAgent = AvoxWrapper.createZlTestSdpAgent(rtcPlayer, uri);
                if (sdpAgent != null) {
                    rtcPlayer.addOb(sdpAgent);
                    Log.d(TAG, "SDP Agent created and set");
                } else {
                    Log.e(TAG, "Failed to create SDP Agent");
                    return;
                }
                // 打开RTC连接
                boolean result = rtcPlayer.open();
                Log.d(TAG, "RTC open result: " + result);
            } catch (Exception e) {
                Log.e(TAG, "Error opening RTC connection: " + e.getMessage(), e);
            }
        }
    }

    @Override
    public void onBackPressed() {
        // 先执行播放器关闭
        if (rtcPlayer != null) {
            try {
                rtcPlayer.close();
                Log.d(TAG, "RTC player closed");
            } catch (Exception e) {
                Log.e(TAG, "Error closing RtcPlayer: " + e.getMessage(), e);
            }
        }
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        // 清理资源
        if (rtcPlayer != null) {
            try {
                rtcPlayer.close();
                rtcPlayer.delete();
                Log.d(TAG, "RTC player destroyed");
            } catch (Exception e) {
                Log.e(TAG, "Error destroying RtcPlayer: " + e.getMessage(), e);
            }
        }
        super.onDestroy();
    }
}
