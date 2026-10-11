#include "media/playback/AudioSpectrum.h"
#include "media/playback/EqualizerDsp.h"
#include "ui/EqualizerInteraction.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <thread>
#include <vector>
using namespace yinamp::media::playback;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Process(EqualizerDsp& dsp, std::vector<float>& samples, const EqualizerSettings& settings) {
    dsp.Process({reinterpret_cast<std::uint8_t*>(samples.data()), samples.size() * sizeof(float)}, settings);
}
std::vector<float> Tone(double frequency, unsigned rate = 48000, unsigned channels = 1) {
    std::vector<float> samples(rate * channels);
    for (unsigned frame = 0; frame < rate; ++frame)
        for (unsigned channel = 0; channel < channels; ++channel)
            samples[frame * channels + channel] =
                static_cast<float>(0.02 * std::sin(2 * std::numbers::pi * frequency * frame / rate));
    return samples;
}
double Energy(const std::vector<float>& samples) {
    double sum{};
    for (std::size_t i = samples.size() / 2; i < samples.size(); ++i) {
        Check(std::isfinite(samples[i]), "finite output");
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / (samples.size() - samples.size() / 2));
}
double Response(double frequency, std::size_t band, int value, unsigned rate = 48000) {
    auto samples = Tone(frequency, rate);
    const auto input = Energy(samples);
    EqualizerDsp dsp;
    Check(dsp.Configure(rate, 1), "configure");
    EqualizerSettings settings{true};
    settings.values[band] = value;
    Process(dsp, samples, settings);
    return Energy(samples) / input;
}
}
int main() {
    try {
        for (unsigned rate : {8000u, 11025u, 16000u, 22050u, 32000u, 44100u, 48000u, 96000u, 192000u}) {
            for (std::size_t band = 0; band < 10; ++band) {
                for (int gain : {-120, 0, 120})
                    Check(EqualizerDsp::Coefficients(band, gain, rate).Stable(), "stable finite coefficients/poles");
                if (EqualizerCenters[band] < 0.45 * rate) {
                    const auto boost = Response(EqualizerCenters[band], band + 1, 120, rate);
                    const auto cut = Response(EqualizerCenters[band], band + 1, -120, rate);
                    Check(std::abs(boost - std::pow(10.0, 12.0 / 20)) < 0.015, "matching center +12 dB");
                    Check(std::abs(cut - std::pow(10.0, -12.0 / 20)) < 0.015, "matching center -12 dB");
                } else {
                    Check(std::abs(Response(375, band + 1, 120, rate) - 1) < 1e-6, "unsafe Nyquist center neutral");
                }
            }
        }
        for (std::size_t band = 0; band < 10; ++band) {
            const auto distant = band < 5 ? 16000.0 : 60.0;
            Check(std::abs(Response(distant, band + 1, 120) - 1) < 0.15, "distant frequency less influenced");
        }
        for (int value : {-60, 0, 60})
            Check(std::abs(Response(1000, 0, value) - std::pow(10.0, value / 200.0)) < 1e-5, "preamp dB gain");
        for (unsigned channels = 1; channels <= 8; ++channels) {
            EqualizerDsp dsp;
            Check(dsp.Configure(48000, channels), "1-8 channels");
            auto flat = Tone(1000, 48000, channels);
            const auto original = flat;
            EqualizerSettings settings{true};
            Process(dsp, flat, settings);
            Check(flat == original, "flat exact unity");
            dsp.Reset();
            settings.enabled = false;
            settings.values.fill(120);
            Process(dsp, flat, settings);
            Check(flat == original, "disabled exact unity ignores gains");
            dsp.Reset();
            settings.enabled = true;
            std::vector<float> silence(4096 * channels);
            Process(dsp, silence, settings);
            Check(Energy(silence) == 0, "silence remains zero under all boosts");
            dsp.Reset();
            auto signal = original;
            for (std::size_t i = 0; i < signal.size(); ++i)
                if (i % channels)
                    signal[i] = 0;
            Process(dsp, signal, settings);
            for (std::size_t i = 0; i < signal.size(); ++i) {
                Check(std::isfinite(signal[i]), "all boosts finite");
                if (i % channels)
                    Check(signal[i] == 0, "independent channel states");
            }
            dsp.Reset();
            settings.values.fill(-120);
            Process(dsp, signal, settings);
            for (float sample : signal)
                Check(std::isfinite(sample), "all cuts finite");
        }
        EqualizerDsp dsp;
        Check(!dsp.Configure(4000, 1) && !dsp.Configure(48000, 9), "unsupported format");
        Check(dsp.Configure(48000, 1), "valid format");
        EqualizerSettings settings{true};
        settings.values[5] = 120;
        {
            EqualizerDsp headroom;
            headroom.Configure(48000, 1);
            EqualizerSettings gain{true};
            gain.values[0] = 120;
            std::vector<float> sample{0.75f};
            Process(headroom, sample, gain);
            Check(sample[0] > 2.9f, "finite headroom above full scale is preserved without limiter");
        }
        dsp.Process({}, settings);
        std::vector<float> bad{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), 0};
        Process(dsp, bad, settings);
        for (float sample : bad)
            Check(std::isfinite(sample), "nonfinite input and overflow safety");
        dsp.Reset();
        settings = {true};
        auto stream = Tone(1000);
        Process(dsp, stream, settings);
        for (int value : {120, -120, 0}) {
            settings.values[5] = value;
            ++settings.revision;
            for (int block = 0; block < 8; ++block) {
                stream = Tone(1000);
                stream.resize(257);
                Process(dsp, stream, settings);
            }
            Check(dsp.Revision() == settings.revision, "live revision applied");
            stream = Tone(1000);
            Process(dsp, stream, settings);
            Check(std::abs(Energy(stream) / 0.0141421356 - std::pow(10.0, value / 200.0)) < 0.02,
                  "live settled response");
        }
        settings.values[0] = 120;
        ++settings.revision;
        stream = Tone(1000);
        Process(dsp, stream, settings);
        Check(Energy(stream) > 0.05, "enabled gain");
        settings.enabled = false;
        ++settings.revision;
        stream = Tone(1000);
        Process(dsp, stream, settings);
        Check(std::abs(Energy(stream) - 0.0141421356) < 1e-7, "live OFF unity");
        settings.enabled = true;
        ++settings.revision;
        stream = Tone(1000);
        Process(dsp, stream, settings);
        Check(Energy(stream) > 0.05, "live ON restores curve");
        {
            EqualizerDsp live;
            live.Configure(48000, 1);
            EqualizerSettings curve{true};
            float previous{};
            unsigned phase{};
            for (unsigned block = 0; block < 240; ++block) {
                if (block % 30 == 0) {
                    curve.values[5] = block % 60 ? -120 : 120;
                    curve.enabled = block % 90 != 0;
                    ++curve.revision;
                }
                std::vector<float> packet(64);
                for (float& sample : packet)
                    sample = static_cast<float>(0.02 * std::sin(2 * std::numbers::pi * 1000 * phase++ / 48000));
                Process(live, packet, curve);
                for (float sample : packet) {
                    Check(std::isfinite(sample) && std::abs(sample - previous) < 0.03f,
                          "continuous live sine updates avoid abrupt sample jumps");
                    previous = sample;
                }
            }
        }
        for (int reset = 0; reset < 6; ++reset) {
            dsp.Reset();
            std::vector<float> silence(2048);
            Process(dsp, silence, settings);
            Check(Energy(silence) == 0, "reset clears tails retaining settings");
            stream = Tone(60);
            Process(dsp, stream, settings);
        }
        Check(dsp.Configure(8000, 2), "format change reset");
        std::vector<float> silence(8192);
        Process(dsp, silence, settings);
        Check(Energy(silence) == 0, "format reset clears history");
        EqualizerParameters parameters;
        yinamp::ui::EqualizerInteraction ui;
        Check(ui.Down({15, 19}), "ON click down");
        (void)ui.Up({15, 19});
        for (std::size_t i = 0; i < 11; ++i) {
            const auto track = yinamp::skin::EqualizerSliders[i];
            Check(ui.Down({track.x + 1, track.y + 5}), "slider down");
            (void)ui.Up({track.x + 1, track.y + 5});
            parameters.Publish(ui.state.on, ui.state.values);
            EqualizerSettings read;
            Check(parameters.Read(read) && read.enabled && read.values[i] == 120, "UI +120 index mapping");
        }
        ui.SavePreset();
        ui.FlatPreset();
        parameters.Publish(ui.state.on, ui.state.values);
        Check(parameters.Read(settings) && settings.enabled && settings.values == std::array<int, 11>{},
              "Flat keeps ON");
        ui.LoadPreset();
        parameters.Publish(ui.state.on, ui.state.values);
        Check(parameters.Read(settings) && settings.values[10] == 120, "session preset restores DSP snapshot");
        ui.state.values.fill(-120);
        parameters.Publish(false, ui.state.values);
        Check(parameters.Read(settings) && !settings.enabled && settings.values[0] == -120,
              "OFF retains stored curve and negative gain");
        std::atomic<bool> finished{};
        std::thread writer([&] {
            for (int i = 0; i < 10000; ++i) {
                std::array<int, 11> values;
                values.fill(i % 2 ? -120 : 120);
                parameters.Publish(true, values);
            }
            finished = true;
        });
        while (!finished)
            if (parameters.Read(settings))
                for (int value : settings.values)
                    Check(value == settings.values[0], "coherent publication under contention");
        writer.join();
        // Actual processed PCM -> existing spectrum analyzer, no snapshot manipulation.
        const auto analyzed = [](int gain) {
            EqualizerDsp eq;
            eq.Configure(48000, 1);
            EqualizerSettings s{true};
            s.values[6] = gain;
            auto tone = Tone(3000);
            Process(eq, tone, s);
            AudioSpectrum spectrum(1);
            spectrum.Configure({48000, 1});
            spectrum.Permit(true);
            spectrum.ClockStart(0);
            spectrum.Submit({reinterpret_cast<const std::uint8_t*>(tone.data()), tone.size() * sizeof(float)}, 0);
            return spectrum.Read(0.1).bands[AudioSpectrum::BandFor(3000, 48000)];
        };
        Check(analyzed(120) > analyzed(0) && analyzed(0) > analyzed(-120), "post-EQ real PCM spectrum follows gain");
        std::cout << "Equalizer DSP, parameters, UI mapping and post-EQ spectrum passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
