#pragma once
#include "YouTubeExtractor.h"
#include <functional>

namespace yinamp::media::native {
struct Candidate {
    const ClientDiagnostic* client{};
    const StreamInfo* stream{};
};
struct MediaSelection {
    Candidate video;
    Candidate audio;
    bool adaptive{};
};
using Probe = std::function<unsigned long(Candidate)>;
std::map<std::wstring, std::wstring, std::less<>> MediaHeaders(const ClientDiagnostic& client);
MediaSelection SelectMedia(const ExtractionResult& result, const Probe& probe);
}
