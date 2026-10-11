#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
namespace yinamp::media::playback {
inline constexpr std::array<double, 10> EqualizerCenters{60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
struct EqualizerSettings {
    bool enabled{};
    std::array<int, 11> values{}; // Existing UI units: tenths of a dB, preamp first.
    std::uint64_t revision{};
};
// Writers serialize here; the audio reader makes one bounded, lock-free attempt.
// All payload fields are atomic, so an interrupted publication has no data race.
class EqualizerParameters final {
  public:
    void Publish(bool enabled, const std::array<int, 11>& values);
    bool Read(EqualizerSettings& settings) const noexcept;

  private:
    std::mutex writers_;
    std::atomic<std::uint64_t> sequence_{};
    std::atomic<bool> enabled_{};
    std::array<std::atomic<int>, 11> values_{};
};
struct EqualizerCoefficients {
    double b0{1}, b1{}, b2{}, a1{}, a2{};
    bool Stable() const noexcept;
};
class EqualizerDsp final {
  public:
    bool Configure(unsigned rate, unsigned channels) noexcept;
    void Reset() noexcept;
    void Process(std::span<std::uint8_t> bytes, const EqualizerSettings& settings) noexcept;
    std::uint64_t Revision() const noexcept {
        return revision_;
    }
    static EqualizerCoefficients Coefficients(std::size_t band, int tenthsDb, unsigned rate) noexcept;

  private:
    struct History {
        double z1{}, z2{};
    };
    struct Bank {
        std::array<EqualizerCoefficients, 10> filters{};
        std::array<std::array<History, 10>, 8> history{};
        double preamp{1};
        bool enabled{};
        double Sample(double input, unsigned channel) noexcept;
    };
    Bank MakeBank(const EqualizerSettings& settings) const noexcept;
    Bank current_, next_;
    unsigned rate_{}, channels_{}, transition_{}, transitionFrames_{};
    bool initialized_{};
    std::uint64_t revision_{};
};
}
