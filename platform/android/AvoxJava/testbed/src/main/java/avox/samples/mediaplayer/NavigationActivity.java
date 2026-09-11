package avox.samples.mediaplayer;

import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.RadioGroup;

import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;


import avox.android.library.swig.IoPlan;
import avox.android.library.JNIHelper;

public class NavigationActivity extends AppCompatActivity {


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_navigation);

        JNIHelper.initJNI(null);

        // 初始化导航按钮
        findViewById(R.id.btnVK).setOnClickListener(v ->
                startActivity(new Intent(this, VkActivity.class)));
        findViewById(R.id.btnGL).setOnClickListener(v ->
                startActivity(new Intent(this, EglActivity.class)));
        findViewById(R.id.btnSource).setOnClickListener(v ->
                startActivity(new Intent(this, AVSource.class)));
        findViewById(R.id.btnRTC).setOnClickListener(v ->
                startActivity(new Intent(this, RtcActivity.class)));
        findViewById(R.id.btnMatrix).setOnClickListener(v ->
                startActivity(new Intent(this, PlayMatrixActivity.class)));
        // 硬解码开关
        SwitchCompat switchHardwareDecode = findViewById(R.id.switch_hardware_decode);
        switchHardwareDecode.setOnCheckedChangeListener((buttonView, isChecked) -> {
            SettingsManager.saveHardwareDecode(this, isChecked);
        });
        // IO解析器单选组
        RadioGroup radioGroup = findViewById(R.id.radio_io_type);
        radioGroup.setOnCheckedChangeListener((group, checkedId) -> {
            IoPlan ioPlan = checkedId == R.id.radio_ffmpeg ? IoPlan.ffmpeg : IoPlan.zlmediakit; // 根据checkedId映射类型
            SettingsManager.saveIoParserType(this, ioPlan.swigValue());
        });
    }
}