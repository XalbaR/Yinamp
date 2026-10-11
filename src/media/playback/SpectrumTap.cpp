#include "media/playback/SpectrumTap.h"
#include "media/playback/EqualizerTransform.h"
#include <mfapi.h>
#include <mferror.h>
namespace yinamp::media::playback {
using Microsoft::WRL::ComPtr;
namespace {
class Tap final : public IMFSampleGrabberSinkCallback {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<AudioSpectrum> analyzer_;

  public:
    explicit Tap(std::shared_ptr<AudioSpectrum> analyzer) : analyzer_(std::move(analyzer)) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IMFSampleGrabberSinkCallback) && iid != __uuidof(IMFClockStateSink))
            return E_NOINTERFACE;
        *value = static_cast<IMFSampleGrabberSinkCallback*>(this);
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return ++refs_;
    }
    STDMETHODIMP_(ULONG) Release() override {
        const auto count = --refs_;
        if (!count)
            delete this;
        return count;
    }
    STDMETHODIMP OnSetPresentationClock(IMFPresentationClock*) override {
        return S_OK;
    }
    STDMETHODIMP OnProcessSample(REFGUID major, DWORD, LONGLONG time, LONGLONG, const BYTE* bytes,
                                 DWORD size) override {
        if (major == MFMediaType_Audio && bytes)
            analyzer_->Submit({bytes, size}, time);
        return S_OK;
    }
    STDMETHODIMP OnShutdown() override {
        analyzer_->ClockHalt();
        return S_OK;
    }
    STDMETHODIMP OnClockStart(MFTIME, LONGLONG offset) override {
        analyzer_->ClockStart(offset == PRESENTATION_CURRENT_POSITION ? 0 : offset);
        return S_OK;
    }
    STDMETHODIMP OnClockStop(MFTIME) override {
        analyzer_->ClockHalt();
        return S_OK;
    }
    STDMETHODIMP OnClockPause(MFTIME) override {
        analyzer_->ClockHalt();
        return S_OK;
    }
    STDMETHODIMP OnClockRestart(MFTIME) override {
        analyzer_->ClockStart(0);
        return S_OK;
    }
    STDMETHODIMP OnClockSetRate(MFTIME, float) override {
        return S_OK;
    }
};
void Check(HRESULT result) {
    if (FAILED(result))
        throw result;
}
}
HRESULT ResolveProcessingTopology(IMFTopology* original, IMFTopologyNode* input, IMFTopologyNode* output,
                                  IMFMediaType* sourceType, std::shared_ptr<AudioSpectrum> analyzer,
                                  std::shared_ptr<EqualizerParameters> equalizer, IMFTopology** resolved,
                                  IMFActivate** activation, IMFTransform** transform) {
    if (!resolved || !activation || !transform)
        return E_POINTER;
    *resolved = nullptr;
    *activation = nullptr;
    *transform = nullptr;
    ComPtr<IMFActivate> sink;
    ComPtr<IMFTransform> dsp;
    try {
        UINT32 rate{}, channels{};
        Check(sourceType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate));
        Check(sourceType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
        if (!PcmFormat{rate, channels}.Valid())
            throw MF_E_INVALIDMEDIATYPE;
        ComPtr<IMFTopology> topology;
        Check(MFCreateTopology(&topology));
        Check(topology->CloneFrom(original));
        TOPOID inputId{}, outputId{};
        Check(input->GetTopoNodeID(&inputId));
        Check(output->GetTopoNodeID(&outputId));
        ComPtr<IMFTopologyNode> clonedInput, clonedOutput;
        Check(topology->GetNodeByID(inputId, &clonedInput));
        Check(topology->GetNodeByID(outputId, &clonedOutput));
        ComPtr<IMFMediaType> type;
        Check(MFCreateMediaType(&type));
        Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        Check(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float));
        Check(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate));
        Check(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels));
        Check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32));
        Check(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4));
        Check(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 4));
        Check(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
        UINT32 mask{};
        if (SUCCEEDED(sourceType->GetUINT32(MF_MT_AUDIO_CHANNEL_MASK, &mask)))
            Check(type->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK, mask));
        Check(clonedInput->DisconnectOutput(0));
        ComPtr<IMFTopologyNode> processingInput = clonedInput;
        if (equalizer) {
            Check(CreateEqualizerTransform(type.Get(), equalizer, &dsp));
            Check(dsp->SetInputType(0, type.Get(), 0));
            Check(dsp->SetOutputType(0, type.Get(), 0));
            ComPtr<IMFTopologyNode> node;
            Check(MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &node));
            Check(node->SetObject(dsp.Get()));
            Check(node->SetInputPrefType(0, type.Get()));
            Check(node->SetOutputPrefType(0, type.Get()));
            Check(topology->AddNode(node.Get()));
            Check(clonedInput->ConnectOutput(0, node.Get(), 0));
            processingInput = node;
        }
        ComPtr<IMFTopologyNode> grabber;
        if (analyzer) {
            ComPtr<Tap> callback;
            callback.Attach(new Tap(analyzer));
            Check(MFCreateSampleGrabberSinkActivate(type.Get(), callback.Get(), &sink));
            Check(sink->SetUINT32(MF_SAMPLEGRABBERSINK_IGNORE_CLOCK, FALSE));
            ComPtr<IMFTopologyNode> tee;
            Check(MFCreateTopologyNode(MF_TOPOLOGY_TEE_NODE, &tee));
            Check(tee->SetInputPrefType(0, type.Get()));
            Check(MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &grabber));
            Check(grabber->SetObject(sink.Get()));
            Check(grabber->SetUINT32(MF_TOPONODE_STREAMID, 0));
            Check(grabber->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE));
            Check(topology->AddNode(tee.Get()));
            Check(topology->AddNode(grabber.Get()));
            Check(processingInput->ConnectOutput(0, tee.Get(), 0));
            Check(tee->ConnectOutput(0, clonedOutput.Get(), 0));
            Check(tee->ConnectOutput(1, grabber.Get(), 0));
        } else
            Check(processingInput->ConnectOutput(0, clonedOutput.Get(), 0));
        // The standalone topology loader requires actual stream sinks, unlike Media Session's resolver.
        WORD nodeCount{};
        Check(topology->GetNodeCount(&nodeCount));
        for (WORD i = 0; i < nodeCount; ++i) {
            ComPtr<IMFTopologyNode> node;
            Check(topology->GetNode(i, &node));
            MF_TOPOLOGY_TYPE nodeType{};
            Check(node->GetNodeType(&nodeType));
            if (nodeType != MF_TOPOLOGY_OUTPUT_NODE)
                continue;
            ComPtr<IUnknown> object;
            Check(node->GetObject(&object));
            ComPtr<IMFActivate> activate;
            Check(object.As(&activate));
            ComPtr<IMFMediaSink> mediaSink;
            Check(activate->ActivateObject(IID_PPV_ARGS(&mediaSink)));
            ComPtr<IMFStreamSink> streamSink;
            Check(mediaSink->GetStreamSinkByIndex(0, &streamSink));
            Check(node->SetObject(streamSink.Get()));
        }
        ComPtr<IMFTopoLoader> loader;
        Check(MFCreateTopoLoader(&loader));
        ComPtr<IMFTopology> full;
        Check(loader->Load(topology.Get(), &full, nullptr));
        if (analyzer) {
            TOPOID grabberId{};
            Check(grabber->GetTopoNodeID(&grabberId));
            ComPtr<IMFTopologyNode> negotiatedNode;
            Check(full->GetNodeByID(grabberId, &negotiatedNode));
            ComPtr<IUnknown> object;
            Check(negotiatedNode->GetObject(&object));
            ComPtr<IMFStreamSink> stream;
            Check(object.As(&stream));
            ComPtr<IMFMediaTypeHandler> handler;
            Check(stream->GetMediaTypeHandler(&handler));
            ComPtr<IMFMediaType> negotiated;
            Check(handler->GetCurrentMediaType(&negotiated));
            GUID major{}, subtype{};
            UINT32 bits{}, alignment{};
            Check(negotiated->GetGUID(MF_MT_MAJOR_TYPE, &major));
            Check(negotiated->GetGUID(MF_MT_SUBTYPE, &subtype));
            Check(negotiated->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate));
            Check(negotiated->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
            Check(negotiated->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits));
            Check(negotiated->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &alignment));
            PcmRepresentation representation;
            if (subtype == MFAudioFormat_Float && bits == 32)
                representation = PcmRepresentation::Float32;
            else if (subtype == MFAudioFormat_PCM && bits == 16)
                representation = PcmRepresentation::Signed16;
            else
                throw MF_E_INVALIDMEDIATYPE;
            if (major != MFMediaType_Audio || alignment != channels * bits / 8 ||
                !analyzer->Configure({rate, channels, representation}))
                throw MF_E_INVALIDMEDIATYPE;
        }
        *resolved = full.Detach();
        *activation = sink.Detach();
        *transform = dsp.Detach();
        return S_OK;
    } catch (HRESULT hr) {
        if (sink)
            sink->ShutdownObject();
        return hr;
    } catch (...) {
        if (sink)
            sink->ShutdownObject();
        return E_FAIL;
    }
}
}
