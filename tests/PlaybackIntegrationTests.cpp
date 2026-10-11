#include "HttpRangeFixture.h"
#include "TemporaryMediaRoot.h"
#include "media/playback/PlaybackSession.h"
#include "platform/windows/WindowsRuntime.h"
#include <fstream>
#include <iostream>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <thread>
#include <numbers>
using namespace yinamp;
using Microsoft::WRL::ComPtr;
namespace {
void Check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
void Hr(HRESULT value) {
    platform::CheckHResult(value, "Media Foundation integration");
}
void Wav(const std::filesystem::path& path, unsigned rate = 8000, bool tone = false, unsigned channels = 1) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&file](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            file.put(static_cast<char>(value >> (8 * i)));
    };
    file.write("RIFF", 4);
    word(36 + rate * channels * 2, 4);
    file.write("WAVEfmt ", 8);
    word(16, 4);
    word(1, 2);
    word(channels, 2);
    word(rate, 4);
    word(rate * channels * 2, 4);
    word(channels * 2, 2);
    word(16, 2);
    file.write("data", 4);
    word(rate * channels * 2, 4);
    for (unsigned i = 0; i < rate; ++i) {
        const auto value = tone ? static_cast<std::int16_t>(12000 * std::sin(2 * std::numbers::pi * 1500 * i / rate)) : std::int16_t{};
        for (unsigned channel = 0; channel < channels; ++channel)
            word(static_cast<std::uint16_t>(value), 2);
    }
}
void Video(const std::filesystem::path& path, bool audio = false, bool audioOnly = false) {
    ComPtr<IMFSinkWriter> writer;
    Hr(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer));
    ComPtr<IMFMediaType> output;
    Hr(MFCreateMediaType(&output));
    Hr(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    Hr(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    Hr(output->SetUINT32(MF_MT_AVG_BITRATE, 100000));
    Hr(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    Hr(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, 64, 64));
    Hr(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, 10, 1));
    Hr(MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    DWORD stream{};
    if (!audioOnly) {
    Hr(writer->AddStream(output.Get(), &stream));
    ComPtr<IMFMediaType> input;
    Hr(MFCreateMediaType(&input));
    Hr(output->CopyAllItems(input.Get()));
    Hr(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    Hr(writer->SetInputMediaType(stream, input.Get(), nullptr));
    }
    DWORD audioStream{};
    if (audio) {
        ComPtr<IMFMediaType> encoded; Hr(MFCreateMediaType(&encoded));
        Hr(encoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        Hr(encoded->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
        Hr(encoded->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        Hr(encoded->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
        Hr(encoded->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        Hr(encoded->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000));
        Hr(encoded->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0));
        Hr(encoded->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29));
        Hr(writer->AddStream(encoded.Get(), &audioStream));
        ComPtr<IMFMediaType> pcm; Hr(MFCreateMediaType(&pcm));
        Hr(pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        Hr(pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
        Hr(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        Hr(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
        Hr(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        Hr(pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
        Hr(pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000));
        Hr(writer->SetInputMediaType(audioStream, pcm.Get(), nullptr));
    }
    Hr(writer->BeginWriting());
    for (int i = 0; i < 40; ++i) {
        if (!audioOnly) {
        ComPtr<IMFMediaBuffer> buffer;
        Hr(MFCreateMemoryBuffer(6144, &buffer));
        BYTE* bytes{};
        Hr(buffer->Lock(&bytes, nullptr, nullptr));
        std::fill_n(bytes, 4096, static_cast<BYTE>(16 + i));
        std::fill_n(bytes + 4096, 2048, static_cast<BYTE>(128));
        Hr(buffer->Unlock());
        Hr(buffer->SetCurrentLength(6144));
        ComPtr<IMFSample> sample;
        Hr(MFCreateSample(&sample));
        Hr(sample->AddBuffer(buffer.Get()));
        Hr(sample->SetSampleTime(static_cast<LONGLONG>(i) * 1000000));
        Hr(sample->SetSampleDuration(1000000));
        Hr(writer->WriteSample(stream, sample.Get()));
        }
        if (audio) {
            ComPtr<IMFMediaBuffer> pcm; Hr(MFCreateMemoryBuffer(19200, &pcm));
            BYTE* data{}; Hr(pcm->Lock(&data, nullptr, nullptr));
            std::fill_n(data, 19200, static_cast<BYTE>(0)); Hr(pcm->Unlock()); Hr(pcm->SetCurrentLength(19200));
            ComPtr<IMFSample> audioSample; Hr(MFCreateSample(&audioSample)); Hr(audioSample->AddBuffer(pcm.Get()));
            Hr(audioSample->SetSampleTime(static_cast<LONGLONG>(i) * 1000000));
            Hr(audioSample->SetSampleDuration(1000000)); Hr(writer->WriteSample(audioStream, audioSample.Get()));
        }
    }
    Hr(writer->Finalize());
}
void Discover(const std::filesystem::path& path, const GUID& major, double duration) {
    ComPtr<IMFSourceResolver> resolver;
    Hr(MFCreateSourceResolver(&resolver));
    MF_OBJECT_TYPE type{};
    ComPtr<IUnknown> object;
    Hr(resolver->CreateObjectFromURL(path.c_str(), MF_RESOLUTION_MEDIASOURCE, nullptr, &type, &object));
    ComPtr<IMFMediaSource> source;
    Hr(object.As(&source));
    ComPtr<IMFPresentationDescriptor> presentation;
    Hr(source->CreatePresentationDescriptor(&presentation));
    UINT64 ticks{};
    Hr(presentation->GetUINT64(MF_PD_DURATION, &ticks));
    Check(std::abs(static_cast<double>(ticks) / 10000000.0 - duration) < 0.2, "source duration discovery");
    DWORD count{};
    Hr(presentation->GetStreamDescriptorCount(&count));
    Check(count == 1, "one stream fixture");
    BOOL selected{};
    ComPtr<IMFStreamDescriptor> stream;
    Hr(presentation->GetStreamDescriptorByIndex(0, &selected, &stream));
    ComPtr<IMFMediaTypeHandler> handler;
    Hr(stream->GetMediaTypeHandler(&handler));
    GUID found{};
    Hr(handler->GetMajorType(&found));
    Check(found == major, "source stream discovery/audio-only recognition");
    Hr(source->Shutdown());
}
media::playback::Snapshot Wait(media::playback::PlaybackSession& session, media::playback::State state,
                               std::uint64_t generation = 1) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto value = session.Read();
        if (value.generation != generation) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (value.state == state)
            return value;
        if (value.state == media::playback::State::Failed)
            throw std::runtime_error("Native session failed: " + std::to_string(value.error));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error("Native session state timeout");
}
}
int main() {
    try {
        platform::ComApartment apartment;
        Hr(MFStartup(MF_VERSION));
        struct Shutdown {
            ~Shutdown() {
                MFShutdown();
            }
        } shutdown;
        TemporaryMediaRoot temp;
        const auto wav = temp.path / L"generated.wav", video = temp.path / L"generated.mp4";
        Wav(wav);
        const auto stereo = temp.path / L"mono-in-name.wav";
        Wav(stereo, 48000, false, 2);
        const auto surround = temp.path / L"surround.wav";
        Wav(surround, 48000, false, 6);
        const auto unsupported = temp.path / L"low-rate.wav";
        Wav(unsupported, 4000);
        Video(video);
        const auto av = temp.path / L"generated-av.mp4", tone = temp.path / L"tone.wav";
        Video(av, true); Wav(tone, 48000, true);
        Discover(wav, MFMediaType_Audio, 1);
        Discover(video, MFMediaType_Video, 4);
        HWND window = CreateWindowExW(0, L"STATIC", L"Silent automated EVR test", WS_POPUP, 0, 0, 160, 160, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "hidden EVR HWND");
        struct Destroy {
            HWND hwnd;
            ~Destroy() {
                DestroyWindow(hwnd);
            }
        } destroy{window};
        using media::playback::State;
        {
            const auto audioOnly = temp.path / L"stream-audio.m4a";
            Video(audioOnly, true, true);
            const auto readFile = [](const std::filesystem::path& path) {
                std::ifstream file(path, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(file), {});
            };
            HttpRangeFixture server({{"/video", readFile(video)}, {"/audio", readFile(audioOnly)}, {"/muxed", readFile(av)}});
            unsigned resolves{};
            media::playback::PlaybackSource online;
            online.videoId = "AbCdEf012_-";
            online.resolve = [&](const std::string&, std::stop_token) {
                ++resolves;
                media::playback::ResolvedStreams streams;
                streams.video.url = media::native::WideToUtf8(server.Url(resolves == 1 ? L"/expired" : L"/video"));
                streams.audio.url = media::native::WideToUtf8(server.Url(L"/audio"));
                streams.adaptive = true;
                streams.durationSeconds = 4;
                streams.author = "Sanatçı";
                streams.video.itag = 137;
                streams.audio.itag = 140;
                return streams;
            };
            media::playback::PlaybackSession session(window, false, false);
            session.Audio(0, 0);
            session.OpenSource(online, 40);
            const auto ready = Wait(session, State::Stopped, 40);
            Check(resolves == 2 && server.expiredRequests == 1, "403 re-resolves once");
            Check(ready.video && ready.channels == 2 && ready.seekable && ready.duration >= 3.9, "separate HTTP sources combine A/V and seek");
            Check(ready.author == "Sanatçı" && ready.videoItag == 137 && ready.audioItag == 140,
                  "Online snapshot exposes only author and selected stream identities");
            session.Play(); Wait(session, State::Playing, 40);
            session.Pause(); Wait(session, State::Paused, 40);
            session.Seek(1.5);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const auto paused = Wait(session, State::Paused, 40);
            Check(paused.position >= 1.4 && paused.position < 2, "Online paused seek shares A/V clock");
            session.Play(); Wait(session, State::Playing, 40);
            session.Stop(); Check(Wait(session, State::Stopped, 40).position == 0, "Online stop resets both streams");
            session.Play(); Wait(session, State::Playing, 40);
            session.Seek(3.8); Wait(session, State::Ended, 40);
            session.Close();
            const auto closedOnline = Wait(session, State::Closed, 0);
            Check(closedOnline.author.empty() && closedOnline.videoItag == 0 && closedOnline.audioItag == 0 &&
                  closedOnline.audioFormat.empty() && closedOnline.videoWidth == 0,
                  "Close clears Track Info metadata before another source opens");
            online.resolve = [&](const std::string&, std::stop_token) {
                ++resolves;
                media::playback::ResolvedStreams streams;
                streams.video.url = media::native::WideToUtf8(server.Url(L"/muxed"));
                streams.audio = streams.video; return streams;
            };
            session.OpenSource(online, 41);
            Check(Wait(session, State::Stopped, 41).channels == 2, "progressive Online fallback has audio");
            session.Close(); Wait(session, State::Closed, 0);
            const auto beforeFailure = resolves;
            online.resolve = [&](const std::string&, std::stop_token) -> media::playback::ResolvedStreams {
                ++resolves; throw media::native::HttpStatusError(403);
            };
            session.OpenSource(online, 42);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (session.Read().state != State::Failed && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Check(session.Read().state == State::Failed && resolves == beforeFailure + 2, "persistent expiry has bounded retry");
        }
        {
            media::playback::PlaybackSession session(window, false);
            session.Open(wav, 9);
            const auto audioReady = Wait(session, State::Stopped, 9);
            Check(!audioReady.video && audioReady.channels == 1 && !session.Display(),
                  "audio topology and volume service without video");
            if (audioReady.sampleRateHz != 8000 || audioReady.audioBitrateBps != 128000)
                throw std::runtime_error("PCM source rate=" + std::to_string(audioReady.sampleRateHz) +
                                         " bitrate=" + std::to_string(audioReady.audioBitrateBps));
            if (!audioReady.spectrumAvailable) throw std::runtime_error("negotiated native PCM tap HRESULT " + std::to_string(audioReady.spectrumError));
            Check(audioReady.equalizerAvailable, "float equalizer resolved before native PCM tee");
            session.Play();
            Wait(session, State::Playing, 9);
            const auto sampleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
            while (session.Spectrum().samples == 0 && std::chrono::steady_clock::now() < sampleDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto actual = session.Spectrum();
            Check(actual.samples > 0 && actual.generation == 9, "actual clock-scheduled decoded silent PCM callbacks");
            for (float band : actual.bands) Check(band == 0, "native silence has no false activity");
            session.Pause(); Wait(session, State::Paused, 9);
            Check(session.Spectrum().samples == 0, "pause clears analysis");
            session.Seek(0.3);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Check(session.Spectrum().samples == 0, "paused seek does not analyze future audio");
            session.Play(); Wait(session, State::Playing, 9);
            session.Stop(); Wait(session, State::Stopped, 9);
            Check(session.Spectrum().samples == 0, "stop clears analysis");
            session.Audio(0, 0);
            session.Audio(255, -127);
            session.Audio(255, 0);
            session.Audio(255, 127);
            session.Close();
            Check(Wait(session, State::Closed, 0).channels == 0, "close clears negotiated audio format");
            session.Open(unsupported, 10);
            const auto fallback = Wait(session, State::Stopped, 10);
            Check(!fallback.spectrumAvailable && FAILED(fallback.spectrumError) && fallback.channels == 1,
                  "unsupported analysis format preserves normal audio renderer");
            Check(!fallback.equalizerAvailable && FAILED(fallback.equalizerError), "unsupported EQ preserves normal renderer");
            session.Play(); Wait(session, State::Playing, 10);
            Check(!session.Spectrum().available, "fallback playback has no false analyzer");
            session.Close(); Wait(session, State::Closed, 0);
            session.Open(tone, 11); Wait(session, State::Stopped, 11);
            session.Audio(0, 0); // Generated signal is observed before SAR volume; output is muted before Start.
            session.Play(); Wait(session, State::Playing, 11);
            const auto toneDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
            bool energy = false;
            while (std::chrono::steady_clock::now() < toneDeadline) {
                const auto spectrum = session.Spectrum();
                if (spectrum.bands[media::playback::AudioSpectrum::BandFor(1500, 48000)] > 0.3f) { energy = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            Check(energy, "muted real decoded sine activates expected band");
            session.Close(); Wait(session, State::Closed, 0);
            // Same generated decoded tone, attenuated by real preamp upstream of the scheduled tap.
            std::array<int, 11> curve{}; curve[0] = -120;
            session.Equalizer(true, curve);
            session.Open(tone, 13);
            Check(Wait(session, State::Stopped, 13).equalizerAvailable, "new generation retains enabled equalizer");
            session.Audio(0, 0); session.Play(); Wait(session, State::Playing, 13);
            float reduced{};
            const auto eqDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
            while (std::chrono::steady_clock::now() < eqDeadline) {
                reduced = std::max(reduced, session.Spectrum().bands[media::playback::AudioSpectrum::BandFor(1500, 48000)]);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Check(reduced > 0.35f && reduced < 0.7f, "native post-EQ spectrum sees actual 12 dB attenuation before muted SAR");
            session.Equalizer(false, curve); // Live update neither opens a source nor changes its generation.
            Check(session.Read().generation == 13 && session.Read().equalizerAvailable, "live bypass keeps source and topology");
            session.Stop(); Wait(session, State::Stopped, 13);
            session.Close(); Wait(session, State::Closed, 0);
            session.Open(av, 12);
            const auto avReady = Wait(session, State::Stopped, 12);
            Check(avReady.video && avReady.channels == 2 && avReady.spectrumAvailable && session.Display(),
                  "AAC decoded PCM tee and EVR in same native session");
            Check(avReady.equalizerAvailable, "AAC EQ, actual PCM spectrum and EVR resolve together");
            Check(avReady.sampleRateHz == 48000 && avReady.audioBitrateBps == 128000,
                  "AAC bitrate comes from encoded audio rather than decoded PCM or video");
            Check(avReady.audioFormat == "AAC" && avReady.videoFormat == "H.264" &&
                  avReady.videoWidth > 0 && avReady.videoHeight > 0 && avReady.videoFps > 0,
                  "Track Info receives actual source codecs and video geometry");
            session.Play(); Wait(session, State::Playing, 12);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            Check(session.Spectrum().samples > 0 && session.Spectrum().generation == 12, "AAC scheduled PCM and source isolation");
            session.Close(); Wait(session, State::Closed, 0);

            session.Open(video, 1);
            const auto ready = Wait(session, State::Stopped);
            Check(ready.video && ready.seekable && ready.duration > 3.9 && session.Display(),
                  "video topology and EVR service ready");
            Check(!ready.spectrumAvailable && !session.Spectrum().available, "video-only has no analyzer");
            Check(!ready.equalizerAvailable, "video-only creates no EQ DSP");
            Check(ready.channels == 0, "video-only clears negotiated audio format");
            Check(ready.sampleRateHz == 0 && ready.audioBitrateBps == 0, "video-only clears audio metadata");
            session.Play();
            Wait(session, State::Playing);
            session.Pause();
            Wait(session, State::Paused);
            session.Seek(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
            const auto paused = Wait(session, State::Paused);
            Check(paused.position >= 0.9 && paused.position < 1.5, "paused seek preserves pause");
            session.Play();
            Wait(session, State::Playing);
            session.Stop();
            const auto stopped = Wait(session, State::Stopped);
            Check(stopped.position == 0, "native Stop resets");
            session.Play();
            Wait(session, State::Playing);
            session.Seek(3.8);
            Wait(session, State::Ended);
            session.Close();
            Wait(session, State::Closed, 0);
            session.Open(temp.path / L"missing.mp4", 2);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (session.Read().state != State::Failed && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Check(session.Read().state == State::Failed, "missing source async failure");
            Check(session.Read().channels == 0, "failed source has no active audio format");
            Check(session.Read().sampleRateHz == 0 && session.Read().audioBitrateBps == 0,
                  "failed source clears audio metadata");
            session.Open(video, 3);
            Wait(session, State::Stopped, 3);
            session.Play();
            Wait(session, State::Playing, 3);
            // Destruction during silent video playback exercises close/callback/shutdown ordering.
        }
        // Format reporting must work with and without the optional spectrum branch,
        // and be replaced even when opening a new source without an explicit Close.
        for (bool spectrum : {false, true}) {
            media::playback::PlaybackSession session(window, false, spectrum);
            session.Open(wav, 20);
            Check(Wait(session, State::Stopped, 20).channels == 1, "negotiated mono independent of spectrum");
            session.Open(stereo, 21);
            const auto stereoReady = Wait(session, State::Stopped, 21);
            Check(stereoReady.equalizerAvailable && stereoReady.spectrumAvailable == spectrum,
                  "EQ-only and EQ-plus-spectrum topology modes both resolve");
            Check(stereoReady.sampleRateHz == 48000 && stereoReady.audioBitrateBps == 1536000,
                  "source stereo PCM metadata independent of spectrum");
            if (stereoReady.channels != 2)
                throw std::runtime_error("stereo channels=" + std::to_string(stereoReady.channels) +
                                         " spectrum=" + std::to_string(spectrum));
            session.Open(surround, 26);
            Check(Wait(session, State::Stopped, 26).channels >= 2, "multichannel source negotiates stereo or more");
            session.Audio(0, 127);
            session.Open(wav, 22);
            Check(Wait(session, State::Stopped, 22).channels == 1, "stereo to mono independent of volume service");
            session.Open(video, 23);
            Check(Wait(session, State::Stopped, 23).channels == 0, "audio to video-only clears format");
            session.Open(stereo, 24);
            Check(Wait(session, State::Stopped, 24).channels == 2, "video-only to audio restores format");
            session.Open(temp.path / L"missing.wav", 25);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (session.Read().state != State::Failed && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Check(session.Read().state == State::Failed && session.Read().channels == 0,
                  "audio to failed source clears format");
            Check(!session.Read().equalizerAvailable, "failed source detaches equalizer transform");
            session.Close();
            const auto closed = Wait(session, State::Closed, 0);
            Check(closed.channels == 0 && closed.sampleRateHz == 0 && closed.audioBitrateBps == 0,
                  "closed source has no audio format or metadata");
            Check(!closed.equalizerAvailable, "close releases equalizer generation");
        }
        std::cout << "MF source, duration, streams, video topology, EVR, silent transport, seek, end, failures and "
                     "shutdown passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
