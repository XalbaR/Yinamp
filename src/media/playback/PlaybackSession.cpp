#include "media/playback/PlaybackSession.h"
#include "media/playback/SpectrumTap.h"
#include "media/playback/HttpByteStream.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mutex>
#include <propvarutil.h>
#include <thread>
#include <vector>
namespace yinamp::media::playback {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result) {
    if (FAILED(result))
        throw result;
}
enum class Operation {
    Open,
    Close,
    Play,
    Pause,
    Stop,
    Seek,
    Audio,
    Quit
};
struct Command {
    Operation operation;
    std::filesystem::path path;
    std::uint64_t generation{};
    double seconds{};
    int volume{}, balance{};
    PlaybackSource source;
    std::stop_token stop;
    bool resumePaused{};
};
struct Event {
    std::uint64_t generation;
    ComPtr<IMFMediaEvent> event;
    HRESULT error;
};
struct Inbox {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> commands;
    std::deque<Event> events;
    std::stop_source cancellation;
};
// Callback has no owner or HWND pointer. Its inbox outlives any pending invocation.
class Callback final : public IMFAsyncCallback {
  public:
    Callback(std::shared_ptr<Inbox> inbox, IMFMediaSession* session, std::uint64_t generation)
        : inbox_(std::move(inbox)), session_(session), generation_(generation) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IMFAsyncCallback)) {
            *value = static_cast<IMFAsyncCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return ++refs_;
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto n = --refs_;
        if (!n)
            delete this;
        return n;
    }
    STDMETHODIMP GetParameters(DWORD*, DWORD*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP Invoke(IMFAsyncResult* result) override {
        ComPtr<IMFMediaEvent> event;
        const auto hr = session_->EndGetEvent(result, &event);
        MediaEventType type = MEUnknown;
        if (event)
            event->GetType(&type);
        {
            std::lock_guard lock(inbox_->mutex);
            inbox_->events.push_back({generation_, event, hr});
        }
        inbox_->wake.notify_one();
        if (SUCCEEDED(hr) && type != MESessionClosed) {
            const auto next = session_->BeginGetEvent(this, nullptr);
            if (FAILED(next) && next != MF_E_SHUTDOWN) {
                std::lock_guard lock(inbox_->mutex);
                inbox_->events.push_back({generation_, {}, next});
                inbox_->wake.notify_one();
            }
        }
        return S_OK;
    }

  private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<Inbox> inbox_;
    ComPtr<IMFMediaSession> session_;
    std::uint64_t generation_;
};
}
struct PlaybackSession::Impl {
    HWND hwnd;
    std::shared_ptr<Inbox> inbox{std::make_shared<Inbox>()};
    std::mutex viewMutex;
    Snapshot view;
    ComPtr<IMFVideoDisplayControl> displayView;
    std::thread worker;
    ComPtr<IMFMediaSession> session;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFPresentationClock> clock;
    ComPtr<IMFAudioStreamVolume> volumeService;
    ComPtr<IMFMediaTypeHandler> audioFormatHandler;
    ComPtr<IMFMediaType> audioSourceFormat;
    std::vector<ComPtr<IMFActivate>> renderers;
    ComPtr<Callback> callback;
    std::shared_ptr<AudioSpectrum> analyzer, analyzerView;
    bool enableSpectrum;
    std::filesystem::path activePath;
    Command activeCommand{Operation::Close};
    std::optional<ResolvedStreams> resolvedStreams;
    std::vector<ComPtr<IMFMediaSource>> onlineSources;
    std::vector<ComPtr<IMFByteStream>> onlineStreams;
    unsigned onlineRetries{};
    double startPosition{};
    bool restorePaused{};
    std::chrono::steady_clock::time_point openingDeadline{};
    unsigned processingMode{}; // EQ+spectrum, EQ, spectrum, plain playback.
    std::shared_ptr<EqualizerParameters> equalizerParameters{std::make_shared<EqualizerParameters>()};
    ComPtr<IMFTransform> equalizerTransform;
    Snapshot state;
    std::uint64_t sessionIdentity{};
    int volume{200}, balance{};
    bool ready{}, pauseAfterStart{}, wantPlaying{true};
    bool autoStart;
    bool accepting{true};
    HRESULT startupResult{E_FAIL};
    bool hasAudio{}, pausedSeekMute{}, pausePending{};
    TOPOID audioSourceNode{};
    explicit Impl(HWND window, bool start, bool spectrum) : hwnd(window), enableSpectrum(spectrum), autoStart(start) {
        worker = std::thread([this] { Run(); });
    }
    ~Impl() {
        Shutdown();
        if (SUCCEEDED(startupResult))
            MFShutdown();
    }
    void Shutdown() {
        if (!worker.joinable())
            return;
        {
            std::lock_guard lock(inbox->mutex);
            accepting = false;
            inbox->cancellation.request_stop();
            inbox->commands.clear();
            inbox->commands.push_back({Operation::Quit});
            inbox->wake.notify_one();
        }
        worker.join();
    }
    void Send(Command command) {
        std::lock_guard lock(inbox->mutex);
        if (!accepting)
            return;
        if (command.operation == Operation::Open || command.operation == Operation::Close) {
            inbox->cancellation.request_stop();
            inbox->cancellation = std::stop_source{};
            command.stop = inbox->cancellation.get_token();
        }
        inbox->commands.push_back(std::move(command));
        inbox->wake.notify_one();
    }
    void Publish() {
        std::lock_guard lock(viewMutex);
        view = state;
    }
    void Dispose() {
        state.channels = 0;
        state.sampleRateHz = 0;
        state.audioBitrateBps = 0;
        state.audioFormat.clear();
        state.videoFormat.clear();
        state.author.clear();
        state.videoWidth = state.videoHeight = 0;
        state.videoFps = 0;
        state.videoItag = state.audioItag = 0;
        audioFormatHandler.Reset();
        audioSourceFormat.Reset();
        hasAudio = false;
        audioSourceNode = 0;
        if (analyzer) analyzer->Shutdown();
        {
            std::lock_guard lock(viewMutex);
            displayView.Reset();
            analyzerView.reset();
        }
        clock.Reset();
        volumeService.Reset();
        if (session) {
            // Close is asynchronous. Only the worker waits; callback continues until MESessionClosed.
            if (SUCCEEDED(session->Close())) {
                std::unique_lock lock(inbox->mutex);
                inbox->wake.wait_for(lock, std::chrono::seconds(3), [this] {
                    return std::ranges::any_of(inbox->events, [this](const Event& e) {
                        MediaEventType type = MEUnknown;
                        if (e.event)
                            e.event->GetType(&type);
                        return e.generation == sessionIdentity && type == MESessionClosed;
                    });
                });
            }
            session->Shutdown();
        }
        if (source)
            source->Shutdown();
        for (auto& child : onlineSources) child->Shutdown();
        onlineSources.clear();
        for (auto& stream : onlineStreams) stream->Close();
        onlineStreams.clear();
        for (auto& renderer : renderers)
            renderer->ShutdownObject();
        renderers.clear();
        callback.Reset();
        source.Reset();
        session.Reset();
        analyzer.reset();
        state.spectrumAvailable = false;
        state.equalizerAvailable = false;
        equalizerTransform.Reset();
        ready = false;
        pauseAfterStart = false;
        pausePending = false;
        pausedSeekMute = false;
    }
    void Start(std::optional<double> seconds = {}) {
        PROPVARIANT position;
        PropVariantInit(&position);
        if (seconds) {
            position.vt = VT_I8;
            position.hVal.QuadPart = static_cast<LONGLONG>(*seconds * 10000000.0);
        }
        Check(session->Start(&GUID_NULL, &position));
    }
    void ApplyAudio() {
        if (!volumeService)
            return;
        UINT32 count{};
        Check(volumeService->GetChannelCount(&count));
        const auto gains = Gains(volume, balance);
        std::vector<float> levels(count, static_cast<float>(volume) / 255.0f);
        if (count >= 2) {
            levels[0] = gains[0];
            levels[1] = gains[1];
        }
        if (pausedSeekMute)
            std::fill(levels.begin(), levels.end(), 0.0f);
        Check(volumeService->SetAllVolumes(count, levels.data()));
    }
    void ReadAudioFormat() {
        state.channels = 0;
        state.sampleRateHz = 0;
        state.audioBitrateBps = 0;
        if (!hasAudio || !ready) return;
        // Use the selected source audio type for bitrate. Decoded float/PCM rates
        // describe decoder buffers, not the compressed track's kbps.
        ComPtr<IMFMediaType> sourceType;
        if (!audioFormatHandler || FAILED(audioFormatHandler->GetCurrentMediaType(&sourceType)))
            sourceType = audioSourceFormat;
        if (sourceType) {
            GUID subtype{};
            if (SUCCEEDED(sourceType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                if (subtype == MFAudioFormat_PCM) state.audioFormat = "PCM";
                else if (subtype == MFAudioFormat_Float) state.audioFormat = "Float PCM";
                else if (subtype == MFAudioFormat_AAC) state.audioFormat = "AAC";
                else if (subtype == MFAudioFormat_MP3) state.audioFormat = "MP3";
            }
            sourceType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &state.sampleRateHz);
            UINT32 bytesPerSecond{}, bitsPerSecond{};
            if (SUCCEEDED(sourceType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond)) && bytesPerSecond > 0)
                state.audioBitrateBps = static_cast<std::uint64_t>(bytesPerSecond) * 8;
            else if (SUCCEEDED(sourceType->GetUINT32(MF_MT_AVG_BITRATE, &bitsPerSecond)))
                state.audioBitrateBps = bitsPerSecond;
            if (!state.audioBitrateBps) {
                GUID pcmSubtype{};
                UINT32 channels{}, bits{};
                if (SUCCEEDED(sourceType->GetGUID(MF_MT_SUBTYPE, &pcmSubtype)) &&
                    (pcmSubtype == MFAudioFormat_PCM || pcmSubtype == MFAudioFormat_Float) &&
                    SUCCEEDED(sourceType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) &&
                    SUCCEEDED(sourceType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)))
                    state.audioBitrateBps = static_cast<std::uint64_t>(state.sampleRateHz) * channels * bits;
            }
        }
        ComPtr<IMFTopology> topology;
        if (FAILED(session->GetFullTopology(MFSESSION_GETFULLTOPOLOGY_CURRENT, 0, &topology))) return;
        ComPtr<IMFTopologyNode> node;
        if (FAILED(topology->GetNodeByID(audioSourceNode, &node))) return;
        WORD count{};
        if (FAILED(topology->GetNodeCount(&count))) return;
        // Read the first negotiated PCM format on the active audio branch rather
        // than the device's volume-service layout. This also works without a spectrum tap.
        for (WORD i = 0; i < count; ++i) {
            MF_TOPOLOGY_TYPE kind{};
            if (FAILED(node->GetNodeType(&kind))) return;
            ComPtr<IMFMediaType> type;
            HRESULT hr = E_FAIL;
            if (kind == MF_TOPOLOGY_OUTPUT_NODE) {
                ComPtr<IUnknown> object;
                ComPtr<IMFStreamSink> sink;
                ComPtr<IMFMediaTypeHandler> handler;
                if (SUCCEEDED(node->GetObject(&object)) && SUCCEEDED(object.As(&sink)) &&
                    SUCCEEDED(sink->GetMediaTypeHandler(&handler)))
                    hr = handler->GetCurrentMediaType(&type);
            } else if (kind != MF_TOPOLOGY_SOURCESTREAM_NODE) {
                hr = MFGetTopoNodeCurrentType(node.Get(), 0, TRUE, &type);
            }
            GUID major{}, subtype{};
            UINT32 channels{};
            if (SUCCEEDED(hr) && SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) && major == MFMediaType_Audio &&
                SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                (subtype == MFAudioFormat_PCM || subtype == MFAudioFormat_Float) &&
                SUCCEEDED(type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) && channels > 0) {
                state.channels = channels;
                if (!state.sampleRateHz)
                    type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &state.sampleRateHz);
                return;
            }
            ComPtr<IMFTopologyNode> next;
            DWORD input{};
            if (FAILED(node->GetOutput(0, &next, &input))) return;
            node = std::move(next);
        }
    }
    void Open(const Command& command) {
        const auto eqError = processingMode ? state.equalizerError : S_OK;
        const auto spectrumError = processingMode ? state.spectrumError : S_OK;
        Dispose();
        activePath = command.path;
        activeCommand = command;
        startPosition = command.seconds;
        restorePaused = command.resumePaused;
        state = {};
        state.equalizerError = eqError;
        state.spectrumError = spectrumError;
        state.generation = command.generation;
        state.state = State::Opening;
        openingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        Publish();
        wantPlaying = autoStart;
        Check(MFCreateMediaSession(nullptr, &session));
        callback.Attach(new Callback(inbox, session.Get(), ++sessionIdentity));
        Check(session->BeginGetEvent(callback.Get(), nullptr));
        ComPtr<IMFSourceResolver> resolver;
        Check(MFCreateSourceResolver(&resolver));
        MF_OBJECT_TYPE objectType{};
        ComPtr<IUnknown> object;
        if (command.source.Online()) {
            if (!resolvedStreams) resolvedStreams = command.source.resolve(command.source.videoId, command.stop);
            if (command.stop.stop_requested()) throw MF_E_OPERATION_CANCELLED;
            const auto openStream = [&](const native::StreamInfo& stream,
                                        const std::map<std::wstring, std::wstring, std::less<>>& headers) {
                if (stream.url.empty()) throw MF_E_INVALIDMEDIATYPE;
                auto bytes = CreateHttpByteStream(native::Utf8ToWide(stream.url), headers, command.stop);
                if (!bytes) throw E_OUTOFMEMORY;
                onlineStreams.push_back(bytes);
                ComPtr<IUnknown> media;
                Check(resolver->CreateObjectFromByteStream(bytes.Get(), L"stream.mp4", MF_RESOLUTION_MEDIASOURCE,
                                                          nullptr, &objectType, &media));
                ComPtr<IMFMediaSource> child;
                Check(media.As(&child));
                onlineSources.push_back(child);
            };
            openStream(resolvedStreams->video, resolvedStreams->videoHeaders);
            if (resolvedStreams->adaptive) {
                openStream(resolvedStreams->audio, resolvedStreams->audioHeaders);
                ComPtr<IMFCollection> collection;
                Check(MFCreateCollection(&collection));
                for (auto& child : onlineSources) Check(collection->AddElement(child.Get()));
                Check(MFCreateAggregateSource(collection.Get(), &source));
            } else source = onlineSources.front();
        } else {
            Check(resolver->CreateObjectFromURL(command.path.c_str(), MF_RESOLUTION_MEDIASOURCE, nullptr, &objectType,
                                                &object));
            Check(object.As(&source));
        }
        ComPtr<IMFPresentationDescriptor> presentation;
        Check(source->CreatePresentationDescriptor(&presentation));
        UINT64 duration{};
        if (SUCCEEDED(presentation->GetUINT64(MF_PD_DURATION, &duration)))
            state.duration = static_cast<double>(duration) / 10000000.0;
        if (resolvedStreams && state.duration <= 0) state.duration = resolvedStreams->durationSeconds;
        if (resolvedStreams) state.title = resolvedStreams->title;
        if (resolvedStreams) {
            state.author = resolvedStreams->author;
            state.videoItag = resolvedStreams->video.itag;
            state.audioItag = resolvedStreams->audio.itag;
        }
        DWORD characteristics{};
        Check(source->GetCharacteristics(&characteristics));
        state.seekable = (characteristics & MFMEDIASOURCE_CAN_SEEK) != 0;
        ComPtr<IMFTopology> topology;
        Check(MFCreateTopology(&topology));
        DWORD streams{};
        Check(presentation->GetStreamDescriptorCount(&streams));
        bool audio = false, video = false;
        ComPtr<IMFTopologyNode> audioInput, audioOutput;
        ComPtr<IMFMediaType> audioType;
        for (DWORD i = 0; i < streams; ++i) {
            BOOL selected{};
            ComPtr<IMFStreamDescriptor> stream;
            Check(presentation->GetStreamDescriptorByIndex(i, &selected, &stream));
            ComPtr<IMFMediaTypeHandler> handler;
            Check(stream->GetMediaTypeHandler(&handler));
            GUID major{};
            Check(handler->GetMajorType(&major));
            const bool useAudio = major == MFMediaType_Audio && !audio;
            const bool useVideo = major == MFMediaType_Video && !video;
            if (useVideo) {
                ComPtr<IMFMediaType> type;
                if (SUCCEEDED(handler->GetCurrentMediaType(&type))) {
                    GUID subtype{};
                    if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_H264)
                        state.videoFormat = "H.264";
                    MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &state.videoWidth, &state.videoHeight);
                    UINT32 numerator{}, denominator{};
                    if (SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) && denominator)
                        state.videoFps = static_cast<double>(numerator) / denominator;
                }
            }
            if (!useAudio && !useVideo) {
                Check(presentation->DeselectStream(i));
                continue;
            }
            Check(presentation->SelectStream(i));
            ComPtr<IMFActivate> renderer;
            if (useAudio) {
                Check(MFCreateAudioRendererActivate(&renderer));
                audio = true;
            } else {
                Check(MFCreateVideoRendererActivate(hwnd, &renderer));
                video = true;
            }
            renderers.push_back(renderer);
            ComPtr<IMFTopologyNode> input, output;
            Check(MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &input));
            Check(input->SetUnknown(MF_TOPONODE_SOURCE, source.Get()));
            Check(input->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, presentation.Get()));
            Check(input->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, stream.Get()));
            Check(MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &output));
            Check(output->SetObject(renderer.Get()));
            Check(output->SetUINT32(MF_TOPONODE_STREAMID, 0));
            Check(output->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE));
            Check(topology->AddNode(input.Get()));
            Check(topology->AddNode(output.Get()));
            Check(input->ConnectOutput(0, output.Get(), 0));
            if (useAudio) {
                audioInput = input; audioOutput = output;
                audioFormatHandler = handler;
                Check(input->GetTopoNodeID(&audioSourceNode));
                if (FAILED(handler->GetCurrentMediaType(&audioType)))
                    handler->GetMediaTypeByIndex(0, &audioType);
                if (audioType) {
                    Check(MFCreateMediaType(&audioSourceFormat));
                    Check(audioType->CopyAllItems(audioSourceFormat.Get()));
                }
            }
        }
        if (!audio && !video)
            throw MF_E_INVALIDMEDIATYPE;
        hasAudio = audio;
        state.video = video;
        if (audio && audioType) {
            for (; processingMode < 3; ++processingMode) {
                const bool useEq = processingMode < 2;
                const bool useSpectrum = enableSpectrum && processingMode != 1;
                if (!useEq && !useSpectrum) continue;
                ComPtr<IMFTopology> resolved;
                ComPtr<IMFActivate> sink;
                ComPtr<IMFTransform> transform;
                HRESULT hr = E_OUTOFMEMORY;
                try {
                    if (useSpectrum) analyzer = std::make_shared<AudioSpectrum>(state.generation);
                    hr = ResolveProcessingTopology(topology.Get(), audioInput.Get(), audioOutput.Get(),
                        audioType.Get(), analyzer, useEq ? equalizerParameters : nullptr, &resolved, &sink, &transform);
                    if (SUCCEEDED(hr)) hr = session->SetTopology(MFSESSION_SETTOPOLOGY_NORESOLUTION, resolved.Get());
                    if (SUCCEEDED(hr)) {
                        if (sink) renderers.push_back(sink);
                        equalizerTransform = transform;
                        state.equalizerAvailable = useEq;
                        state.spectrumAvailable = useSpectrum;
                        if (useEq) state.equalizerError = S_OK;
                        if (useSpectrum) {
                            state.spectrumError = S_OK;
                            analyzer->Permit(wantPlaying);
                            std::lock_guard lock(viewMutex); analyzerView = analyzer;
                        }
                        return;
                    }
                } catch (...) { hr = E_OUTOFMEMORY; }
                if (sink) sink->ShutdownObject();
                if (analyzer) analyzer->Shutdown();
                analyzer.reset();
                if (useEq) state.equalizerError = hr;
                if (useSpectrum) state.spectrumError = hr;
                OutputDebugStringW(L"Yinamp: optional EQ/spectrum topology unavailable; trying next playback mode.\n");
            }
        } else if (audio) {
            state.equalizerError = MF_E_INVALIDMEDIATYPE;
            if (enableSpectrum) state.spectrumError = MF_E_INVALIDMEDIATYPE;
        }
        Check(session->SetTopology(0, topology.Get()));
    }
    void EventReceived(const Event& event) {
        if (!session || event.generation != sessionIdentity)
            return;
        Check(event.error);
        HRESULT status{};
        Check(event.event->GetStatus(&status));
        Check(status);
        MediaEventType type{};
        Check(event.event->GetType(&type));
        if (type == MESessionTopologyStatus) {
            UINT32 topologyStatus{};
            Check(event.event->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &topologyStatus));
            if (topologyStatus != MF_TOPOSTATUS_READY)
                return;
            ready = true;
            ReadAudioFormat();
            ComPtr<IMFClock> genericClock;
            Check(session->GetClock(&genericClock));
            Check(genericClock.As(&clock));
            if (state.video) {
                ComPtr<IMFVideoDisplayControl> display;
                Check(MFGetService(session.Get(), MR_VIDEO_RENDER_SERVICE, IID_PPV_ARGS(&display)));
                Check(display->SetAspectRatioMode(MFVideoARMode_PreservePicture));
                Check(display->SetBorderColor(RGB(0, 0, 0)));
                std::lock_guard lock(viewMutex);
                displayView = display;
            }
            // Video-only presentations legitimately have no audio service.
            if (hasAudio)
                Check(MFGetService(session.Get(), MR_STREAM_VOLUME_SERVICE, IID_PPV_ARGS(&volumeService)));
            ApplyAudio();
            if (wantPlaying)
                {
                    pauseAfterStart = restorePaused;
                    Start(startPosition);
                }
            else
                state.state = State::Stopped;
        } else if (type == MESessionStarted) {
            if (pauseAfterStart || !wantPlaying) {
                pauseAfterStart = false;
                if (wantPlaying) {
                    Check(session->Pause());
                    pausePending = true;
                } else
                    Check(session->Stop());
            } else
                state.state = State::Playing;
        } else if (type == MESessionPaused) {
            pausePending = false;
            pausedSeekMute = false;
            ApplyAudio();
            state.state = State::Paused;
        } else if (type == MESessionStopped) {
            state.state = State::Stopped;
            state.position = 0;
        } else if (type == MESessionEnded) {
            if (analyzer) analyzer->Permit(false);
            state.state = State::Ended;
            state.position = state.duration;
        } else if (type == MEError)
            throw FAILED(status) ? status : E_FAIL;
    }
    void Execute(const Command& command) {
        switch (command.operation) {
        case Operation::Open:
            resolvedStreams.reset();
            onlineRetries = 0;
            processingMode = enableSpectrum ? 0u : 1u;
            state.equalizerError = S_OK;
            state.spectrumError = S_OK;
            Open(command);
            break;
        case Operation::Close:
            Dispose();
            state = {};
            break;
        case Operation::Audio:
            volume = std::clamp(command.volume, 0, 255);
            balance = std::clamp(command.balance, -127, 127);
            ApplyAudio();
            break;
        case Operation::Play:
            wantPlaying = true;
            if (analyzer) analyzer->Permit(true);
            if (session && ready)
                Start(state.state == State::Stopped || state.state == State::Ended ? std::optional<double>{0}
                                                                                   : std::nullopt);
            break;
        case Operation::Pause:
            if (analyzer) analyzer->Permit(false);
            if (session && ready && state.state == State::Playing && !pausePending) {
                Check(session->Pause());
                pausePending = true;
            }
            break;
        case Operation::Stop:
            if (analyzer) analyzer->Permit(false);
            if (equalizerTransform) Check(equalizerTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
            wantPlaying = false;
            pauseAfterStart = false;
            pausedSeekMute = false;
            ApplyAudio();
            if (session && ready)
                Check(session->Stop());
            state.state = State::Stopped;
            state.position = 0;
            break;
        case Operation::Seek:
            if (session && ready && state.seekable && state.duration > 0) {
                if (state.state == State::Stopped) {
                    state.position = 0;
                    break;
                }
                if (equalizerTransform) Check(equalizerTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0));
                pauseAfterStart = state.state == State::Paused;
                if (analyzer) { analyzer->Clear(); analyzer->Permit(!pauseAfterStart); }
                pausedSeekMute = pauseAfterStart;
                ApplyAudio();
                Start(ClampSeek(command.seconds, state.duration));
            }
            break;
        case Operation::Quit:
            break;
        }
    }
    void Run() noexcept {
        const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const auto startup = SUCCEEDED(com) ? MFStartup(MF_VERSION) : com;
        startupResult = startup;
        if (FAILED(startup)) {
            state.state = State::Failed;
            state.error = startup;
            Publish();
        }
        bool quit = false;
        while (!quit) {
            std::deque<Command> commands;
            std::deque<Event> events;
            {
                std::unique_lock lock(inbox->mutex);
                inbox->wake.wait_for(lock, std::chrono::milliseconds(100),
                                     [this] { return !inbox->commands.empty() || !inbox->events.empty(); });
                commands.swap(inbox->commands);
                events.swap(inbox->events);
            }
            const auto fail = [this](HRESULT hr) {
                // Opening failures advance the finite optional-mode hierarchy; stale session events stay isolated.
                if ((state.spectrumAvailable || state.equalizerAvailable) && processingMode < 3 && state.state == State::Opening) {
                    if (state.equalizerAvailable) state.equalizerError = hr;
                    if (state.spectrumAvailable) state.spectrumError = hr;
                    ++processingMode;
                    const auto retry = activeCommand;
                    try { Open(retry);
                        OutputDebugStringW(L"Yinamp: optional processing rejected asynchronously; retrying next playback mode.\n");
                        return;
                    } catch (...) {}
                }
                const auto network = hr == MF_E_NET_SERVER_ACCESSDENIED || hr == MF_E_NOT_FOUND ||
                    hr == MF_E_NET_TIMEOUT || hr == MF_E_NET_READ || hr == E_FAIL;
                if (activeCommand.source.Online() && network && onlineRetries == 0 &&
                    !activeCommand.stop.stop_requested()) {
                    ++onlineRetries;
                    auto retry = activeCommand;
                    retry.seconds = state.position;
                    retry.resumePaused = state.state == State::Paused || pausePending || pauseAfterStart;
                    const bool playing = wantPlaying;
                    resolvedStreams.reset();
                    const auto message = "Yinamp playback mode=online video=" + retry.source.videoId +
                        " re-resolve attempt=1 HRESULT=" + std::to_string(hr) + "\n";
                    OutputDebugStringA(message.c_str());
                    try { Open(retry); wantPlaying = playing; return; }
                    catch (...) { OutputDebugStringA("Yinamp online re-resolve failed\n"); }
                }
                const auto message = "Yinamp playback mode=" + std::string(activeCommand.source.Online() ? "online" : "download/local") +
                    " video=" + activeCommand.source.videoId + " failed HRESULT=" + std::to_string(hr) + "\n";
                OutputDebugStringA(message.c_str());
                state.state = State::Failed;
                state.error = hr;
                Dispose();
            };
            for (const auto& command : commands) {
                if (command.operation == Operation::Quit) {
                    quit = true;
                    break;
                }
                try {
                    if (FAILED(startup)) {
                        if (command.operation == Operation::Open)
                            state.generation = command.generation;
                        throw startup;
                    }
                    Execute(command);
                } catch (HRESULT hr) {
                    fail(hr);
                } catch (const native::HttpStatusError& error) {
                    OutputDebugStringA(("Yinamp online opening HTTP=" + std::to_string(error.status) + "\n").c_str());
                    fail(error.status == 404 ? MF_E_NOT_FOUND : MF_E_NET_SERVER_ACCESSDENIED);
                } catch (const native::NetworkError& error) {
                    OutputDebugStringA(("Yinamp online opening WinHTTP=" + std::to_string(error.code) + "\n").c_str());
                    fail(MF_E_NET_READ);
                } catch (...) {
                    fail(E_FAIL);
                }
            }
            if (!quit)
                for (const auto& event : events) {
                    try {
                        EventReceived(event);
                    } catch (HRESULT hr) {
                        fail(hr);
                    } catch (...) {
                        fail(E_FAIL);
                    }
                }
            if (clock && (state.state == State::Playing || state.state == State::Paused)) {
                MFTIME time{};
                if (SUCCEEDED(clock->GetTime(&time)))
                    state.position = state.duration > 0
                                         ? ClampSeek(static_cast<double>(time) / 10000000.0, state.duration)
                                         : std::max(0.0, static_cast<double>(time) / 10000000.0);
            }
            if (!quit && activeCommand.source.Online() && state.state == State::Opening &&
                std::chrono::steady_clock::now() > openingDeadline)
                fail(MF_E_NET_TIMEOUT);
            Publish();
        }
        Dispose();
        {
            std::lock_guard lock(inbox->mutex);
            inbox->events.clear();
        }
        if (SUCCEEDED(com))
            CoUninitialize();
    }
};
PlaybackSession::PlaybackSession(HWND window, bool autoStart, bool enableSpectrum)
    : impl_(std::make_unique<Impl>(window, autoStart, enableSpectrum)) {}
