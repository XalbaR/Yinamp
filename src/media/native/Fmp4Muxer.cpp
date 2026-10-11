#include "Fmp4Muxer.h"
#include "Cancellation.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
using yinamp::media::native::CheckCancelled;

constexpr std::uint32_t FourCC(char a, char b, char c, char d) noexcept {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

std::string FourCCString(std::uint32_t value) {
    std::string out(4, ' ');
    out[0] = static_cast<char>((value >> 24U) & 0xFFU);
    out[1] = static_cast<char>((value >> 16U) & 0xFFU);
    out[2] = static_cast<char>((value >> 8U) & 0xFFU);
    out[3] = static_cast<char>(value & 0xFFU);
    return out;
}

std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t pos) {
    if (pos + 2U > data.size()) {
        throw std::runtime_error("Unexpected EOF while reading u16");
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[pos]) << 8U) |
                                      static_cast<std::uint16_t>(data[pos + 1U]));
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t pos) {
    if (pos + 4U > data.size()) {
        throw std::runtime_error("Unexpected EOF while reading u32");
    }
    return (static_cast<std::uint32_t>(data[pos]) << 24U) | (static_cast<std::uint32_t>(data[pos + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[pos + 2U]) << 8U) | static_cast<std::uint32_t>(data[pos + 3U]);
}

std::int32_t ReadI32(const std::vector<std::uint8_t>& data, std::size_t pos) {
    return static_cast<std::int32_t>(ReadU32(data, pos));
}

std::uint64_t ReadU64(const std::vector<std::uint8_t>& data, std::size_t pos) {
    const auto hi = static_cast<std::uint64_t>(ReadU32(data, pos));
    const auto lo = static_cast<std::uint64_t>(ReadU32(data, pos + 4U));
    return (hi << 32U) | lo;
}

struct Box {
    std::uint64_t start{};
    std::uint64_t size{};
    std::uint64_t headerSize{};
    std::uint32_t type{};

    [[nodiscard]] std::uint64_t payloadStart() const noexcept {
        return start + headerSize;
    }
    [[nodiscard]] std::uint64_t end() const noexcept {
        return start + size;
    }
};

Box ParseBox(const std::vector<std::uint8_t>& data, std::uint64_t pos, std::uint64_t limit) {
    if (pos + 8U > limit || limit > data.size()) {
        throw std::runtime_error("Invalid MP4 box header bounds");
    }
    const auto p = static_cast<std::size_t>(pos);
    const std::uint32_t size32 = ReadU32(data, p);
    const std::uint32_t type = ReadU32(data, p + 4U);
    std::uint64_t size = size32;
    std::uint64_t header = 8U;
    if (size32 == 1U) {
        if (pos + 16U > limit) {
            throw std::runtime_error("Invalid extended MP4 box header");
        }
        size = ReadU64(data, p + 8U);
        header = 16U;
    } else if (size32 == 0U) {
        size = limit - pos;
    }
    if (size < header || size > limit - pos) {
        throw std::runtime_error("Invalid MP4 box size for " + FourCCString(type));
    }
    return Box{pos, size, header, type};
}

template <typename Fn>
void ForEachBox(const std::vector<std::uint8_t>& data, std::uint64_t begin, std::uint64_t end, Fn&& fn) {
    std::uint64_t pos = begin;
    while (pos + 8U <= end) {
        const Box box = ParseBox(data, pos, end);
        fn(box);
        if (box.size == 0U) {
            break;
        }
        pos += box.size;
    }
}

std::vector<std::uint8_t> ReadAll(const fs::path& path, std::stop_token stop) {
    CheckCancelled(stop);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("Cannot open input: " + path.string());
    }
    const auto size = in.tellg();
    if (size < 0) {
        throw std::runtime_error("Cannot determine input size: " + path.string());
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!data.empty()) {
        for (std::size_t offset = 0; offset < data.size();) {
            CheckCancelled(stop);
            const auto count = std::min<std::size_t>(1024 * 1024, data.size() - offset);
            in.read(reinterpret_cast<char*>(data.data() + offset), static_cast<std::streamsize>(count));
            if (!in)
                throw std::runtime_error("Failed to read media input");
            offset += count;
        }
        if (!in) {
            throw std::runtime_error("Failed to read input: " + path.string());
        }
    }
    return data;
}

struct TrexDefaults {
    bool present{false};
    std::uint32_t trackId{};
    std::uint32_t sampleDescriptionIndex{1U};
    std::uint32_t duration{};
    std::uint32_t size{};
    std::uint32_t flags{};
};

struct Sample {
    std::uint64_t inputOffset{};
    std::uint32_t size{};
    std::uint32_t duration{};
    std::int64_t dts{};
    std::int64_t cto{};
    std::uint32_t flags{};

    [[nodiscard]] bool isSync() const noexcept {
        return (flags & 0x00010000U) == 0U;
    }
};

struct TrackInfo {
    std::stop_token stop;
    std::uint32_t trackId{};
    std::uint32_t handler{};
    std::uint32_t timescale{};
    std::vector<std::uint8_t> sampleEntry;
    TrexDefaults trex{};
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint16_t channels{};
    std::uint32_t sampleRate{};
    std::vector<Sample> samples;
};

std::optional<Box> FindChild(const std::vector<std::uint8_t>& data, const Box& parent, std::uint32_t type) {
    std::optional<Box> result;
    ForEachBox(data, parent.payloadStart(), parent.end(), [&](const Box& box) {
        if (!result && box.type == type) {
            result = box;
        }
    });
    return result;
}

std::optional<Box> FindPath(const std::vector<std::uint8_t>& data, Box root, const std::vector<std::uint32_t>& path) {
    for (const auto type : path) {
        const auto child = FindChild(data, root, type);
        if (!child) {
            return std::nullopt;
        }
        root = *child;
    }
    return root;
}

std::uint32_t ParseTrackId(const std::vector<std::uint8_t>& data, const Box& trak) {
    const auto tkhdOpt = FindChild(data, trak, FourCC('t', 'k', 'h', 'd'));
    if (!tkhdOpt) {
        throw std::runtime_error("trak missing tkhd");
    }
    const Box tkhd = *tkhdOpt;
    const auto base = static_cast<std::size_t>(tkhd.payloadStart());
    const std::uint8_t version = data.at(base);
    if (version == 0U) {
        return ReadU32(data, base + 12U);
    }
    if (version == 1U) {
        return ReadU32(data, base + 20U);
    }
    throw std::runtime_error("Unsupported tkhd version");
}

