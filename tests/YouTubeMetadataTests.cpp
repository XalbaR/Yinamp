#include "media/native/YouTubeExtractor.h"
#include <iostream>
#include <stdexcept>
using namespace yinamp::media::native;
namespace {
constexpr auto Id = "AbCdEf012_-";
constexpr wchar_t Url[] = L"https://youtu.be/AbCdEf012_-";
void Check(bool value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}
std::string Metadata(std::string_view id = Id) {
    return "{\"videoDetails\":{\"videoId\":\"" + std::string(id) +
           "\",\"title\":\"Başlık } [ \\\" 日本\",\"lengthSeconds\":\"123\",\"author\":\"Author\"},"
           "\"streamingData\":{\"formats\":[]}}";
}
void WatchPage() {
    for (const auto marker :
         {"var ytInitialPlayerResponse = ", "window[\"ytInitialPlayerResponse\"] = ", "ytInitialPlayerResponse\t=\n"}) {
        HttpClient http;
        unsigned requests{};
        YouTubeExtractor extractor(http, [&](auto method, auto, auto, const auto&) {
            ++requests;
            Check(method == L"GET", "watch metadata requires no player API request");
            return HttpResponse{200, "<script>" + std::string(marker) + Metadata() + ";</script>", {}};
        });
        const auto result = extractor.ExtractMetadata(Url);
        Check(requests == 1 && result.videoId == Id && result.title == "Başlık } [ \" 日本" &&
                  result.durationSeconds == 123 && result.author == "Author" && result.clients.empty(),
              "one request yields complete Unicode metadata without API key or stream resolution");
    }
}
void Fallback() {
    for (const bool firstDenied : {false, true}) {
        HttpClient http;
        unsigned requests{}, posts{};
        YouTubeExtractor extractor(http, [&](auto method, auto, auto, const auto&) {
            ++requests;
            if (method == L"GET")
                return HttpResponse{
                    200, R"(<script>ytInitialPlayerResponse = {broken};</script>"INNERTUBE_API_KEY":"fixture")", {}};
            ++posts;
            if (firstDenied && posts == 1)
                return HttpResponse{403, {}, {}};
            return HttpResponse{200, Metadata(), {}};
        });
        const auto result = extractor.ExtractMetadata(Url);
        Check(result.durationSeconds == 123 && requests == (firstDenied ? 3U : 2U) &&
                  result.clients.size() == (firstDenied ? 1U : 0U),
              "fallback stops at first successful metadata client, skipping remaining profiles");
    }
}
void FullExtraction() {
    HttpClient http;
    unsigned requests{}, posts{};
    YouTubeExtractor extractor(http, [&](auto method, auto, auto, const auto&) {
        ++requests;
        if (method == L"GET")
            return HttpResponse{200, R"("INNERTUBE_API_KEY":"fixture")", {}};
        ++posts;
        return HttpResponse{200, Metadata(), {}};
    });
    const auto result = extractor.Extract(Url, false);
    Check(posts == 7 && requests == 8 && result.clients.size() == 7 && result.durationSeconds == 123,
          "Download and Online playback still collect all format clients");
}
void Identity() {
    HttpClient http;
    unsigned requests{};
    YouTubeExtractor extractor(http, [&](auto method, auto, auto, const auto&) {
        ++requests;
        if (method == L"GET")
            return HttpResponse{200,
                                "var ytInitialPlayerResponse = " + Metadata("ZbCdEf012_-") +
                                    R"(;"INNERTUBE_API_KEY":"fixture")",
                                {}};
        return HttpResponse{200, Metadata(), {}};
    });
    Check(extractor.ExtractMetadata(Url).videoId == Id && requests == 2, "wrong video metadata falls back safely");
}
}
int main() {
    try {
        WatchPage();
        Fallback();
        FullExtraction();
        Identity();
        std::cout << "Metadata fast path, first-client fallback, full playback extraction and identity passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