PlaybackSession::~PlaybackSession() = default;
void PlaybackSession::Shutdown() {
    impl_->Shutdown();
}
void PlaybackSession::Open(const std::filesystem::path& path, std::uint64_t generation) {
    impl_->Send({Operation::Open, path, generation});
}
void PlaybackSession::OpenSource(const PlaybackSource& source, std::uint64_t generation) {
    Command command{Operation::Open, source.localPath, generation};
    command.source = source;
    impl_->Send(std::move(command));
}
void PlaybackSession::Close() {
    impl_->Send({Operation::Close});
}
void PlaybackSession::Play() {
    impl_->Send({Operation::Play});
}
void PlaybackSession::Pause() {
    impl_->Send({Operation::Pause});
}
void PlaybackSession::Stop() {
    impl_->Send({Operation::Stop});
}
void PlaybackSession::Seek(double seconds) {
    impl_->Send({Operation::Seek, {}, 0, seconds});
}
void PlaybackSession::Audio(int volume, int balance) {
    impl_->Send({Operation::Audio, {}, 0, 0, volume, balance});
}
void PlaybackSession::Equalizer(bool enabled, const std::array<int, 11>& values) {
    impl_->equalizerParameters->Publish(enabled, values);
}
Snapshot PlaybackSession::Read() {
    std::lock_guard lock(impl_->viewMutex);
    return impl_->view;
}
SpectrumSnapshot PlaybackSession::Spectrum() {
    std::shared_ptr<AudioSpectrum> analyzer;
    { std::lock_guard lock(impl_->viewMutex); analyzer = impl_->analyzerView; }
    return analyzer ? analyzer->Read() : SpectrumSnapshot{};
}
ComPtr<IMFVideoDisplayControl> PlaybackSession::Display() {
    std::lock_guard lock(impl_->viewMutex);
    return impl_->displayView;
}
}
