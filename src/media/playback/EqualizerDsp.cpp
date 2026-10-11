#include "media/playback/EqualizerDsp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
namespace yinamp::media::playback {
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
void EqualizerParameters::Publish(bool enabled, const std::array<int, 11>& values) {
    std::lock_guard lock(writers_);
    bool changed = enabled_.load() != enabled;
    for (std::size_t i = 0; i < values.size(); ++i)
        changed = changed || values_[i].load() != std::clamp(values[i], -120, 120);
    if (!changed)
        return;
    sequence_.fetch_add(1); // Sequential consistency also orders payload against both sequence reads.
    enabled_.store(enabled);
    for (std::size_t i = 0; i < values.size(); ++i)
        values_[i].store(std::clamp(values[i], -120, 120));
    sequence_.fetch_add(1);
}
bool EqualizerParameters::Read(EqualizerSettings& settings) const noexcept {
    const auto first = sequence_.load();
    if (first & 1)
        return false;
    EqualizerSettings snapshot;
    snapshot.enabled = enabled_.load();
    for (std::size_t i = 0; i < values_.size(); ++i)
        snapshot.values[i] = values_[i].load();
    snapshot.revision = first;
    if (sequence_.load() != first)
        return false;
    settings = snapshot;
    return true;
}
bool EqualizerCoefficients::Stable() const noexcept {
    return std::isfinite(b0) && std::isfinite(b1) && std::isfinite(b2) && std::isfinite(a1) && std::isfinite(a2) &&
           std::abs(a2) < 1 && 1 + a1 + a2 > 0 && 1 - a1 + a2 > 0;
}
EqualizerCoefficients EqualizerDsp::Coefficients(std::size_t band, int tenthsDb, unsigned rate) noexcept {
    if (band >= EqualizerCenters.size() || rate < 8000 || rate > 192000 || !tenthsDb)
        return {};
    const double center = EqualizerCenters[band];
    if (center >= 0.45 * rate)
        return {}; // Keep a 10% Nyquist margin; never move centers.
    const double lower =
        band ? std::sqrt(EqualizerCenters[band - 1] * center) : center / std::sqrt(EqualizerCenters[1] / center);
    const double upper = band + 1 < EqualizerCenters.size() ? std::sqrt(center * EqualizerCenters[band + 1])
                                                            : center * std::sqrt(center / EqualizerCenters[band - 1]);
    const double q = center / (upper - lower);
    const double w = 2 * std::numbers::pi * center / rate;
    const double a = std::pow(10.0, std::clamp(tenthsDb, -120, 120) / 400.0);
    const double alpha = std::sin(w) / (2 * q);
    const double denominator = 1 + alpha / a;
    EqualizerCoefficients c{(1 + alpha * a) / denominator, -2 * std::cos(w) / denominator,
                            (1 - alpha * a) / denominator, -2 * std::cos(w) / denominator,
                            (1 - alpha / a) / denominator};
    return c.Stable() ? c : EqualizerCoefficients{};
}
bool EqualizerDsp::Configure(unsigned rate, unsigned channels) noexcept {
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 8)
        return false;
    rate_ = rate;
    channels_ = channels;
    transitionFrames_ = std::max(1u, rate / 50);
    Reset();
    return true;
}
void EqualizerDsp::Reset() noexcept {
    current_ = {};
    next_ = {};
    transition_ = 0;
    initialized_ = false;
    revision_ = 0;
}
EqualizerDsp::Bank EqualizerDsp::MakeBank(const EqualizerSettings& settings) const noexcept {
    Bank bank;
    bank.enabled = settings.enabled;
    if (settings.enabled) {
        bank.preamp = std::pow(10.0, std::clamp(settings.values[0], -120, 120) / 200.0);
        for (std::size_t i = 0; i < bank.filters.size(); ++i)
            bank.filters[i] = Coefficients(i, settings.values[i + 1], rate_);
    }
    return bank;
}
double EqualizerDsp::Bank::Sample(double input, unsigned channel) noexcept {
    if (!enabled)
        return input;
    double value = input * preamp;
    for (std::size_t i = 0; i < filters.size(); ++i) {
        const auto& c = filters[i];
        auto& h = history[channel][i];
        const double output = c.b0 * value + h.z1;
        h.z1 = c.b1 * value - c.a1 * output + h.z2;
        h.z2 = c.b2 * value - c.a2 * output;
        if (!std::isfinite(output) || !std::isfinite(h.z1) || !std::isfinite(h.z2)) {
            h = {};
            value = 0;
        } else {
            if (std::abs(h.z1) < 1e-30)
                h.z1 = 0;
            if (std::abs(h.z2) < 1e-30)
                h.z2 = 0;
            value = output;
        }
    }
    return value;
}
void EqualizerDsp::Process(std::span<std::uint8_t> bytes, const EqualizerSettings& settings) noexcept {
    if (!channels_ || bytes.empty())
        return;
    if (!initialized_) {
        current_ = MakeBank(settings);
        revision_ = settings.revision;
        initialized_ = true;
    } else if (!transition_ && revision_ != settings.revision) {
        next_ = MakeBank(settings); // Two stable fixed banks; no interpolation of feedback coefficients.
        // Cold start the new bank: prior direct-form states belong to the old coefficients.
        transition_ = transitionFrames_;
        revision_ = settings.revision;
    }
    const auto frames = bytes.size() / (channels_ * sizeof(float));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double blend = transition_ ? 1.0 - static_cast<double>(transition_ - 1) / transitionFrames_ : 0;
        for (unsigned channel = 0; channel < channels_; ++channel) {
            auto* address = bytes.data() + (frame * channels_ + channel) * sizeof(float);
            float input;
            std::memcpy(&input, address, sizeof(input));
            const double clean = std::isfinite(input) ? input : 0.0;
            double output = current_.Sample(clean, channel);
            if (transition_)
                output = output * (1 - blend) + next_.Sample(clean, channel) * blend;
            // Preserve normal over-full-scale float headroom. Only numeric overflow is neutralized.
            const float sample = std::isfinite(output) && std::abs(output) <= std::numeric_limits<float>::max()
                                     ? static_cast<float>(output)
                                     : 0.0f;
            std::memcpy(address, &sample, sizeof(sample));
        }
        if (transition_ && !--transition_)
            current_ = next_;
    }
}
}
