#pragma once
#include <mfidl.h>

namespace yinamp::media::playback {
// Optional output adapter for integration fixtures. An absent adapter retains
// the system audio renderer and its real per-stream volume service.
class AudioOutput {
  public:
    virtual ~AudioOutput() = default;
    virtual HRESULT CreateRenderer(IMFMediaType* sourceType, IMFActivate** renderer) = 0;
    virtual HRESULT VolumeService(IMFMediaSession* session, IMFAudioStreamVolume** volume) = 0;
};
}