std::uint32_t ParseTimescale(const std::vector<std::uint8_t>& data, const Box& mdia) {
    const auto mdhdOpt = FindChild(data, mdia, FourCC('m', 'd', 'h', 'd'));
    if (!mdhdOpt) {
        throw std::runtime_error("mdia missing mdhd");
    }
    const auto base = static_cast<std::size_t>(mdhdOpt->payloadStart());
    const std::uint8_t version = data.at(base);
    if (version == 0U) {
        return ReadU32(data, base + 12U);
    }
    if (version == 1U) {
        return ReadU32(data, base + 20U);
    }
    throw std::runtime_error("Unsupported mdhd version");
}

std::uint32_t ParseHandler(const std::vector<std::uint8_t>& data, const Box& mdia) {
    const auto hdlrOpt = FindChild(data, mdia, FourCC('h', 'd', 'l', 'r'));
    if (!hdlrOpt) {
        throw std::runtime_error("mdia missing hdlr");
    }
    return ReadU32(data, static_cast<std::size_t>(hdlrOpt->payloadStart()) + 8U);
}

std::vector<std::uint8_t> ParseSampleEntry(const std::vector<std::uint8_t>& data, const Box& mdia) {
    const auto stsdOpt =
        FindPath(data, mdia, {FourCC('m', 'i', 'n', 'f'), FourCC('s', 't', 'b', 'l'), FourCC('s', 't', 's', 'd')});
    if (!stsdOpt) {
        throw std::runtime_error("mdia missing stsd");
    }
    const std::uint64_t entryStart = stsdOpt->payloadStart() + 8U; // Fullbox ve entry_count alanları.
    if (entryStart + 8U > stsdOpt->end()) {
        throw std::runtime_error("stsd has no sample entry");
    }
    const Box entry = ParseBox(data, entryStart, stsdOpt->end());
    const auto begin = static_cast<std::size_t>(entry.start);
    const auto finish = static_cast<std::size_t>(entry.end());
    return std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(begin),
                                     data.begin() + static_cast<std::ptrdiff_t>(finish));
}

TrexDefaults ParseTrex(const std::vector<std::uint8_t>& data, const Box& moov, std::uint32_t trackId) {
    TrexDefaults out;
    const auto mvexOpt = FindChild(data, moov, FourCC('m', 'v', 'e', 'x'));
    if (!mvexOpt) {
        return out;
    }
    ForEachBox(data, mvexOpt->payloadStart(), mvexOpt->end(), [&](const Box& box) {
        if (box.type != FourCC('t', 'r', 'e', 'x')) {
            return;
        }
        const auto p = static_cast<std::size_t>(box.payloadStart()) + 4U;
        const std::uint32_t id = ReadU32(data, p);
        if (id != trackId) {
            return;
        }
        out.present = true;
        out.trackId = id;
        out.sampleDescriptionIndex = ReadU32(data, p + 4U);
        out.duration = ReadU32(data, p + 8U);
        out.size = ReadU32(data, p + 12U);
        out.flags = ReadU32(data, p + 16U);
    });
    return out;
}

TrackInfo ParseInitTrack(const std::vector<std::uint8_t>& data, std::uint32_t wantedHandler) {
    std::optional<Box> moov;
    ForEachBox(data, 0U, data.size(), [&](const Box& box) {
        if (!moov && box.type == FourCC('m', 'o', 'o', 'v')) {
            moov = box;
        }
    });
    if (!moov) {
        throw std::runtime_error("Input missing moov");
    }

    std::optional<TrackInfo> found;
    ForEachBox(data, moov->payloadStart(), moov->end(), [&](const Box& box) {
        if (found || box.type != FourCC('t', 'r', 'a', 'k')) {
            return;
        }
        const auto mdiaOpt = FindChild(data, box, FourCC('m', 'd', 'i', 'a'));
        if (!mdiaOpt) {
            return;
        }
        const std::uint32_t handler = ParseHandler(data, *mdiaOpt);
        if (handler != wantedHandler) {
            return;
        }
        TrackInfo info;
        info.trackId = ParseTrackId(data, box);
        info.handler = handler;
        info.timescale = ParseTimescale(data, *mdiaOpt);
        info.sampleEntry = ParseSampleEntry(data, *mdiaOpt);
        info.trex = ParseTrex(data, *moov, info.trackId);

        if (info.sampleEntry.size() < 36U) {
            throw std::runtime_error("Sample entry too small");
        }
        const std::uint32_t entryType = ReadU32(info.sampleEntry, 4U);
        if (wantedHandler == FourCC('v', 'i', 'd', 'e')) {
            if (entryType != FourCC('a', 'v', 'c', '1')) {
                throw std::runtime_error("Expected avc1 video sample entry, got " + FourCCString(entryType));
            }
            info.width = ReadU16(info.sampleEntry, 32U);
            info.height = ReadU16(info.sampleEntry, 34U);
        } else {
            if (entryType != FourCC('m', 'p', '4', 'a')) {
                throw std::runtime_error("Expected mp4a audio sample entry, got " + FourCCString(entryType));
            }
            info.channels = ReadU16(info.sampleEntry, 24U);
            info.sampleRate = ReadU32(info.sampleEntry, 32U) >> 16U;
        }
        found = std::move(info);
    });

    if (!found) {
        throw std::runtime_error("Requested track not found in moov");
    }
    return *found;
}

struct Tfhd {
    std::uint32_t trackId{};
    std::uint64_t baseDataOffset{};
    bool hasBaseDataOffset{false};
    bool defaultBaseIsMoof{false};
    std::uint32_t sampleDescriptionIndex{};
    std::uint32_t defaultDuration{};
    std::uint32_t defaultSize{};
    std::uint32_t defaultFlags{};
    bool hasDefaultDuration{false};
    bool hasDefaultSize{false};
    bool hasDefaultFlags{false};
};

Tfhd ParseTfhd(const std::vector<std::uint8_t>& data, const Box& box) {
    const auto base = static_cast<std::size_t>(box.payloadStart());
    const std::uint32_t vf = ReadU32(data, base);
    const std::uint32_t flags = vf & 0x00FFFFFFU;
    std::size_t p = base + 4U;
    Tfhd out;
    out.trackId = ReadU32(data, p);
    p += 4U;
    if ((flags & 0x000001U) != 0U) {
        out.baseDataOffset = ReadU64(data, p);
        out.hasBaseDataOffset = true;
        p += 8U;
    }
    if ((flags & 0x000002U) != 0U) {
        out.sampleDescriptionIndex = ReadU32(data, p);
        p += 4U;
    }
    if ((flags & 0x000008U) != 0U) {
        out.defaultDuration = ReadU32(data, p);
        out.hasDefaultDuration = true;
        p += 4U;
    }
    if ((flags & 0x000010U) != 0U) {
        out.defaultSize = ReadU32(data, p);
        out.hasDefaultSize = true;
        p += 4U;
    }
    if ((flags & 0x000020U) != 0U) {
        out.defaultFlags = ReadU32(data, p);
        out.hasDefaultFlags = true;
        p += 4U;
    }
    out.defaultBaseIsMoof = (flags & 0x020000U) != 0U;
    return out;
}

