#include "media/playback/AudioSpectrum.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <numbers>
namespace yinamp::media::playback {
bool AudioSpectrum::Configure(PcmFormat format) {
    std::lock_guard lock(mutex_);
    if (!format.Valid() || shutdown_) return false;
    format_ = format;
    ResetLocked();
    return true;
}
void AudioSpectrum::ResetLocked() {
    ++revision_; idle_ = 0;
    ring_.fill(0); cursor_ = count_ = 0; samples_ = 0;
    view_ = {}; view_.generation = generation_; view_.available = format_.Valid() && !shutdown_;
}
void AudioSpectrum::ClockHalt() {
    running_ = false;
    pendingStart_ = false;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock) ResetLocked();
}
void AudioSpectrum::Permit(bool value) {
    permitted_ = value;
    if (!value) { running_ = false; Clear(); }
}
void AudioSpectrum::Clear() {
    running_ = false;
    pendingStart_ = false;
    std::lock_guard lock(mutex_);
    ResetLocked();
}
void AudioSpectrum::ClockStart(std::int64_t position) {
    pendingPosition_ = position; pendingStart_ = true;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock || shutdown_) return;
    ApplyStartLocked();
}
void AudioSpectrum::ApplyStartLocked() {
    if (!pendingStart_.exchange(false) || shutdown_) return;
    ResetLocked(); minimumTime_ = pendingPosition_.load();
    running_ = permitted_.load();
}
void AudioSpectrum::Shutdown() { shutdown_ = true; Permit(false); }
void AudioSpectrum::Submit(std::span<const std::uint8_t> bytes, std::int64_t time) {
    if ((!running_ && !pendingStart_) || shutdown_) return;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock) { ++dropped_; return; }
    ApplyStartLocked();
    if (!running_ || !format_.Valid() || time < minimumTime_) return;
    const std::size_t size = format_.representation == PcmRepresentation::Float32 ? 4 : 2;
    const auto stride = size * format_.channels;
    if (bytes.size() % stride) { ++dropped_; return; }
    // A large packet cannot monopolize the MF callback: keep at most the latest window.
    const auto frames = bytes.size() / stride;
    const auto first = frames > Window ? frames - Window : 0;
    if (first) ++dropped_;
    for (std::size_t frame = first; frame < frames; ++frame) {
        float mono = 0;
        for (unsigned channel = 0; channel < format_.channels; ++channel) {
            const auto* data = bytes.data() + frame * stride + channel * size;
            float value{};
            if (size == 4) std::memcpy(&value, data, 4);
            else { std::int16_t integer{}; std::memcpy(&integer, data, 2); value = integer / 32768.0f; }
            if (!std::isfinite(value)) value = 0;
            mono += std::clamp(value, -1.0f, 1.0f) / static_cast<float>(format_.channels);
        }
        ring_[cursor_] = mono; cursor_ = (cursor_ + 1) % Window;
        count_ = std::min(count_ + 1, Window); ++samples_;
    }
}
std::size_t AudioSpectrum::BandFor(double frequency, unsigned rate) {
    const double low = static_cast<double>(rate) / Window;
    const double high = std::min(16000.0, rate / 2.0);
    const double mapped = std::log(std::max(low, frequency) / low) / std::log(high / low);
    return static_cast<std::size_t>(std::clamp(mapped * SpectrumBands, 0.0, static_cast<double>(SpectrumBands - 1)));
}
SpectrumSnapshot AudioSpectrum::Read(double seconds) {
    std::array<float, Window> window{};
    unsigned rate{};
    std::uint64_t samples{}, revision{};
    {
        std::lock_guard lock(mutex_);
        ApplyStartLocked();
        if (!running_ || shutdown_ || !format_.Valid()) { ResetLocked(); return view_; }
        rate = format_.rate; samples = samples_; revision = revision_;
        idle_ = samples == view_.samples ? idle_ + seconds : 0;
        for (std::size_t i = 0; i < count_; ++i)
            if (idle_ < 0.15) window[Window - count_ + i] = ring_[(cursor_ + Window - count_ + i) % Window];
    }
    std::array<std::complex<float>, Window> fft{};
    for (std::size_t i = 0; i < Window; ++i) {
        std::size_t reversed = 0, index = i;
        for (std::size_t n = Window; n > 1; n >>= 1) { reversed = (reversed << 1) | (index & 1); index >>= 1; }
        const float hann = 0.5f - 0.5f * std::cos(2 * std::numbers::pi_v<float> * static_cast<float>(i) / (Window - 1));
        fft[reversed] = window[i] * hann;
    }
    for (std::size_t length = 2; length <= Window; length <<= 1) {
        const auto step = std::polar(1.0f, -2 * std::numbers::pi_v<float> / static_cast<float>(length));
        for (std::size_t base = 0; base < Window; base += length) {
            std::complex<float> twiddle{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto a = fft[base + j], b = twiddle * fft[base + j + length / 2];
                fft[base + j] = a + b; fft[base + j + length / 2] = a - b; twiddle *= step;
            }
        }
    }
    std::array<float, SpectrumBands> target{};
    for (std::size_t i = 1; i < Window / 2; ++i) {
        const auto band = BandFor(static_cast<double>(i) * rate / Window, rate);
        const auto magnitude = std::abs(fft[i]) * (4.0f / Window);
        target[band] = std::max(target[band], std::clamp((20 * std::log10(std::max(magnitude, 1e-8f)) + 60) / 60, 0.0f, 1.0f));
    }
    std::lock_guard lock(mutex_);
    if (!running_ || revision_ != revision || shutdown_) return view_;
    const float dt = static_cast<float>(std::clamp(seconds, 0.0, 0.1));
    for (std::size_t i = 0; i < SpectrumBands; ++i) {
        const float tau = target[i] > view_.bands[i] ? 0.035f : 0.18f;
        view_.bands[i] += (target[i] - view_.bands[i]) * (1 - std::exp(-dt / tau));
        view_.peaks[i] = std::max(view_.bands[i], view_.peaks[i] - dt * 0.55f);
    }
    view_.samples = samples; view_.dropped = dropped_.load();
    return view_;
}
}
