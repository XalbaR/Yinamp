#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
namespace yinamp::media::playback {
inline constexpr std::size_t SpectrumBands = 19;
struct SpectrumSnapshot {
    std::array<float, SpectrumBands> bands{}, peaks{};
    std::uint64_t generation{}, samples{}, dropped{};
    bool available{};
};
enum class PcmRepresentation { Signed16, Float32 };
struct PcmFormat {
    unsigned rate{}, channels{};
    PcmRepresentation representation{PcmRepresentation::Float32};
    bool Valid() const { return rate >= 8000 && rate <= 192000 && channels > 0 && channels <= 8 &&
        (representation == PcmRepresentation::Float32 || representation == PcmRepresentation::Signed16); }
};
// Fixed latest-window storage. Callback never waits; FFT runs only when UI requests a snapshot.
class AudioSpectrum final {
  public:
    static constexpr std::size_t Window = 2048;
    explicit AudioSpectrum(std::uint64_t generation) : generation_(generation) {}
    bool Configure(PcmFormat format);
    void Permit(bool value);
    void ClockStart(std::int64_t position);
    void ClockHalt();
    void Clear();
    void Shutdown();
    void Submit(std::span<const std::uint8_t> bytes, std::int64_t time);
    SpectrumSnapshot Read(double seconds = 1.0 / 30.0);
    static std::size_t BandFor(double frequency, unsigned rate);
  private:
    std::mutex mutex_;
    PcmFormat format_;
    const std::uint64_t generation_;
    std::array<float, Window> ring_{};
    std::size_t cursor_{}, count_{};
    std::int64_t minimumTime_{};
    std::uint64_t samples_{};
    std::uint64_t revision_{};
    double idle_{};
    std::atomic<std::uint64_t> dropped_{};
    std::atomic<bool> permitted_{}, running_{}, shutdown_{};
    std::atomic<bool> pendingStart_{};
    std::atomic<std::int64_t> pendingPosition_{};
    SpectrumSnapshot view_;
    void ResetLocked();
    void ApplyStartLocked();
};
}