std::uint64_t ParseTfdt(const std::vector<std::uint8_t>& data, const Box& box) {
    const auto base = static_cast<std::size_t>(box.payloadStart());
    const std::uint8_t version = data.at(base);
    if (version == 1U) {
        return ReadU64(data, base + 4U);
    }
    return ReadU32(data, base + 4U);
}

std::uint64_t FindFollowingMdatPayload(const std::vector<std::uint8_t>& data, const Box& moof) {
    std::uint64_t pos = moof.end();
    while (pos + 8U <= data.size()) {
        const Box box = ParseBox(data, pos, data.size());
        if (box.type == FourCC('m', 'd', 'a', 't')) {
            return box.payloadStart();
        }
        if (box.type == FourCC('m', 'o', 'o', 'f')) {
            break;
        }
        pos = box.end();
    }
    throw std::runtime_error("moof not followed by mdat");
}

void ParseFragments(const std::vector<std::uint8_t>& data, TrackInfo& track) {
    ForEachBox(data, 0U, data.size(), [&](const Box& top) {
        if (top.type != FourCC('m', 'o', 'o', 'f')) {
            return;
        }
        const std::uint64_t fallbackMdat = FindFollowingMdatPayload(data, top);
        ForEachBox(data, top.payloadStart(), top.end(), [&](const Box& child) {
            if (child.type != FourCC('t', 'r', 'a', 'f')) {
                return;
            }

            std::optional<Tfhd> tfhd;
            std::optional<std::uint64_t> tfdt;
            std::vector<Box> truns;
            ForEachBox(data, child.payloadStart(), child.end(), [&](const Box& b) {
                if (b.type == FourCC('t', 'f', 'h', 'd')) {
                    tfhd = ParseTfhd(data, b);
                } else if (b.type == FourCC('t', 'f', 'd', 't')) {
                    tfdt = ParseTfdt(data, b);
                } else if (b.type == FourCC('t', 'r', 'u', 'n')) {
                    truns.push_back(b);
                }
            });
            if (!tfhd || tfhd->trackId != track.trackId) {
                return;
            }

            std::uint64_t decodeTime = tfdt.value_or(
                track.samples.empty()
                    ? 0U
                    : static_cast<std::uint64_t>(track.samples.back().dts + track.samples.back().duration));
            const std::uint64_t baseData =
                tfhd->hasBaseDataOffset ? tfhd->baseDataOffset : (tfhd->defaultBaseIsMoof ? top.start : top.start);
            std::optional<std::uint64_t> previousRunEnd;

            for (const Box& trun : truns) {
                const auto base = static_cast<std::size_t>(trun.payloadStart());
                const std::uint32_t vf = ReadU32(data, base);
                const std::uint8_t version = static_cast<std::uint8_t>((vf >> 24U) & 0xFFU);
                const std::uint32_t flags = vf & 0x00FFFFFFU;
                std::size_t p = base + 4U;
                const std::uint32_t sampleCount = ReadU32(data, p);
                if (sampleCount > data.size())
                    throw std::runtime_error("Unreasonable fragment sample count");
                p += 4U;

                std::optional<std::int32_t> dataOffset;
                std::optional<std::uint32_t> firstSampleFlags;
                if ((flags & 0x000001U) != 0U) {
                    dataOffset = ReadI32(data, p);
                    p += 4U;
                }
                if ((flags & 0x000004U) != 0U) {
                    firstSampleFlags = ReadU32(data, p);
                    p += 4U;
                }

                std::uint64_t dataCursor{};
                if (dataOffset) {
                    const std::int64_t signedStart = static_cast<std::int64_t>(baseData) + *dataOffset;
                    if (signedStart < 0) {
                        throw std::runtime_error("Negative trun data offset");
                    }
                    dataCursor = static_cast<std::uint64_t>(signedStart);
                } else if (previousRunEnd) {
                    dataCursor = *previousRunEnd;
                } else {
                    dataCursor = fallbackMdat;
                }

                for (std::uint32_t i = 0; i < sampleCount; ++i) {
                    CheckCancelled(track.stop);
                    const std::uint32_t duration =
                        (flags & 0x000100U) != 0U
                            ? ReadU32(data, p)
                            : (tfhd->hasDefaultDuration ? tfhd->defaultDuration : track.trex.duration);
                    if ((flags & 0x000100U) != 0U) {
                        p += 4U;
                    }

                    const std::uint32_t size = (flags & 0x000200U) != 0U
                                                   ? ReadU32(data, p)
                                                   : (tfhd->hasDefaultSize ? tfhd->defaultSize : track.trex.size);
                    if ((flags & 0x000200U) != 0U) {
                        p += 4U;
                    }

                    std::uint32_t sampleFlags{};
                    if ((flags & 0x000400U) != 0U) {
                        sampleFlags = ReadU32(data, p);
                        p += 4U;
                    } else if (i == 0U && firstSampleFlags) {
                        sampleFlags = *firstSampleFlags;
                    } else if (tfhd->hasDefaultFlags) {
                        sampleFlags = tfhd->defaultFlags;
                    } else {
                        sampleFlags = track.trex.flags;
                    }

                    std::int64_t cto = 0;
                    if ((flags & 0x000800U) != 0U) {
                        if (version == 1U) {
                            cto = ReadI32(data, p);
                        } else {
                            cto = static_cast<std::int64_t>(ReadU32(data, p));
                        }
                        p += 4U;
                    }

                    if (duration == 0U || size == 0U) {
                        throw std::runtime_error("Fragment sample has zero duration/size; unsupported defaults");
                    }
                    if (dataCursor > data.size() || size > data.size() - dataCursor) {
                        throw std::runtime_error("Fragment sample points outside input file");
                    }

                    track.samples.push_back(
                        Sample{dataCursor, size, duration, static_cast<std::int64_t>(decodeTime), cto, sampleFlags});
                    dataCursor += size;
                    decodeTime += duration;
                }
                previousRunEnd = dataCursor;
            }
        });
    });

    if (track.samples.empty()) {
        throw std::runtime_error("Fragment-aware parser found zero samples");
    }
}

