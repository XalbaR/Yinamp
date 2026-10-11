#pragma once

#include <cstdint>
#include <filesystem>
#include <stop_token>

namespace yinamp::media::native {
void ValidateAvcAacMp4(const std::filesystem::path& inputPath, std::stop_token stop = {});

struct MuxResult {
    std::uint64_t finalBytes{};
    std::uint64_t videoSamples{};
    std::uint64_t audioSamples{};
};

struct TrackExtractResult {
    std::uint64_t finalBytes{};
    std::uint64_t samples{};
};

// V6'da doğrulanan YouTube fMP4 -> standart flat MP4 mux yolunu kullanır.
// Girdiler yeniden encode edilmez; H.264 ve AAC sample'ları doğrudan kopyalanır.
MuxResult MuxFmp4ToFlatMp4(const std::filesystem::path& videoPath, const std::filesystem::path& audioPath,
                           const std::filesystem::path& outputPath, std::stop_token stop = {});

// AAC track'ini yeniden encode etmeden tek track'li standart M4A'ya yazar.
// Girdi YouTube fMP4 ya da normal flat MP4 olabilir.
TrackExtractResult ExtractAacTrackToM4a(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
                                        std::stop_token stop = {});

// H.264 track'ini yeniden encode etmeden tek track'li sessiz MP4'e yazar.
// Deterministik container testlerinde video sample korumasını da doğrular.
TrackExtractResult ExtractH264TrackToMp4(const std::filesystem::path& inputPath,
                                         const std::filesystem::path& outputPath, std::stop_token stop = {});

}
