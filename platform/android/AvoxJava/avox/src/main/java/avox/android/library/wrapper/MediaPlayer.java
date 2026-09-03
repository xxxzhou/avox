package avox.android.library.wrapper;

import avox.android.library.swig.AvoxWrapper;
import avox.android.library.swig.IMediaPlayer;
import avox.android.library.swig.IMediaPlayerOb;
import avox.android.library.swig.ISourceInfo;

public class MediaPlayer {

    private MediaPlayerOb observer = null;
    public IMediaPlayer Player = null;

    public static class MediaPlayerOb extends IMediaPlayerOb {
        private MediaPlayer parent = null;

        public MediaPlayerOb(MediaPlayer parent_) {
            parent = parent_;
        }

        @Override
        public void onReady() {
            ISourceInfo sourceInfo = parent.Player.getSourceInfo();
        }

        @Override
        public void onClose(){       
        }
    }

    public MediaPlayer(){
        observer = new MediaPlayerOb(this);
        Player = AvoxWrapper.createMediaPlayer();
        AvoxWrapper.addMediaPlayerOb(Player,observer);
    }

    public void Open(String Uri){
        AvoxWrapper.addMediaPlayerOb(Player,observer);
        Player.open(Uri);
    }

    public void Close(){
        Player.close();
        AvoxWrapper.removeMediaPlayerOb(Player,observer);
    }
}