bool HasTopLevelBox(const std::vector<std::uint8_t>& data, std::uint32_t type) {
    bool found = false;
    ForEachBox(data, 0U, data.size(), [&](const Box& box) {
        if (box.type == type) {
            found = true;
        }
    });
    return found;
}

std::optional<Box> FindTrackStbl(const std::vector<std::uint8_t>& data, std::uint32_t wantedHandler,
                                 std::uint32_t wantedTrackId) {
    std::optional<Box> moov;
    ForEachBox(data, 0U, data.size(), [&](const Box& box) {
        if (!moov && box.type == FourCC('m', 'o', 'o', 'v')) {
            moov = box;
        }
    });
    if (!moov) {
        return std::nullopt;
    }

    std::optional<Box> stbl;
    ForEachBox(data, moov->payloadStart(), moov->end(), [&](const Box& box) {
        if (stbl || box.type != FourCC('t', 'r', 'a', 'k')) {
            return;
        }
        const auto mdia = FindChild(data, box, FourCC('m', 'd', 'i', 'a'));
        if (!mdia) {
            return;
        }
        if (ParseHandler(data, *mdia) != wantedHandler || ParseTrackId(data, box) != wantedTrackId) {
            return;
        }
        stbl = FindPath(data, *mdia, {FourCC('m', 'i', 'n', 'f'), FourCC('s', 't', 'b', 'l')});
    });
    return stbl;
}

struct StscEntry {
    std::uint32_t firstChunk{};
    std::uint32_t samplesPerChunk{};
};

void ParseFlatSamples(const std::vector<std::uint8_t>& data, TrackInfo& track) {
    const auto stblOpt = FindTrackStbl(data, track.handler, track.trackId);
    if (!stblOpt) {
        throw std::runtime_error("Flat MP4 track is missing stbl");
    }
    const Box stbl = *stblOpt;

    const auto stszOpt = FindChild(data, stbl, FourCC('s', 't', 's', 'z'));
    const auto sttsOpt = FindChild(data, stbl, FourCC('s', 't', 't', 's'));
    const auto stscOpt = FindChild(data, stbl, FourCC('s', 't', 's', 'c'));
    auto stcoOpt = FindChild(data, stbl, FourCC('s', 't', 'c', 'o'));
    const auto co64Opt = FindChild(data, stbl, FourCC('c', 'o', '6', '4'));
    if (!stszOpt || !sttsOpt || !stscOpt || (!stcoOpt && !co64Opt)) {
        throw std::runtime_error("Flat MP4 track is missing required sample tables");
    }

    std::vector<std::uint32_t> sizes;
    {
        std::size_t p = static_cast<std::size_t>(stszOpt->payloadStart()) + 4U;
        const std::uint32_t fixedSize = ReadU32(data, p);
        p += 4U;
        const std::uint32_t count = ReadU32(data, p);
        p += 4U;
        if (count > data.size())
            throw std::runtime_error("Unreasonable flat sample count");
        sizes.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CheckCancelled(track.stop);
            sizes.push_back(fixedSize != 0U ? fixedSize : ReadU32(data, p));
            if (fixedSize == 0U) {
                p += 4U;
            }
        }
    }
    if (sizes.empty()) {
        throw std::runtime_error("Flat MP4 track has zero samples");
    }

    std::vector<std::uint32_t> durations;
    durations.reserve(sizes.size());
    {
        std::size_t p = static_cast<std::size_t>(sttsOpt->payloadStart()) + 4U;
        const std::uint32_t entryCount = ReadU32(data, p);
        p += 4U;
        for (std::uint32_t i = 0; i < entryCount; ++i) {
            const std::uint32_t count = ReadU32(data, p);
            const std::uint32_t delta = ReadU32(data, p + 4U);
            p += 8U;
            if (delta == 0U) {
                throw std::runtime_error("Flat MP4 stts contains zero sample duration");
            }
            if (durations.size() + count > sizes.size()) {
                throw std::runtime_error("Flat MP4 stts sample count exceeds stsz");
            }
            durations.insert(durations.end(), count, delta);
        }
    }
    if (durations.size() != sizes.size()) {
        throw std::runtime_error("Flat MP4 stts/stsz sample count mismatch");
    }

    std::vector<std::int64_t> compositionOffsets(sizes.size(), 0);
    if (const auto cttsOpt = FindChild(data, stbl, FourCC('c', 't', 't', 's'))) {
        std::size_t p = static_cast<std::size_t>(cttsOpt->payloadStart());
        const std::uint32_t vf = ReadU32(data, p);
        const std::uint8_t version = static_cast<std::uint8_t>((vf >> 24U) & 0xFFU);
        p += 4U;
        const std::uint32_t entryCount = ReadU32(data, p);
        p += 4U;
        std::size_t sampleIndex = 0U;
        for (std::uint32_t i = 0; i < entryCount; ++i) {
            const std::uint32_t count = ReadU32(data, p);
            const std::int64_t offset = version == 1U ? static_cast<std::int64_t>(ReadI32(data, p + 4U))
                                                      : static_cast<std::int64_t>(ReadU32(data, p + 4U));
            p += 8U;
            if (sampleIndex + count > compositionOffsets.size()) {
                throw std::runtime_error("Flat MP4 ctts sample count exceeds stsz");
            }
            std::fill_n(compositionOffsets.begin() + static_cast<std::ptrdiff_t>(sampleIndex), count, offset);
            sampleIndex += count;
        }
        if (sampleIndex != compositionOffsets.size()) {
            throw std::runtime_error("Flat MP4 ctts/stsz sample count mismatch");
        }
    }

    std::vector<bool> syncSamples(sizes.size(), true);
    if (const auto stssOpt = FindChild(data, stbl, FourCC('s', 't', 's', 's'))) {
        std::fill(syncSamples.begin(), syncSamples.end(), false);
        std::size_t p = static_cast<std::size_t>(stssOpt->payloadStart()) + 4U;
        const std::uint32_t count = ReadU32(data, p);
        p += 4U;
        for (std::uint32_t i = 0; i < count; ++i) {
            CheckCancelled(track.stop);
            const std::uint32_t oneBased = ReadU32(data, p);
            p += 4U;
            if (oneBased == 0U || oneBased > syncSamples.size()) {
                throw std::runtime_error("Flat MP4 stss contains invalid sample number");
            }
            syncSamples[oneBased - 1U] = true;
        }
    }

    std::vector<StscEntry> stsc;
    {
        std::size_t p = static_cast<std::size_t>(stscOpt->payloadStart()) + 4U;
        const std::uint32_t count = ReadU32(data, p);
        p += 4U;
        stsc.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CheckCancelled(track.stop);
            const std::uint32_t firstChunk = ReadU32(data, p);
            const std::uint32_t samplesPerChunk = ReadU32(data, p + 4U);
            const std::uint32_t sampleDescriptionIndex = ReadU32(data, p + 8U);
            p += 12U;
            if (firstChunk == 0U || samplesPerChunk == 0U || sampleDescriptionIndex == 0U) {
                throw std::runtime_error("Flat MP4 stsc contains invalid entry");
            }
            stsc.push_back({firstChunk, samplesPerChunk});
        }
    }
    if (stsc.empty() || stsc.front().firstChunk != 1U) {
        throw std::runtime_error("Flat MP4 stsc does not start at chunk 1");
    }

    std::vector<std::uint64_t> chunkOffsets;
    if (stcoOpt) {
        std::size_t p = static_cast<std::size_t>(stcoOpt->payloadStart()) + 4U;
        const std::uint32_t count = ReadU32(data, p);
        p += 4U;
        chunkOffsets.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CheckCancelled(track.stop);
            chunkOffsets.push_back(ReadU32(data, p));
            p += 4U;
        }
    } else {
        std::size_t p = static_cast<std::size_t>(co64Opt->payloadStart()) + 4U;
        const std::uint32_t count = ReadU32(data, p);
        p += 4U;
        chunkOffsets.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            CheckCancelled(track.stop);
            chunkOffsets.push_back(ReadU64(data, p));
            p += 8U;
        }
    }
    if (chunkOffsets.empty()) {
        throw std::runtime_error("Flat MP4 has zero chunks");
    }

    track.samples.clear();
    track.samples.reserve(sizes.size());
    std::size_t sampleIndex = 0U;
    std::size_t stscIndex = 0U;
    std::int64_t dts = 0;
    for (std::size_t chunkIndex = 0U; chunkIndex < chunkOffsets.size() && sampleIndex < sizes.size(); ++chunkIndex) {
        const std::uint32_t oneBasedChunk = static_cast<std::uint32_t>(chunkIndex + 1U);
        while (stscIndex + 1U < stsc.size() && stsc[stscIndex + 1U].firstChunk <= oneBasedChunk) {
            ++stscIndex;
        }

        std::uint64_t sampleOffset = chunkOffsets[chunkIndex];
        for (std::uint32_t inChunk = 0U; inChunk < stsc[stscIndex].samplesPerChunk && sampleIndex < sizes.size();
             ++inChunk, ++sampleIndex) {
            const std::uint32_t size = sizes[sampleIndex];
            CheckCancelled(track.stop);
            if (sampleOffset > data.size() || size > data.size() - sampleOffset) {
                throw std::runtime_error("Flat MP4 sample points outside input file");
            }
            const std::uint32_t flags = syncSamples[sampleIndex] ? 0U : 0x00010000U;
            track.samples.push_back(
                Sample{sampleOffset, size, durations[sampleIndex], dts, compositionOffsets[sampleIndex], flags});
            sampleOffset += size;
            dts += durations[sampleIndex];
        }
    }

    if (sampleIndex != sizes.size() || track.samples.size() != sizes.size()) {
        throw std::runtime_error("Flat MP4 chunk tables did not map every sample");
    }
}

