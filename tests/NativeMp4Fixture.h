#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace fixture {
using Bytes = std::vector<std::uint8_t>;
inline void U32(Bytes& bytes, std::uint32_t value) {
    for (int shift : {24, 16, 8, 0})
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
inline Bytes Words(std::initializer_list<std::uint32_t> words) {
    Bytes result;
    for (auto word : words)
        U32(result, word);
    return result;
}
inline void Append(Bytes& target, const Bytes& source) {
    target.insert(target.end(), source.begin(), source.end());
}
inline Bytes Box(std::string_view type, Bytes body) {
    Bytes result;
    U32(result, static_cast<std::uint32_t>(body.size() + 8));
    result.insert(result.end(), type.begin(), type.end());
    Append(result, body);
    return result;
}
inline Bytes Fragmented(bool video, bool defaults = false) {
    auto entry = Bytes(video ? 78 : 28, 0);
    // Sample entry offsets include its eight-byte box header.
    if (video) {
        entry[25] = 16;
        entry[27] = 16;
    } else {
        entry[17] = 2;
        entry[24] = 0xAC;
        entry[25] = 0x44;
    }
    Bytes stsd = Words({0, 1});
    Append(stsd, Box(video ? "avc1" : "mp4a", entry));
    Bytes mdia = Box("mdhd", Words({0, 0, 0, video ? 1000U : 44100U, 0, 0}));
    Append(mdia, Box("hdlr", Words({0, 0, video ? 0x76696465U : 0x736F756EU, 0, 0, 0})));
    Append(mdia, Box("minf", Box("stbl", Box("stsd", stsd))));
    Bytes trak = Box("tkhd", Words({0, 0, 0, 1, 0, 0}));
    Append(trak, Box("mdia", mdia));
    Bytes moov = Box("trak", trak);
    Append(moov, Box("mvex", Box("trex", Words({0, 1, 1, video ? 40U : 1024U, 4, 0}))));
    Bytes result = Box("ftyp", Words({0x69736F6DU, 0, 0x69736F6DU}));
    Append(result, Box("moov", moov));
    Bytes traf = Box("tfhd", Words({0x020000, 1}));
    Append(traf, Box("tfdt", Words({1U << 24, 0, 0})));
    // İki sample; ikinci video sample'ında signed composition offset bulunur.
    Bytes trun = defaults ? Words({1, 2, 0})
                          : Words({0x01000F01, 2, 0, video ? 40U : 1024U, 4, 0, 0, video ? 40U : 1024U, 4, 0x10000,
                                   video ? 0xFFFFFFF6U : 0U});
    Append(traf, Box("trun", trun));
    auto moof = Box("moof", Box("traf", traf));
    // trun data_offset is its payload's third word, last trun in this moof.
    const auto offsetPosition = moof.size() - trun.size() + 8;
    const auto offset = static_cast<std::uint32_t>(moof.size() + 8);
    for (std::size_t i = 0; i < 4; ++i)
        moof[offsetPosition + i] = static_cast<std::uint8_t>(offset >> (24 - static_cast<int>(i) * 8));
    Append(result, moof);
    Append(result, Box("mdat", video ? Bytes{1, 2, 3, 4, 5, 6, 7, 8} : Bytes{21, 22, 23, 24, 25, 26, 27, 28}));
    return result;
}
}
