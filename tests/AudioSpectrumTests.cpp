#include "media/playback/AudioSpectrum.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <thread>
using namespace yinamp::media::playback;
namespace {
void Check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
template<class T> void Submit(AudioSpectrum& analyzer, const std::vector<T>& data, std::int64_t time = 0) {
    analyzer.Submit({reinterpret_cast<const std::uint8_t*>(data.data()), data.size() * sizeof(T)}, time);
}
std::vector<float> Tone(double frequency, double second = 0, unsigned channels = 1) {
    std::vector<float> result(AudioSpectrum::Window * channels);
    for (std::size_t i = 0; i < AudioSpectrum::Window; ++i) {
        const float value = static_cast<float>(0.4 * std::sin(2 * std::numbers::pi * frequency * i / 48000) +
            (second ? 0.4 * std::sin(2 * std::numbers::pi * second * i / 48000) : 0));
        for (unsigned c = 0; c < channels; ++c) result[i * channels + c] = value;
    }
    return result;
}
float Maximum(const SpectrumSnapshot& value) { return *std::max_element(value.bands.begin(), value.bands.end()); }
}
int main() {
    try {
        AudioSpectrum analyzer(7);
        Check(!analyzer.Configure({0, 1}), "reject invalid rate");
        Check(!analyzer.Configure({48000, 9}), "reject unsupported channel count");
        Check(!analyzer.Configure({48000, 1, static_cast<PcmRepresentation>(99)}), "reject unsupported representation");
        Check(analyzer.Configure({48000, 1}), "float format");
        analyzer.Permit(true); analyzer.ClockStart(0);
        Submit(analyzer, std::vector<float>(AudioSpectrum::Window));
        Check(Maximum(analyzer.Read()) == 0, "silence");
        const double low = 375, high = 6000;
        const auto lowBand = AudioSpectrum::BandFor(low, 48000), highBand = AudioSpectrum::BandFor(high, 48000);
        Check(lowBand != highBand, "different frequency bands");
        Submit(analyzer, Tone(low));
        const auto first = analyzer.Read();
        Check(first.bands[lowBand] == Maximum(first) && Maximum(first) > 0.4f, "sine expected band");
        analyzer.Clear(); analyzer.ClockStart(0);
        Submit(analyzer, Tone(high));
        const auto upper = analyzer.Read();
        Check(upper.bands[highBand] == Maximum(upper), "high sine expected band");
        analyzer.Clear(); analyzer.ClockStart(0); Submit(analyzer, Tone(low, high));
        const auto multi = analyzer.Read();
        Check(multi.bands[lowBand] > 0.4f && multi.bands[highBand] > 0.4f, "multi tone");
        Submit(analyzer, std::vector<float>(AudioSpectrum::Window));
        const auto decay = analyzer.Read();
        Check(decay.bands[lowBand] > 0 && decay.bands[lowBand] < multi.bands[lowBand], "release smoothing");
        Check(decay.peaks[lowBand] > decay.bands[lowBand], "peak decay");
        for (int i = 0; i < 100; ++i) analyzer.Read();
        Check(Maximum(analyzer.Read()) < 0.001f, "silence settles");
        analyzer.Configure({48000, 2}); analyzer.ClockStart(0);
        Submit(analyzer, Tone(low, 0, 2));
        Check(std::abs(Maximum(analyzer.Read()) - Maximum(first)) < 0.001f, "stereo identical channels preserve level");
        auto opposite = Tone(low, 0, 2);
        for (std::size_t i = 1; i < opposite.size(); i += 2) opposite[i] = -opposite[i];
        analyzer.Clear(); analyzer.ClockStart(0); Submit(analyzer, opposite);
        Check(Maximum(analyzer.Read()) == 0, "stereo arithmetic downmix");
        analyzer.Configure({48000, 1, PcmRepresentation::Signed16}); analyzer.ClockStart(0);
        const auto tone = Tone(low); std::vector<std::int16_t> integers;
        for (float value : tone) integers.push_back(static_cast<std::int16_t>(value * 32768));
        Submit(analyzer, integers);
        Check(std::abs(Maximum(analyzer.Read()) - Maximum(first)) < 0.001f, "signed16 conversion");
        analyzer.Configure({48000, 1}); analyzer.ClockStart(0);
        Submit(analyzer, std::vector<float>(1000000, std::numeric_limits<float>::infinity()));
        const auto bounded = analyzer.Read();
        Check(bounded.samples == AudioSpectrum::Window && bounded.dropped > 0, "bounded oversized packets");
        for (float value : bounded.bands) Check(std::isfinite(value) && value >= 0 && value <= 1, "finite bounded");
        Submit(analyzer, Tone(low)); analyzer.Read(); analyzer.Permit(false);
        Submit(analyzer, Tone(high));
        Check(Maximum(analyzer.Read()) == 0, "pause stop clear and ignore future audio");
        analyzer.Permit(true); analyzer.ClockStart(10000000);
        Submit(analyzer, Tone(low), 0);
        Check(Maximum(analyzer.Read()) == 0, "seek rejects stale timestamps");
        Submit(analyzer, Tone(high), 10000000);
        Check(analyzer.Read().bands[highBand] > 0.4f, "seek restarts at target");
        analyzer.Shutdown(); Submit(analyzer, Tone(low), 20000000);
        Check(!analyzer.Read().available && Maximum(analyzer.Read()) == 0, "shutdown");
        AudioSpectrum next(8);
        Check(!next.Read().available && Maximum(next.Read()) == 0, "video only unconfigured");
        next.Configure({48000, 1}); next.Permit(true); next.ClockStart(0); Submit(next, Tone(high));
        Check(next.Read().generation == 8 && next.Read().bands[lowBand] < 0.01f, "generation isolation");
        // Contended callback and snapshot reads must finish without a growing queue or callback waits.
        const auto concurrentTone = Tone(high);
        std::thread producer([&] { for (int i = 0; i < 1000; ++i) Submit(next, concurrentTone); });
        for (int i = 0; i < 100; ++i) {
            const auto value = next.Read();
            for (float band : value.bands) Check(std::isfinite(band) && band >= 0 && band <= 1, "concurrent snapshot bounds");
        }
        producer.join();
        next.Shutdown();
        std::cout << "Deterministic spectrum tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