void ParseSamplesAuto(const std::vector<std::uint8_t>& data, TrackInfo& track) {
    if (HasTopLevelBox(data, FourCC('m', 'o', 'o', 'f'))) {
        ParseFragments(data, track);
    } else {
        ParseFlatSamples(data, track);
    }
}

class Writer {
  public:
    std::vector<std::uint8_t> bytes;

    void u8(std::uint8_t v) {
        bytes.push_back(v);
    }
    void u16(std::uint16_t v) {
        bytes.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    }
    void u24(std::uint32_t v) {
        bytes.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    }
    void u32(std::uint32_t v) {
        bytes.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    }
    void i32(std::int32_t v) {
        u32(static_cast<std::uint32_t>(v));
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>((v >> 32U) & 0xFFFFFFFFULL));
        u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFULL));
    }
    void raw(const std::vector<std::uint8_t>& v) {
        bytes.insert(bytes.end(), v.begin(), v.end());
    }
    void raw(const std::uint8_t* p, std::size_t n) {
        bytes.insert(bytes.end(), p, p + n);
    }
    void zeros(std::size_t n) {
        bytes.insert(bytes.end(), n, 0U);
    }

    template <typename Fn> void box(std::uint32_t type, Fn&& fn) {
        const std::size_t start = bytes.size();
        u32(0U);
        u32(type);
        fn();
        const std::size_t size = bytes.size() - start;
        if (size > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Metadata box too large");
        }
        const std::uint32_t s = static_cast<std::uint32_t>(size);
        bytes[start] = static_cast<std::uint8_t>((s >> 24U) & 0xFFU);
        bytes[start + 1U] = static_cast<std::uint8_t>((s >> 16U) & 0xFFU);
        bytes[start + 2U] = static_cast<std::uint8_t>((s >> 8U) & 0xFFU);
        bytes[start + 3U] = static_cast<std::uint8_t>(s & 0xFFU);
    }

    void fullbox(std::uint8_t version, std::uint32_t flags) {
        u8(version);
        u24(flags);
    }
};

std::uint64_t DecodeDuration(const TrackInfo& t) {
    std::uint64_t total = 0U;
    for (const auto& s : t.samples) {
        CheckCancelled(t.stop);
        total += s.duration;
    }
    return total;
}

std::uint64_t PresentationDuration(const TrackInfo& t) {
    std::int64_t maxEnd = 0;
    for (const auto& s : t.samples) {
        CheckCancelled(t.stop);
        maxEnd = std::max(maxEnd, s.dts + s.cto + static_cast<std::int64_t>(s.duration));
    }
    return maxEnd > 0 ? static_cast<std::uint64_t>(maxEnd) : DecodeDuration(t);
}

std::uint64_t RescaleDuration(std::uint64_t value, std::uint32_t src, std::uint32_t dst) {
    if (src == 0U) {
        throw std::runtime_error("Zero timescale");
    }
    const long double scaled =
        static_cast<long double>(value) * static_cast<long double>(dst) / static_cast<long double>(src);
    return static_cast<std::uint64_t>(scaled + 0.5L);
}

