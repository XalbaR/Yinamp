#pragma once
#include "media/playback/PlaybackTypes.h"
#include "media/playback/AudioSpectrum.h"
#include <Windows.h>
#include <evr.h>
#include <memory>
#include <wrl/client.h>
namespace yinamp::media::playback {
class PlaybackSession final : public Session {
  public:
    explicit PlaybackSession(HWND videoWindow, bool autoStart = true, bool enableSpectrum = true);
    ~PlaybackSession() override;
    void Open(const std::filesystem::path&, std::uint64_t) override;
    void OpenSource(const PlaybackSource&, std::uint64_t) override;
    void Close() override;
    // Join/release native media before destroying its HWND; platform shutdown remains at owner destruction.
    void Shutdown();
    void Play() override;
    void Pause() override;
    void Stop() override;
    void Seek(double) override;
    void Audio(int, int) override;
    void Equalizer(bool enabled, const std::array<int, 11>& values);
    Snapshot Read() override;
    SpectrumSnapshot Spectrum();
    Microsoft::WRL::ComPtr<IMFVideoDisplayControl> Display();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
