package avox.samples.mediaplayer;

import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import avox.android.library.JNIHelper;

// 播放回归矩阵的带画面宿主 (对齐 iOS avoxtest): SurfaceView 渲染各拉流用例,
// 顶部半透明横幅滚动最近判定行, 日志落盘 app 外部 files 目录 pm_log.txt。
// 用法: 默认拉 192.168.68.245 的 ZLM; adb shell am start ... --es host <ip> 可换。
// 本地源 (file-* 用例) 首次启动从 assets/video 拷到 filesDir; 缺了会 FAIL 对应用例
public class PlayMatrixActivity extends AppCompatActivity {

    // 开发机 ZLM (推流: script/testenv/push_streams.py), intent --es host 覆盖
    private static final String DEFAULT_HOST = "192.168.68.245";

    private TextView mBanner;
    private final StringBuilder mLines = new StringBuilder();
    private volatile boolean mRunning = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FrameLayout root = new FrameLayout(this);
        SurfaceView surfaceView = new SurfaceView(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mBanner = new TextView(this);
        mBanner.setTextColor(Color.WHITE);
        mBanner.setBackgroundColor(Color.argb(140, 0, 0, 0));
        mBanner.setTextSize(11f);
        mBanner.setTypeface(Typeface.MONOSPACE);
        mBanner.setPadding(12, 24, 12, 12);
        mBanner.setGravity(Gravity.BOTTOM);
        FrameLayout.LayoutParams bannerLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP);
        root.addView(mBanner, bannerLp);
        setContentView(root);
        banner("host=" + host() + "  等待 Surface...");

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // 只在首次有效 surface 启动一次; 矩阵全程约 2 分钟, 期间别退出页面
                if (!mRunning) {
                    mRunning = true;
                    startMatrix(holder);
                }
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
            }
        });
    }

    private String host() {
        Intent it = getIntent();
        return it != null && it.getStringExtra("host") != null
                ? it.getStringExtra("host") : DEFAULT_HOST;
    }

    // 本地源: 首次从 assets 拷到 filesDir (矩阵按文件路径打开, assets 直读不行)
    // assets/video 由 testbed build.gradle 的 srcDirs 指向仓库 assets/video
    private String assetFile(String name) {
        java.io.File dst = new java.io.File(getFilesDir(), name);
        if (!dst.exists() || dst.length() == 0) {
            try (java.io.InputStream in = getAssets().open(name)) {
                try (java.io.OutputStream out = new java.io.FileOutputStream(dst)) {
                    byte[] buf = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        out.write(buf, 0, n);
                    }
                }
            } catch (Exception e) {
                return "";  // 缺源: 对应 file-* 用例会 FAIL, 其余照跑
            }
        }
        return dst.getAbsolutePath();
    }

    private void startMatrix(SurfaceHolder holder) {
        String outDir = getExternalFilesDir(null) != null
                ? getExternalFilesDir(null).getAbsolutePath() : getFilesDir().getAbsolutePath();
        banner("日志: " + outDir + "/pm_log.txt");
        new Thread(() -> {
            int code = JNIHelper.runPlayMatrix(holder.getSurface(), host(), outDir,
                    assetFile("webrtc_pull.mp4"), assetFile("avox_electron.mp4"), "",
                    new JNIHelper.PmCallback() {
                        @Override
                        public void onLine(String line) {
                            if (line.startsWith("[AVOX][TEST]")) {
                                banner(line);
                            }
                        }

                        @Override
                        public void onDone(int code1) {
                            banner("=== 矩阵结束 code=" + code1 + " (0=全过) ===");
                        }
                    });
            mRunning = false;
            if (code != 0) {
                banner("!!! 有失败用例, 详见 pm_log.txt !!!");
            }
        }, "playmatrix").start();
    }

    // 横幅只留最近 6 行 (对齐 iOS avoxtest 的判定上屏)
    private void banner(String line) {
        runOnUiThread(() -> {
            mLines.append(line).append('\n');
            String[] arr = mLines.toString().split("\n");
            int from = Math.max(0, arr.length - 6);
            StringBuilder tail = new StringBuilder();
            for (int i = from; i < arr.length; ++i) {
                tail.append(arr[i]).append('\n');
            }
            mBanner.setText(tail.toString());
        });
    }
}