void WriteMatrix(Writer& w) {
    w.u32(0x00010000U);
    w.u32(0U);
    w.u32(0U);
    w.u32(0U);
    w.u32(0x00010000U);
    w.u32(0U);
    w.u32(0U);
    w.u32(0U);
    w.u32(0x40000000U);
}

void WriteStts(Writer& w, const TrackInfo& t) {
    w.box(FourCC('s', 't', 't', 's'), [&] {
        w.fullbox(0U, 0U);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
        for (const auto& s : t.samples) {
            CheckCancelled(t.stop);
            if (!runs.empty() && runs.back().second == s.duration) {
                ++runs.back().first;
            } else {
                runs.emplace_back(1U, s.duration);
            }
        }
        w.u32(static_cast<std::uint32_t>(runs.size()));
        for (const auto& [count, duration] : runs) {
            w.u32(count);
            w.u32(duration);
        }
    });
}

void WriteCtts(Writer& w, const TrackInfo& t) {
    const bool any = std::any_of(t.samples.begin(), t.samples.end(), [](const Sample& s) { return s.cto != 0; });
    if (!any) {
        return;
    }
    const bool negative = std::any_of(t.samples.begin(), t.samples.end(), [](const Sample& s) { return s.cto < 0; });
    w.box(FourCC('c', 't', 't', 's'), [&] {
        w.fullbox(negative ? 1U : 0U, 0U);
        std::vector<std::pair<std::uint32_t, std::int64_t>> runs;
        for (const auto& s : t.samples) {
            CheckCancelled(t.stop);
            if (!runs.empty() && runs.back().second == s.cto) {
                ++runs.back().first;
            } else {
                runs.emplace_back(1U, s.cto);
            }
        }
        w.u32(static_cast<std::uint32_t>(runs.size()));
        for (const auto& [count, cto] : runs) {
            w.u32(count);
            if (negative) {
                if (cto < std::numeric_limits<std::int32_t>::min() || cto > std::numeric_limits<std::int32_t>::max()) {
                    throw std::runtime_error("Composition offset outside int32");
                }
                w.i32(static_cast<std::int32_t>(cto));
            } else {
                if (cto < 0 || static_cast<std::uint64_t>(cto) > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::runtime_error("Composition offset outside uint32");
                }
                w.u32(static_cast<std::uint32_t>(cto));
            }
        }
    });
}

void WriteStss(Writer& w, const TrackInfo& t) {
    std::vector<std::uint32_t> sync;
    for (std::size_t i = 0; i < t.samples.size(); ++i) {
        if (t.samples[i].isSync()) {
            sync.push_back(static_cast<std::uint32_t>(i + 1U));
        }
    }
    if (sync.empty() || sync.size() == t.samples.size()) {
        return;
    }
    w.box(FourCC('s', 't', 's', 's'), [&] {
        w.fullbox(0U, 0U);
        w.u32(static_cast<std::uint32_t>(sync.size()));
        for (const auto n : sync) {
            w.u32(n);
        }
    });
}

void WriteTrack(Writer& w, const TrackInfo& t, std::uint32_t outputTrackId, std::uint32_t movieTimescale,
                std::uint64_t chunkOffset) {
    const bool video = t.handler == FourCC('v', 'i', 'd', 'e');
    const std::uint64_t trackMovieDuration = RescaleDuration(PresentationDuration(t), t.timescale, movieTimescale);

    w.box(FourCC('t', 'r', 'a', 'k'), [&] {
        w.box(FourCC('t', 'k', 'h', 'd'), [&] {
            w.fullbox(0U, 0x000007U);
            w.u32(0U);
            w.u32(0U);
            w.u32(outputTrackId);
            w.u32(0U);
            w.u32(static_cast<std::uint32_t>(std::min<std::uint64_t>(trackMovieDuration, 0xFFFFFFFFULL)));
            w.u32(0U);
            w.u32(0U);
            w.u16(0U);
            w.u16(0U);
            w.u16(video ? 0U : 0x0100U);
            w.u16(0U);
            WriteMatrix(w);
            w.u32(video ? (static_cast<std::uint32_t>(t.width) << 16U) : 0U);
            w.u32(video ? (static_cast<std::uint32_t>(t.height) << 16U) : 0U);
        });
        w.box(FourCC('m', 'd', 'i', 'a'), [&] {
            w.box(FourCC('m', 'd', 'h', 'd'), [&] {
                w.fullbox(0U, 0U);
                w.u32(0U);
                w.u32(0U);
                w.u32(t.timescale);
                const auto dur = DecodeDuration(t);
                w.u32(static_cast<std::uint32_t>(std::min<std::uint64_t>(dur, 0xFFFFFFFFULL)));
                w.u16(0x55C4U); // und
                w.u16(0U);
            });
            w.box(FourCC('h', 'd', 'l', 'r'), [&] {
                w.fullbox(0U, 0U);
                w.u32(0U);
                w.u32(t.handler);
                w.u32(0U);
                w.u32(0U);
                w.u32(0U);
                const std::string name = video ? "VideoHandler" : "SoundHandler";
                w.raw(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());
                w.u8(0U);
            });
            w.box(FourCC('m', 'i', 'n', 'f'), [&] {
                if (video) {
                    w.box(FourCC('v', 'm', 'h', 'd'), [&] {
                        w.fullbox(0U, 1U);
                        w.u16(0U);
                        w.u16(0U);
                        w.u16(0U);
                        w.u16(0U);
                    });
                } else {
                    w.box(FourCC('s', 'm', 'h', 'd'), [&] {
                        w.fullbox(0U, 0U);
                        w.u16(0U);
                        w.u16(0U);
                    });
                }
                w.box(FourCC('d', 'i', 'n', 'f'), [&] {
                    w.box(FourCC('d', 'r', 'e', 'f'), [&] {
                        w.fullbox(0U, 0U);
                        w.u32(1U);
                        w.box(FourCC('u', 'r', 'l', ' '), [&] { w.fullbox(0U, 1U); });
                    });
                });
                w.box(FourCC('s', 't', 'b', 'l'), [&] {
                    w.box(FourCC('s', 't', 's', 'd'), [&] {
                        w.fullbox(0U, 0U);
                        w.u32(1U);
                        w.raw(t.sampleEntry);
                    });
                    WriteStts(w, t);
                    if (video) {
                        WriteCtts(w, t);
                    }
                    w.box(FourCC('s', 't', 's', 'c'), [&] {
                        w.fullbox(0U, 0U);
                        w.u32(1U);
                        w.u32(1U);
                        w.u32(static_cast<std::uint32_t>(t.samples.size()));
                        w.u32(1U);
                    });
                    w.box(FourCC('s', 't', 's', 'z'), [&] {
                        w.fullbox(0U, 0U);
                        w.u32(0U);
                        w.u32(static_cast<std::uint32_t>(t.samples.size()));
                        for (const auto& s : t.samples) {
                            CheckCancelled(t.stop);
                            w.u32(s.size);
                        }
                    });
                    if (chunkOffset <= 0xFFFFFFFFULL) {
                        w.box(FourCC('s', 't', 'c', 'o'), [&] {
                            w.fullbox(0U, 0U);
                            w.u32(1U);
                            w.u32(static_cast<std::uint32_t>(chunkOffset));
                        });
                    } else {
                        w.box(FourCC('c', 'o', '6', '4'), [&] {
                            w.fullbox(0U, 0U);
                            w.u32(1U);
                            w.u64(chunkOffset);
                        });
                    }
                    if (video) {
                        WriteStss(w, t);
                    }
                });
            });
        });
    });
}

