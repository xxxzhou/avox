package avox.android.library;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;

class AvoxAudioTrack {
    private AudioTrack mAudioTrack = null;
    private byte[] mAudioBuffer = null;
    private int m_nSampleRateInHz = 0;
    private int m_nChannelNumber = 0;
    private int m_nBitsPerSample = 0;
    private int bufferSize = 0;
    private boolean m_bStopped = true;

    private static final String TAG = "AvoxAudioTrack";

    private AvoxAudioTrack() {
    }

    private int initAudioBuffer() {
        if (null != mAudioBuffer && mAudioBuffer.length != bufferSize) {
            mAudioBuffer = null;
        }
        if (null == mAudioBuffer) {
            mAudioBuffer = new byte[bufferSize];
        }
        return 0;
    }

    public int Create(int nSampleRateInHz, int nChannelNumber,
                      int nBitsPerSample) {
        int status = 0;
        status = initAudioOutput(nSampleRateInHz, nChannelNumber,
                nBitsPerSample);
        if (0 == status) {
            status = initAudioBuffer();
        }
        if (0 != status) {
            Destroy();
        }
        return status;
    }

    public int initAudioOutput(int nSampleRateInHz, int nChannelNumber,
                               int nBitsPerSample) {
//		Log.i(TAG, "NKRAudioTrack, nSampleRateInHz = " + nSampleRateInHz
//				+ ", nChannelNumber =" + nChannelNumber + ", nBitsPerSample = "
//				+ nBitsPerSample);
        int iChannelConfig = AudioFormat.CHANNEL_OUT_DEFAULT;
        if (1 == nChannelNumber)
            iChannelConfig = AudioFormat.CHANNEL_OUT_MONO;
        else if (2 == nChannelNumber)
            iChannelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        else
            return -1;

        int iAudioFormat = AudioFormat.ENCODING_DEFAULT;
        if (8 == nBitsPerSample)
            iAudioFormat = AudioFormat.ENCODING_PCM_8BIT;
        else if (16 == nBitsPerSample)
            iAudioFormat = AudioFormat.ENCODING_PCM_16BIT;
        else if (32 == nBitsPerSample)
            iAudioFormat = AudioFormat.ENCODING_PCM_FLOAT;
        else
            return -1;

        if (null != mAudioTrack) {
            mAudioTrack.release();
            mAudioTrack = null;
        }
        try {
            int iMinBufSize = AudioTrack.getMinBufferSize(nSampleRateInHz, iChannelConfig, iAudioFormat);
            bufferSize = iMinBufSize * 2;
            Log.i(TAG, "initAudioOutput iMinBufSize: " + iMinBufSize + "; nBuffSize:" + bufferSize);
            mAudioTrack = new AudioTrack(AudioManager.STREAM_MUSIC,
                    nSampleRateInHz, iChannelConfig, iAudioFormat, bufferSize,
                    AudioTrack.MODE_STREAM);
        } catch (Exception e) {
            e.printStackTrace();
            Log.i(TAG, "initAudioOutput Exception: " + e.getMessage());
            mAudioTrack = null;
            return -1;
        }
        m_nSampleRateInHz = nSampleRateInHz;
        m_nChannelNumber = nChannelNumber;
        m_nBitsPerSample = nBitsPerSample;
        return 0;
    }

    public int GetFrameSize() {
        if(mAudioTrack == null){
            return 0;
        }
        return mAudioTrack.getBufferSizeInFrames();
    }

    public int SetVolume(float fLeft, float fRight) {
        if (null != mAudioTrack) {
            mAudioTrack.setStereoVolume(fLeft, fRight);
        }
        return 0;
    }

    public byte[] GetDataBuffer() {
        return mAudioBuffer;
    }

    public int Write(int nDataLen) {
        int pos = -1;
        if (null != mAudioTrack) {
            pos = mAudioTrack.write(mAudioBuffer, 0, nDataLen);
        }
        if (pos < 0) {
            Log.e(TAG, "some error happened in AudioTrack, error code = " + pos
                    + " restart AudioTrack ");
            ReStart();
            if (null != mAudioTrack) {
                pos = mAudioTrack.write(mAudioBuffer, 0, nDataLen);
            }
            Log.e(TAG, "After AudioTrack restart, pos = " + pos);
        }
        return pos;
    }

    public void Flush() {
        if (null != mAudioTrack) {
            mAudioTrack.flush();
        }
    }

    public void Start() {
        if (null != mAudioTrack && mAudioTrack.getState() == AudioTrack.STATE_INITIALIZED) {
            mAudioTrack.play();
        }
        m_bStopped = false;
    }

    public void Pause() {
        if (null != mAudioTrack && mAudioTrack.getState() == AudioTrack.STATE_INITIALIZED) {
            mAudioTrack.pause();
        }
    }

    public void Stop() {
        if (null != mAudioTrack && mAudioTrack.getState() == AudioTrack.STATE_INITIALIZED) {
            mAudioTrack.stop();
        }
        Flush();
        m_bStopped = true;
    }

    public void Destroy() {
        if (null != mAudioTrack) {
            mAudioTrack.release();
            mAudioTrack = null;
        }
        mAudioBuffer = null;
    }

    public int GetPosition() {
        int Pos = 0;
        if (m_bStopped)
            return Pos;
        if (mAudioTrack != null && mAudioTrack.getState() == AudioTrack.STATE_INITIALIZED)
            Pos = mAudioTrack.getPlaybackHeadPosition();
        return Pos;
    }

    private int ReStart() {
        Stop();
        if (null != mAudioTrack) {
            mAudioTrack.release();
            mAudioTrack = null;
        }
        if (0 == initAudioOutput(m_nSampleRateInHz, m_nChannelNumber,
                m_nBitsPerSample)) {
            Start();
            return 0;
        }
        return -1;
    }
}
