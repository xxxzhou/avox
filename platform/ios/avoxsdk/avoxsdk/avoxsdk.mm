#include "avoxsdk.h"
#include "avox/AvoxPlayer.h"

using namespace avox;

@interface AVOXSDKPlayer () {
    IMediaPlayer* _mediaPlayer;
    class MediaPlayerOb : public IMediaPlayerOb {
        AVOXSDKPlayer* _sdkPlayer;
    public:
        MediaPlayerOb(AVOXSDKPlayer* sdkPlayer) : _sdkPlayer(sdkPlayer) {}
        void onStateChange(PlayerState preState, PlayerState state) override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onStateChangeFrom:to:)]) {
                [[_sdkPlayer getDelegate] onStateChangeFrom:(NSInteger)preState to:(NSInteger)state];
            }
        }
        void onIoError(IoError error) override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onIoError:)]) {
                [[_sdkPlayer getDelegate] onIoError:(NSInteger)error];
            }
        }
        void onDecodeError(TrackType trackType, DecodeError error) override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onDecodeErrorForTrack:withError:)]) {
                [[_sdkPlayer getDelegate] onDecodeErrorForTrack:(NSInteger)trackType withError:(NSInteger)error];
            }
        }
        void onReady() override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onReady)]) {
                [[_sdkPlayer getDelegate] onReady];
            }
        }
        void onComplete() override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onComplete)]) {
                [[_sdkPlayer getDelegate] onComplete];
            }
        }
        void onSeek() override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onSeek)]) {
                [[_sdkPlayer getDelegate] onSeek];
            }
        }
        void onPause() override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onPause)]) {
                [[_sdkPlayer getDelegate] onPause];
            }
        }
        void onResume() override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onResume)]) {
                [[_sdkPlayer getDelegate] onResume];
            }
        }
        void onClose() override {
            if ([_sdkPlayer getDelegate] && [[_sdkPlayer getDelegate] respondsToSelector:@selector(onClose)]) {
                [[_sdkPlayer getDelegate] onClose];
            }
        }
    };
    MediaPlayerOb* _mediaPlayerOb;
}
@end

@implementation AVOXSDKPlayer

- (instancetype)init {
    self = [super init];
    if (self) {
        _mediaPlayer = createMediaPlayer();
        _mediaPlayerOb = new MediaPlayerOb(self);
        addMediaPlayerOb(_mediaPlayer, _mediaPlayerOb);
    }
    return self;
}

- (void)dealloc {
    removeMediaPlayerOb(_mediaPlayer, _mediaPlayerOb);
    delete _mediaPlayerOb;
    delete _mediaPlayer;
}

- (void)setDelegate:(id<AVOXSDKPlayerDelegate>)delegate {
    _delegate = delegate;
}

- (void)setIoPlan:(NSInteger)plan {
    _mediaPlayer->setIoPlan((IoPlan)plan);
}

- (void)setHardDecode:(BOOL)hard {
    _mediaPlayer->setHardDecode(hard);
}

- (void)setVRenderType:(NSInteger)type {
    _mediaPlayer->setVRenderType((RenderType)type);
}

- (void)setARenderType:(NSInteger)type {
    _mediaPlayer->setARenderType((ARenderType)type);
}

- (void)setWindow:(void *)window vtrack:(NSInteger)vtrack {
    _mediaPlayer->setWindow((IWindow*)window, (int32_t)vtrack);
}

- (void)openWithURL:(NSString *)url {
    const char* cUrl = [url UTF8String];
    _mediaPlayer->open(cUrl);
}

- (void)stop {
    _mediaPlayer->stop();
}

- (void)seekToPosition:(int64_t)pos {
    _mediaPlayer->seek(pos);
}

- (void)pause {
    _mediaPlayer->pause();
}

- (void)resume {
    _mediaPlayer->resume();
}

- (void)setSpeed:(double)speed {
    _mediaPlayer->speed(speed);
}

- (NSInteger)getState {
    return (NSInteger)_mediaPlayer->getState();
}

- (double)getProcess {
    return _mediaPlayer->getProcess();
}

- (int64_t)getDuration {
    return _mediaPlayer->getDuration();
}

- (int64_t)getPosition {
    return _mediaPlayer->getPosition();
}

- (NSInteger)videoTrackSize {
    return (NSInteger)_mediaPlayer->videoTrackSize();
}

- (void *)getVideoTrackAtIndex:(NSInteger)index {
    return _mediaPlayer->getVideoTrack((int32_t)index);
}

- (NSInteger)audioTrackSize {
    return (NSInteger)_mediaPlayer->audioTrackSize();
}

- (void *)getAudioTrackAtIndex:(NSInteger)index {
    return _mediaPlayer->getAudioTrack((int32_t)index);
}

@end
