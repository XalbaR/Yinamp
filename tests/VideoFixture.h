#pragma once
#include "platform/windows/WindowsRuntime.h"
#include <algorithm>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
namespace yinamp {
inline void VideoFixtureHr(HRESULT value) { platform::CheckHResult(value, "Video UI fixture"); }
using Microsoft::WRL::ComPtr;
inline void WriteVideoFixture(const std::filesystem::path& path, bool audio = false, bool audioOnly = false) {
    ComPtr<IMFSinkWriter> writer;
    VideoFixtureHr(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer));
    ComPtr<IMFMediaType> output;
    VideoFixtureHr(MFCreateMediaType(&output));
    VideoFixtureHr(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    VideoFixtureHr(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    VideoFixtureHr(output->SetUINT32(MF_MT_AVG_BITRATE, 100000));
    VideoFixtureHr(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    VideoFixtureHr(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, 64, 64));
    VideoFixtureHr(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, 10, 1));
    VideoFixtureHr(MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    DWORD stream{};
    if (!audioOnly) {
    VideoFixtureHr(writer->AddStream(output.Get(), &stream));
    ComPtr<IMFMediaType> input;
    VideoFixtureHr(MFCreateMediaType(&input));
    VideoFixtureHr(output->CopyAllItems(input.Get()));
    VideoFixtureHr(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    VideoFixtureHr(writer->SetInputMediaType(stream, input.Get(), nullptr));
    }
    DWORD audioStream{};
    if (audio) {
        ComPtr<IMFMediaType> encoded; VideoFixtureHr(MFCreateMediaType(&encoded));
        VideoFixtureHr(encoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        VideoFixtureHr(encoded->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
        VideoFixtureHr(encoded->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        VideoFixtureHr(encoded->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
        VideoFixtureHr(encoded->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        VideoFixtureHr(encoded->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000));
        VideoFixtureHr(encoded->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0));
        VideoFixtureHr(encoded->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29));
        VideoFixtureHr(writer->AddStream(encoded.Get(), &audioStream));
        ComPtr<IMFMediaType> pcm; VideoFixtureHr(MFCreateMediaType(&pcm));
        VideoFixtureHr(pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        VideoFixtureHr(pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
        VideoFixtureHr(pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
        VideoFixtureHr(pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
        VideoFixtureHr(pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        VideoFixtureHr(pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
        VideoFixtureHr(pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000));
        VideoFixtureHr(writer->SetInputMediaType(audioStream, pcm.Get(), nullptr));
    }
    VideoFixtureHr(writer->BeginWriting());
    for (int i = 0; i < 40; ++i) {
        if (!audioOnly) {
        ComPtr<IMFMediaBuffer> buffer;
        VideoFixtureHr(MFCreateMemoryBuffer(6144, &buffer));
        BYTE* bytes{};
        VideoFixtureHr(buffer->Lock(&bytes, nullptr, nullptr));
        std::fill_n(bytes, 4096, static_cast<BYTE>(16 + i));
        std::fill_n(bytes + 4096, 2048, static_cast<BYTE>(128));
        VideoFixtureHr(buffer->Unlock());
        VideoFixtureHr(buffer->SetCurrentLength(6144));
        ComPtr<IMFSample> sample;
        VideoFixtureHr(MFCreateSample(&sample));
        VideoFixtureHr(sample->AddBuffer(buffer.Get()));
        VideoFixtureHr(sample->SetSampleTime(static_cast<LONGLONG>(i) * 1000000));
        VideoFixtureHr(sample->SetSampleDuration(1000000));
        VideoFixtureHr(writer->WriteSample(stream, sample.Get()));
        }
        if (audio) {
            ComPtr<IMFMediaBuffer> pcm; VideoFixtureHr(MFCreateMemoryBuffer(19200, &pcm));
            BYTE* data{}; VideoFixtureHr(pcm->Lock(&data, nullptr, nullptr));
            std::fill_n(data, 19200, static_cast<BYTE>(0)); VideoFixtureHr(pcm->Unlock()); VideoFixtureHr(pcm->SetCurrentLength(19200));
            ComPtr<IMFSample> audioSample; VideoFixtureHr(MFCreateSample(&audioSample)); VideoFixtureHr(audioSample->AddBuffer(pcm.Get()));
            VideoFixtureHr(audioSample->SetSampleTime(static_cast<LONGLONG>(i) * 1000000));
            VideoFixtureHr(audioSample->SetSampleDuration(1000000)); VideoFixtureHr(writer->WriteSample(audioStream, audioSample.Get()));
        }
    }
    VideoFixtureHr(writer->Finalize());
}
}