std::vector<std::uint8_t> MakeFtyp() {
    Writer w;
    w.box(FourCC('f', 't', 'y', 'p'), [&] {
        w.u32(FourCC('i', 's', 'o', 'm'));
        w.u32(0x00000200U);
        w.u32(FourCC('i', 's', 'o', 'm'));
        w.u32(FourCC('i', 's', 'o', '2'));
        w.u32(FourCC('a', 'v', 'c', '1'));
        w.u32(FourCC('m', 'p', '4', '1'));
    });
    return w.bytes;
}

std::vector<std::uint8_t> MakeSingleTrackFtyp(bool video) {
    Writer w;
    w.box(FourCC('f', 't', 'y', 'p'), [&] {
        w.u32(FourCC('i', 's', 'o', 'm'));
        w.u32(0x00000200U);
        w.u32(FourCC('i', 's', 'o', 'm'));
        w.u32(FourCC('i', 's', 'o', '2'));
        if (video) {
            w.u32(FourCC('a', 'v', 'c', '1'));
        }
        w.u32(FourCC('m', 'p', '4', '1'));
    });
    return w.bytes;
}

std::vector<std::uint8_t> MakeMoov(const TrackInfo& video, const TrackInfo& audio, std::uint64_t videoChunkOffset,
                                   std::uint64_t audioChunkOffset) {
    constexpr std::uint32_t movieTimescale = 1000U;
    const auto movieDuration = std::max(RescaleDuration(PresentationDuration(video), video.timescale, movieTimescale),
                                        RescaleDuration(PresentationDuration(audio), audio.timescale, movieTimescale));

    Writer w;
    w.box(FourCC('m', 'o', 'o', 'v'), [&] {
        w.box(FourCC('m', 'v', 'h', 'd'), [&] {
            w.fullbox(0U, 0U);
            w.u32(0U);
            w.u32(0U);
            w.u32(movieTimescale);
            w.u32(static_cast<std::uint32_t>(std::min<std::uint64_t>(movieDuration, 0xFFFFFFFFULL)));
            w.u32(0x00010000U);
            w.u16(0x0100U);
            w.u16(0U);
            w.u32(0U);
            w.u32(0U);
            WriteMatrix(w);
            for (int i = 0; i < 6; ++i) {
                w.u32(0U);
            }
            w.u32(3U);
        });
        WriteTrack(w, video, 1U, movieTimescale, videoChunkOffset);
        WriteTrack(w, audio, 2U, movieTimescale, audioChunkOffset);
    });
    return w.bytes;
}

std::vector<std::uint8_t> MakeSingleTrackMoov(const TrackInfo& track, std::uint64_t chunkOffset) {
    constexpr std::uint32_t movieTimescale = 1000U;
    const auto movieDuration = RescaleDuration(PresentationDuration(track), track.timescale, movieTimescale);

    Writer w;
    w.box(FourCC('m', 'o', 'o', 'v'), [&] {
        w.box(FourCC('m', 'v', 'h', 'd'), [&] {
            w.fullbox(0U, 0U);
            w.u32(0U);
            w.u32(0U);
            w.u32(movieTimescale);
            w.u32(static_cast<std::uint32_t>(std::min<std::uint64_t>(movieDuration, 0xFFFFFFFFULL)));
            w.u32(0x00010000U);
            w.u16(0x0100U);
            w.u16(0U);
            w.u32(0U);
            w.u32(0U);
            WriteMatrix(w);
            for (int i = 0; i < 6; ++i) {
                w.u32(0U);
            }
            w.u32(2U);
        });
        WriteTrack(w, track, 1U, movieTimescale, chunkOffset);
    });
    return w.bytes;
}

void WriteU32BE(std::ofstream& out, std::uint32_t v) {
    const std::array<char, 4> b{static_cast<char>((v >> 24U) & 0xFFU), static_cast<char>((v >> 16U) & 0xFFU),
                                static_cast<char>((v >> 8U) & 0xFFU), static_cast<char>(v & 0xFFU)};
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}

void WriteU64BE(std::ofstream& out, std::uint64_t v) {
    WriteU32BE(out, static_cast<std::uint32_t>((v >> 32U) & 0xFFFFFFFFULL));
    WriteU32BE(out, static_cast<std::uint32_t>(v & 0xFFFFFFFFULL));
}

std::uint64_t SumBytes(const TrackInfo& t) {
    std::uint64_t total = 0U;
    for (const auto& s : t.samples) {
        CheckCancelled(t.stop);
        total += s.size;
    }
    return total;
}

void CopySamples(std::ofstream& out, const std::vector<std::uint8_t>& input, const TrackInfo& track,
                 std::uint64_t& copied) {

    for (const auto& s : track.samples) {
        CheckCancelled(track.stop);
        const auto start = static_cast<std::size_t>(s.inputOffset);
        out.write(reinterpret_cast<const char*>(input.data() + start), static_cast<std::streamsize>(s.size));
        if (!out) {
            throw std::runtime_error("Output write failed while copying samples");
        }
        copied += s.size;
    }
}

