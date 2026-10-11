#include "media/playback/EqualizerTransform.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <mfapi.h>
#include <mferror.h>
#include <wrl/client.h>
using namespace yinamp::media::playback;
using Microsoft::WRL::ComPtr;
namespace {
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Hr(HRESULT hr) {
    Check(SUCCEEDED(hr), "MF transform operation");
}
ComPtr<IMFMediaType> Type(unsigned rate = 48000, unsigned channels = 2) {
    ComPtr<IMFMediaType> type;
    Hr(MFCreateMediaType(&type));
    Hr(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    Hr(type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float));
    Hr(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate));
    Hr(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels));
    Hr(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32));
    Hr(type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4));
    Hr(type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 4));
    return type;
}
ComPtr<IMFSample> Sample(float value = 0.01f, unsigned count = 8192) {
    ComPtr<IMFSample> sample;
    Hr(MFCreateSample(&sample));
    ComPtr<IMFMediaBuffer> buffer;
    Hr(MFCreateMemoryBuffer(count * 4, &buffer));
    BYTE* bytes{};
    Hr(buffer->Lock(&bytes, nullptr, nullptr));
    for (unsigned i = 0; i < count; ++i)
        std::memcpy(bytes + i * 4, &value, 4);
    Hr(buffer->Unlock());
    Hr(buffer->SetCurrentLength(count * 4));
    Hr(sample->AddBuffer(buffer.Get()));
    Hr(sample->SetSampleTime(123456));
    Hr(sample->SetSampleDuration(654321));
    Hr(sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE));
    return sample;
}
ComPtr<IMFSample> Output(IMFTransform* transform) {
    MFT_OUTPUT_DATA_BUFFER output{};
    DWORD status{};
    Hr(transform->ProcessOutput(0, 1, &output, &status));
    Check(!status && !output.dwStatus && !output.pEvents, "output status");
    ComPtr<IMFSample> result;
    result.Attach(output.pSample);
    return result;
}
float Last(IMFSample* sample) {
    ComPtr<IMFMediaBuffer> buffer;
    Hr(sample->ConvertToContiguousBuffer(&buffer));
    BYTE* bytes{};
    DWORD count{};
    Hr(buffer->Lock(&bytes, nullptr, &count));
    float value{};
    std::memcpy(&value, bytes + count - 4, 4);
    Hr(buffer->Unlock());
    return value;
}
}
int main() {
    try {
        Hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        Hr(MFStartup(MF_VERSION));
        {
            auto parameters = std::make_shared<EqualizerParameters>();
            auto type = Type();
            ComPtr<IMFTransform> transform;
            Hr(CreateEqualizerTransform(type.Get(), parameters, &transform));
            Check(transform->SetInputType(1, type.Get(), 0) == MF_E_INVALIDSTREAMNUMBER, "stream validation");
            Hr(transform->SetInputType(0, type.Get(), MFT_SET_TYPE_TEST_ONLY));
            ComPtr<IMFMediaType> current;
            Check(transform->GetInputCurrentType(0, &current) == MF_E_TRANSFORM_TYPE_NOT_SET,
                  "test-only type does not commit");
            auto compressed = Type();
            Hr(compressed->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
            Check(transform->SetInputType(0, compressed.Get(), 0) == MF_E_INVALIDMEDIATYPE,
                  "compressed bytes rejected");
            Hr(transform->SetInputType(0, type.Get(), 0));
            auto mismatch = Type(44100);
            Check(transform->SetOutputType(0, mismatch.Get(), 0) == MF_E_INVALIDMEDIATYPE, "output rate unchanged");
            Hr(transform->SetOutputType(0, type.Get(), 0));
            MFT_OUTPUT_DATA_BUFFER empty{};
            DWORD status{};
            Check(transform->ProcessOutput(0, 1, &empty, &status) == MF_E_TRANSFORM_NEED_MORE_INPUT,
                  "no fabricated output");
            auto sample = Sample();
            Hr(transform->ProcessInput(0, sample.Get(), 0));
            Check(transform->ProcessInput(0, sample.Get(), 0) == MF_E_NOTACCEPTING, "bounded one-sample queue");
            Check(transform->SetInputType(0, type.Get(), 0) == MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING,
                  "pending type change rejected");
            auto output = Output(transform.Get());
            Check(output.Get() == sample.Get(), "in-place sample preservation");
            LONGLONG time{}, duration{};
            UINT32 clean{};
            Hr(output->GetSampleTime(&time));
            Hr(output->GetSampleDuration(&duration));
            Hr(output->GetUINT32(MFSampleExtension_CleanPoint, &clean));
            Check(time == 123456 && duration == 654321 && clean == TRUE && Last(output.Get()) == 0.01f,
                  "bypass metadata and samples preserved");
            std::array<int, 11> values{};
            values[0] = 60;
            parameters->Publish(true, values);
            sample = Sample();
            Hr(transform->ProcessInput(0, sample.Get(), 0));
            output = Output(transform.Get());
            Check(std::abs(Last(output.Get()) - 0.01 * std::pow(10.0, 6.0 / 20)) < 1e-7, "live preamp");
            for (auto message : {MFT_MESSAGE_COMMAND_FLUSH, MFT_MESSAGE_NOTIFY_END_STREAMING}) {
                sample = Sample();
                Hr(transform->ProcessInput(0, sample.Get(), 0));
                Hr(transform->ProcessMessage(message, 0));
                empty = {};
                Check(transform->ProcessOutput(0, 1, &empty, &status) == MF_E_TRANSFORM_NEED_MORE_INPUT,
                      "seek/stop/close flush discards pending sample");
                sample = Sample(0);
                Hr(transform->ProcessInput(0, sample.Get(), 0));
                output = Output(transform.Get());
                Check(Last(output.Get()) == 0, "flush no stale filter history");
            }
            values[0] = 0;
            values[1] = 120;
            parameters->Publish(true, values);
            sample = Sample(0.1f);
            Hr(transform->ProcessInput(0, sample.Get(), 0));
            Output(transform.Get());
            sample = Sample(0);
            Hr(sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE));
            Hr(transform->ProcessInput(0, sample.Get(), 0));
            output = Output(transform.Get());
            Check(Last(output.Get()) == 0, "discontinuity reset");
            Hr(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0));
            Check(transform->ProcessInput(0, sample.Get(), 0) == MF_E_NOTACCEPTING, "draining blocks input");
            Hr(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
            Hr(transform->ProcessInput(0, sample.Get(), 0));
            Output(transform.Get());
            Hr(transform->SetOutputType(0, nullptr, 0));
            Hr(transform->SetInputType(0, mismatch.Get(), 0));
            Hr(transform->SetOutputType(0, mismatch.Get(), 0));
            sample = Sample(0);
            Hr(transform->ProcessInput(0, sample.Get(), 0));
            output = Output(transform.Get());
            Check(Last(output.Get()) == 0, "sample rate change reset");
            sample = Sample(0, 3);
            Check(transform->ProcessInput(0, sample.Get(), 0) == MF_E_INVALID_STREAM_DATA, "partial frame rejected");
            ComPtr<IMFTransform> second;
            Hr(CreateEqualizerTransform(mismatch.Get(), parameters, &second));
            Hr(second->SetInputType(0, mismatch.Get(), 0));
            Hr(second->SetOutputType(0, mismatch.Get(), 0));
            transform.Reset(); // Parameters survive old source destruction; new source has independent histories.
            sample = Sample(0);
            Hr(second->ProcessInput(0, sample.Get(), 0));
            output = Output(second.Get());
            Check(Last(output.Get()) == 0, "source generation isolation and safe ownership");
        }
        Hr(MFShutdown());
        CoUninitialize();
        std::cout << "Equalizer synchronous MFT tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
