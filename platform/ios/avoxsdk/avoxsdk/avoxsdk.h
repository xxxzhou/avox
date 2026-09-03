#pragma once

#import <Foundation/Foundation.h>

// Forward declarations
@class AVOXSDKPlayer; 

// Protocol definition for callbacks
@protocol AVOXSDKPlayerDelegate <NSObject>
@optional
- (void)onStateChangeFrom:(NSInteger)preState to:(NSInteger)state;
- (void)onIoError:(NSInteger)error;
- (void)onDecodeErrorForTrack:(NSInteger)trackType withError:(NSInteger)error;
- (void)onReady;
- (void)onComplete;
- (void)onSeek;
- (void)onPause;
- (void)onResume;
- (void)onClose;
@end

@interface AVOXSDKPlayer : NSObject {
    id<AVOXSDKPlayerDelegate> _delegate;
}

// Get the delegate
- (id<AVOXSDKPlayerDelegate>)getDelegate;

// Set the delegate for callbacks
- (void)setDelegate:(id<AVOXSDKPlayerDelegate>)delegate;

// Set IO plan
- (void)setIoPlan:(NSInteger)plan;

// Set hardware decoding
- (void)setHardDecode:(BOOL)hard;

// Set video render type
- (void)setVRenderType:(NSInteger)type;

// Set audio render type
- (void)setARenderType:(NSInteger)type;

// Set window (assuming a placeholder for now)
- (void)setWindow:(void *)window vtrack:(NSInteger)vtrack;

// Open a media URL
- (void)openWithURL:(NSString *)url;

// Stop playback
- (void)stop;

// Seek to a position
- (void)seekToPosition:(int64_t)pos;

// Pause playback
- (void)pause;

// Resume playback
- (void)resume;

// Set playback speed
- (void)setSpeed:(double)speed;

// Get current player state
- (NSInteger)getState;

// Get playback progress
- (double)getProcess;

// Get media duration
- (int64_t)getDuration;

// Get current playback position
- (int64_t)getPosition;

// Get number of video tracks
- (NSInteger)videoTrackSize;

// Get video track at index
- (void *)getVideoTrackAtIndex:(NSInteger)index;

// Get number of audio tracks
- (NSInteger)audioTrackSize;

// Get audio track at index
- (void *)getAudioTrackAtIndex:(NSInteger)index;

@end