void WriteFlatMp4(const fs::path& outputPath, const std::vector<std::uint8_t>& videoData, const TrackInfo& video,
                  const std::vector<std::uint8_t>& audioData, const TrackInfo& audio) {
    const auto ftyp = MakeFtyp();
    const std::uint64_t videoBytes = SumBytes(video);
    const std::uint64_t audioBytes = SumBytes(audio);
    const std::uint64_t mdatPayload = videoBytes + audioBytes;
    const std::uint64_t mdatStart = ftyp.size();
    const std::uint64_t mdatHeader = 16U;
    const std::uint64_t videoChunkOffset = mdatStart + mdatHeader;
    const std::uint64_t audioChunkOffset = videoChunkOffset + videoBytes;
    const auto moov = MakeMoov(video, audio, videoChunkOffset, audioChunkOffset);

    fs::create_directories(outputPath.parent_path().empty() ? fs::path(".") : outputPath.parent_path());
    const fs::path part = outputPath.wstring() + L".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Cannot open output part file");
    }
    out.write(reinterpret_cast<const char*>(ftyp.data()), static_cast<std::streamsize>(ftyp.size()));
    WriteU32BE(out, 1U);
    WriteU32BE(out, FourCC('m', 'd', 'a', 't'));
    WriteU64BE(out, 16U + mdatPayload);

    std::uint64_t copied = 0U;

    CopySamples(out, videoData, video, copied);
    CopySamples(out, audioData, audio, copied);
    out.write(reinterpret_cast<const char*>(moov.data()), static_cast<std::streamsize>(moov.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("Failed finalizing output file");
    }

    std::error_code ec;
    fs::remove(outputPath, ec);
    ec.clear();
    fs::rename(part, outputPath, ec);
    if (ec) {
        throw std::runtime_error("Cannot rename .part to final output: " + ec.message());
    }
}

void WriteSingleTrackMp4(const fs::path& outputPath, const std::vector<std::uint8_t>& inputData,
                         const TrackInfo& track) {
    const bool video = track.handler == FourCC('v', 'i', 'd', 'e');
    const auto ftyp = MakeSingleTrackFtyp(video);
    const std::uint64_t payloadBytes = SumBytes(track);
    const std::uint64_t mdatStart = ftyp.size();
    const std::uint64_t mdatHeader = 16U;
    const std::uint64_t chunkOffset = mdatStart + mdatHeader;
    const auto moov = MakeSingleTrackMoov(track, chunkOffset);

    fs::create_directories(outputPath.parent_path().empty() ? fs::path(".") : outputPath.parent_path());
    const fs::path part = outputPath.wstring() + L".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Cannot open single-track output part file");
    }

    out.write(reinterpret_cast<const char*>(ftyp.data()), static_cast<std::streamsize>(ftyp.size()));
    WriteU32BE(out, 1U);
    WriteU32BE(out, FourCC('m', 'd', 'a', 't'));
    WriteU64BE(out, 16U + payloadBytes);

    std::uint64_t copied = 0U;
    CopySamples(out, inputData, track, copied);
    out.write(reinterpret_cast<const char*>(moov.data()), static_cast<std::streamsize>(moov.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("Failed finalizing single-track output file");
    }

    std::error_code ec;
    fs::remove(outputPath, ec);
    ec.clear();
    fs::rename(part, outputPath, ec);
    if (ec) {
        throw std::runtime_error("Cannot rename single-track .part output: " + ec.message());
    }
}

TrackInfo LoadTrackAuto(const std::vector<std::uint8_t>& data, std::uint32_t handler, std::stop_token stop) {
    TrackInfo track = ParseInitTrack(data, handler);
    track.stop = stop;
    ParseSamplesAuto(data, track);
    return track;
}

}

namespace yinamp::media::native {
void ValidateAvcAacMp4(const std::filesystem::path& inputPath, std::stop_token stop) {
    const auto data = ReadAll(inputPath, stop);
    const auto video = LoadTrackAuto(data, FourCC('v', 'i', 'd', 'e'), stop);
    const auto audio = LoadTrackAuto(data, FourCC('s', 'o', 'u', 'n'), stop);
    if (video.height == 0 || video.height > 1080 || video.samples.empty() || audio.samples.empty() ||
        video.timescale == 0 || audio.timescale == 0)
        throw std::runtime_error("Unsupported or incomplete progressive H.264/AAC MP4");
}

MuxResult MuxFmp4ToFlatMp4(const std::filesystem::path& videoPath, const std::filesystem::path& audioPath,
                           const std::filesystem::path& outputPath, std::stop_token stop) {
    const auto videoData = ReadAll(videoPath, stop);
    const auto audioData = ReadAll(audioPath, stop);

    TrackInfo video = ParseInitTrack(videoData, FourCC('v', 'i', 'd', 'e'));
    TrackInfo audio = ParseInitTrack(audioData, FourCC('s', 'o', 'u', 'n'));
    video.stop = stop;
    audio.stop = stop;
    ParseFragments(videoData, video);
    ParseFragments(audioData, audio);

    if (video.samples.empty()) {
        throw std::runtime_error("H.264 fragmented MP4 contains no parsed samples");
    }
    if (audio.samples.empty()) {
        throw std::runtime_error("AAC fragmented MP4 contains no parsed samples");
    }

    WriteFlatMp4(outputPath, videoData, video, audioData, audio);

    MuxResult result;
    result.finalBytes = std::filesystem::file_size(outputPath);
    result.videoSamples = video.samples.size();
    result.audioSamples = audio.samples.size();
    return result;
}

TrackExtractResult ExtractAacTrackToM4a(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
                                        std::stop_token stop) {
    const auto data = ReadAll(inputPath, stop);
    TrackInfo audio = LoadTrackAuto(data, FourCC('s', 'o', 'u', 'n'), stop);
    if (ReadU32(audio.sampleEntry, 4U) != FourCC('m', 'p', '4', 'a')) {
        throw std::runtime_error("Audio track is not AAC/mp4a");
    }
    WriteSingleTrackMp4(outputPath, data, audio);
    return TrackExtractResult{std::filesystem::file_size(outputPath), audio.samples.size()};
}

TrackExtractResult ExtractH264TrackToMp4(const std::filesystem::path& inputPath,
                                         const std::filesystem::path& outputPath, std::stop_token stop) {
    const auto data = ReadAll(inputPath, stop);
    TrackInfo video = LoadTrackAuto(data, FourCC('v', 'i', 'd', 'e'), stop);
    if (ReadU32(video.sampleEntry, 4U) != FourCC('a', 'v', 'c', '1')) {
        throw std::runtime_error("Video track is not H.264/avc1");
    }
    WriteSingleTrackMp4(outputPath, data, video);
    return TrackExtractResult{std::filesystem::file_size(outputPath), video.samples.size()};
}

}
