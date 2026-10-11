#include "YouTubeExtractor.h"
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <ranges>
#include <regex>
#include <stdexcept>

namespace yinamp::media::native {
namespace {

struct ClientSpec {
    std::string id;
    std::string clientName;
    std::string clientVersion;
    int clientNumber{};
    std::string userAgent;
    std::string osName;
    std::string osVersion;
    std::string deviceMake;
    std::string deviceModel;
    std::optional<int> androidSdkVersion;
};

// InnerTube istemci profilleri burada tutulur; erişilebilirlik her işte yeniden ölçülür.
const std::vector<ClientSpec> kClients{
    {"ANDROID", "ANDROID", "21.26.364", 3, "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip",
     "Android", "11", "", "", 30},
    {"IOS", "IOS", "21.26.4", 5, "com.google.ios.youtube/21.26.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)",
     "iPhone", "18.3.2.22D82", "Apple", "iPhone16,2", std::nullopt},
    {"VISIONOS", "VISIONOS", "1.02", 101,
     "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 "
     "Safari/605.1.15",
     "visionOS", "26.5.23O471", "Apple", "RealityDevice17,1", std::nullopt},
    {"TV", "TVHTML5", "7.20260707.07.00", 7,
     "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown "
     "(Unknown, Unknown)",
     "", "", "", "", std::nullopt},
    {"TV_DOWNGRADED", "TVHTML5", "5.20260707", 7, "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/Version", "", "", "", "",
     std::nullopt},
    {"MWEB", "MWEB", "2.20260708.05.00", 2,
     "Mozilla/5.0 (iPad; CPU OS 16_7_10 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 "
     "Mobile/15E148 Safari/604.1,gzip(gfe)",
     "", "", "", "", std::nullopt},
    {"WEB", "WEB", "2.20260708.00.00", 1,
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/154 Safari/537.36",
     "Windows", "10", "", "", std::nullopt},
};

int IntField(const json::Value& object, std::string_view key) {
    if (const auto* value = object.Get(key)) {
        if (const auto number = value->AsNumber())
            return static_cast<int>(*number);
        if (const auto* text = value->AsString()) {
            int result{};
            const auto parsed = std::from_chars(text->data(), text->data() + text->size(), result);
            if (parsed.ec == std::errc{})
                return result;
        }
    }
    return 0;
}

long long LongField(const json::Value& object, std::string_view key) {
    if (const auto* value = object.Get(key)) {
        if (const auto number = value->AsNumber())
            return static_cast<long long>(*number);
        if (const auto* text = value->AsString()) {
            long long result{};
            const auto parsed = std::from_chars(text->data(), text->data() + text->size(), result);
            if (parsed.ec == std::errc{})
                return result;
        }
    }
    return 0;
}

std::string StringField(const json::Value& object, std::string_view key) {
    if (const auto* value = object.Get(key))
        if (const auto* text = value->AsString())
            return *text;
    return {};
}

void AppendFormats(const json::Value* arrayValue, std::vector<StreamInfo>& output, std::string_view clientId) {
    if (!arrayValue)
        return;
    const auto* array = arrayValue->AsArray();
    if (!array)
        return;
    for (const auto& item : *array) {
        if (!item.AsObject())
            continue;
        StreamInfo stream;
        stream.itag = IntField(item, "itag");
        stream.mimeType = StringField(item, "mimeType");
        stream.width = IntField(item, "width");
        stream.height = IntField(item, "height");
        stream.fps = IntField(item, "fps");
        if (const auto* color = item.Get("colorInfo")) {
            const auto transfer = StringField(*color, "transferCharacteristics");
            stream.hdr = transfer == "COLOR_TRANSFER_CHARACTERISTICS_SMPTE2084" ||
                         transfer == "COLOR_TRANSFER_CHARACTERISTICS_ARIB_STD_B67";
        }
        stream.bitrate = LongField(item, "bitrate");
        stream.contentLength = LongField(item, "contentLength");
        stream.qualityLabel = StringField(item, "qualityLabel");
        stream.hdr = stream.hdr || stream.qualityLabel.find("HDR") != std::string::npos;
        stream.audioQuality = StringField(item, "audioQuality");
        stream.url = StringField(item, "url");
        stream.signatureCipher = StringField(item, "signatureCipher");
        stream.sourceClient = std::string(clientId);
        output.push_back(std::move(stream));
    }
}

std::wstring CanonicalWatchUrl(std::string_view videoId) {
    return Utf8ToWide("https://www.youtube.com/watch?v=" + std::string(videoId) + "&hl=en");
}

bool ValidId(std::string_view id) {
    return id.size() == 11 && std::ranges::all_of(id, [](const char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
           });
}

std::string JsonString(std::string_view value) {
    return "\"" + json::Escape(value) + "\"";
}

std::string BuildPlayerRequest(std::string_view videoId, const ClientSpec& client) {
    std::string request = "{\"context\":{\"client\":{";
    request += "\"clientName\":" + JsonString(client.clientName);
    request += ",\"clientVersion\":" + JsonString(client.clientVersion);
    request += ",\"hl\":\"en\",\"gl\":\"US\"";
    if (!client.userAgent.empty())
        request += ",\"userAgent\":" + JsonString(client.userAgent);
    if (!client.osName.empty())
        request += ",\"osName\":" + JsonString(client.osName);
    if (!client.osVersion.empty())
        request += ",\"osVersion\":" + JsonString(client.osVersion);
    if (!client.deviceMake.empty())
        request += ",\"deviceMake\":" + JsonString(client.deviceMake);
    if (!client.deviceModel.empty())
        request += ",\"deviceModel\":" + JsonString(client.deviceModel);
    if (client.androidSdkVersion)
        request += ",\"androidSdkVersion\":" + std::to_string(*client.androidSdkVersion);
    request += "}},\"videoId\":" + JsonString(videoId);
    request += ",\"contentCheckOk\":true,\"racyCheckOk\":true}";
    return request;
}

std::optional<std::string> PickThumbnailFromRoot(const json::Value& root) {
    const auto* details = root.Get("videoDetails");
    if (!details)
        return {};
    const auto* thumbnail = details->Get("thumbnail");
    if (!thumbnail)
        return {};
    const auto* listValue = thumbnail->Get("thumbnails");
    const auto* list = listValue ? listValue->AsArray() : nullptr;
    if (!list || list->empty())
        return {};
    for (auto it = list->rbegin(); it != list->rend(); ++it) {
        if (const auto* url = it->Get("url"))
            if (const auto* text = url->AsString())
                return *text;
    }
    return {};
}

void ApplyMetadataIfMissing(ExtractionResult& output, const json::Value& root) {
    if (const auto* details = root.Get("videoDetails")) {
        if (output.title.empty())
            output.title = StringField(*details, "title");
        if (output.author.empty())
            output.author = StringField(*details, "author");
        if (output.durationSeconds == 0)
            output.durationSeconds = IntField(*details, "lengthSeconds");
    }
    if (output.thumbnailUrl.empty()) {
        if (const auto thumbnail = PickThumbnailFromRoot(root))
            output.thumbnailUrl = *thumbnail;
    }
}

}

bool StreamInfo::IsH264() const {
    return mimeType.find("avc1") != std::string::npos || mimeType.find("h264") != std::string::npos;
}

bool StreamInfo::IsAac() const {
    return mimeType.find("mp4a") != std::string::npos;
}

bool StreamInfo::HasVideo() const noexcept {
    return width > 0 || height > 0 || mimeType.starts_with("video/");
}

bool StreamInfo::HasAudio() const noexcept {
    return mimeType.starts_with("audio/") || !audioQuality.empty() || mimeType.find("mp4a") != std::string::npos ||
           mimeType.find("opus") != std::string::npos;
}

std::optional<std::string> YouTubeExtractor::ParseVideoId(std::wstring_view input) {
    const std::string value = WideToUtf8(input);
    const auto hostStart = value.find("://");
    if (hostStart == std::string::npos)
        return {};
    const auto authorityStart = hostStart + 3;
    const auto pathStart = value.find('/', authorityStart);
    const auto authority =
        value.substr(authorityStart, pathStart == std::string::npos ? std::string::npos : pathStart - authorityStart);
    std::string host = authority;
    std::ranges::transform(host, host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (const auto colon = host.find(':'); colon != std::string::npos)
        host.resize(colon);

    if (host == "youtu.be") {
        if (pathStart == std::string::npos)
            return {};
        const auto end = value.find_first_of("?#/", pathStart + 1);
        const auto id = value.substr(pathStart + 1, end == std::string::npos ? std::string::npos : end - pathStart - 1);
        return ValidId(id) ? std::optional{id} : std::nullopt;
    }

    if (host != "youtube.com" && host != "www.youtube.com" && host != "m.youtube.com" && host != "music.youtube.com")
        return {};
    if (pathStart == std::string::npos)
        return {};

    const auto queryStart = value.find('?', pathStart);
    const auto path =
        value.substr(pathStart, queryStart == std::string::npos ? std::string::npos : queryStart - pathStart);
    if (path == "/watch" && queryStart != std::string::npos) {
        auto pos = queryStart + 1;
        while (pos < value.size()) {
            const auto amp = value.find('&', pos);
            const auto token = value.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            if (token.starts_with("v=")) {
                const auto id = token.substr(2);
                return ValidId(id) ? std::optional{id} : std::nullopt;
            }
            if (amp == std::string::npos)
                break;
            pos = amp + 1;
        }
        return {};
    }

    for (const std::string_view prefix : {"/shorts/", "/live/"}) {
        if (path.starts_with(prefix)) {
            const auto id = path.substr(prefix.size());
            return ValidId(id) ? std::optional{id} : std::nullopt;
        }
    }
    return {};
}

std::optional<std::string> YouTubeExtractor::FindQuotedConfigValue(std::string_view html, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\":\"";
    const auto start = html.find(needle);
    if (start == std::string::npos)
        return {};
    std::string value;
    for (std::size_t i = start + needle.size(); i < html.size(); ++i) {
        const char c = html[i];
        if (c == '"')
            return value;
        if (c == '\\' && i + 1 < html.size()) {
            const char next = html[++i];
            switch (next) {
            case '\\':
                value.push_back('\\');
                break;
            case '"':
                value.push_back('"');
                break;
            case '/':
                value.push_back('/');
                break;
            default:
                value.push_back('\\');
                value.push_back(next);
                break;
            }
        } else {
            value.push_back(c);
        }
    }
    return {};
}

std::optional<std::string> YouTubeExtractor::PickThumbnail(const json::Value& root) {
    const auto* details = root.Get("videoDetails");
    if (!details)
        return {};
    const auto* thumbnail = details->Get("thumbnail");
    if (!thumbnail)
        return {};
    const auto* listValue = thumbnail->Get("thumbnails");
    const auto* list = listValue ? listValue->AsArray() : nullptr;
    if (!list || list->empty())
        return {};
    for (auto it = list->rbegin(); it != list->rend(); ++it) {
        if (const auto* url = it->Get("url"))
            if (const auto* text = url->AsString())
                return *text;
    }
    return {};
}

ExtractionResult YouTubeExtractor::Extract(std::wstring_view inputUrl, bool probeDirectUrls) {
    return ExtractImpl(inputUrl, probeDirectUrls, false);
}

ExtractionResult YouTubeExtractor::ExtractMetadata(std::wstring_view inputUrl) {
    return ExtractImpl(inputUrl, false, true);
}

HttpResponse YouTubeExtractor::Send(std::wstring_view method, std::wstring_view url, std::string_view body,
                                   const std::map<std::wstring, std::wstring, std::less<>>& headers) {
    http_.CheckCancellation();
    auto response = request_ ? request_(method, url, body, headers)
                            : method == L"GET" ? http_.Get(url, headers) : http_.PostJson(url, body, headers);
    http_.CheckCancellation();
    return response;
}

ExtractionResult YouTubeExtractor::ExtractImpl(std::wstring_view inputUrl, bool probeDirectUrls, bool metadataOnly) {
    const auto videoId = ParseVideoId(inputUrl);
    if (!videoId)
        throw std::runtime_error("Unsupported or invalid single-video YouTube URL");

    const auto watch = Send(
        L"GET", CanonicalWatchUrl(*videoId), {},
        {{L"Accept-Language", L"en-US,en;q=0.9"},
         {L"User-Agent", L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/154 Safari/537.36"}});
    if (watch.status != 200)
        throw std::runtime_error("YouTube watch page returned HTTP " + std::to_string(watch.status));

    ExtractionResult result;
    result.videoId = *videoId;
    if (metadataOnly) {
        static const std::regex marker(R"((?:\bytInitialPlayerResponse|window\s*\[\s*"ytInitialPlayerResponse"\s*\])\s*=\s*)");
        for (auto it = std::sregex_iterator(watch.body.begin(), watch.body.end(), marker);
             it != std::sregex_iterator{}; ++it) {
            try {
                const auto object = json::ObjectAt(watch.body, static_cast<std::size_t>(it->position() + it->length()));
                if (object.empty()) continue;
                const auto root = json::Parse(object);
                const auto* details = root.Get("videoDetails");
                if (!details || StringField(*details, "videoId") != *videoId) continue;
                ApplyMetadataIfMissing(result, root);
                if (!result.title.empty()) return result;
            } catch (const std::runtime_error&) {
                http_.CheckCancellation();
            }
        }
    }
    const auto apiKey = FindQuotedConfigValue(watch.body, "INNERTUBE_API_KEY");
    if (!apiKey)
        throw std::runtime_error("INNERTUBE_API_KEY not found in watch page; YouTube page shape changed");

    const auto endpoint =
        Utf8ToWide("https://www.youtube.com/youtubei/v1/player?key=" + *apiKey + "&prettyPrint=false");

    for (const auto& client : kClients) {
        http_.CheckCancellation();
        ClientDiagnostic diagnostic;
        diagnostic.id = client.id;
        diagnostic.clientName = client.clientName;
        diagnostic.clientVersion = client.clientVersion;
        diagnostic.clientNumber = client.clientNumber;
        diagnostic.userAgent = client.userAgent;
        try {
            std::map<std::wstring, std::wstring, std::less<>> headers{
                {L"X-YouTube-Client-Name", std::to_wstring(client.clientNumber)},
                {L"X-YouTube-Client-Version", Utf8ToWide(client.clientVersion)},
                {L"Origin", L"https://www.youtube.com"},
            };
            if (!client.userAgent.empty())
                headers.emplace(L"User-Agent", Utf8ToWide(client.userAgent));

            const auto response = Send(L"POST", endpoint, BuildPlayerRequest(*videoId, client), headers);
            if (response.status != 200) {
                diagnostic.requestError = "HTTP " + std::to_string(response.status);
                result.clients.push_back(std::move(diagnostic));
                continue;
            }

            (void)Utf8ToWide(response.body);
            const auto root = json::Parse(response.body);
            const auto* details = root.Get("videoDetails");
            if (!metadataOnly || (details && StringField(*details, "videoId") == *videoId))
                ApplyMetadataIfMissing(result, root);
            if (metadataOnly && !result.title.empty()) return result;
            if (const auto* playability = root.Get("playabilityStatus")) {
                diagnostic.playabilityStatus = StringField(*playability, "status");
                diagnostic.playabilityReason = StringField(*playability, "reason");
            }
            if (const auto* streaming = root.Get("streamingData"); streaming && !metadataOnly) {
                AppendFormats(streaming->Get("formats"), diagnostic.streams, client.id);
                AppendFormats(streaming->Get("adaptiveFormats"), diagnostic.streams, client.id);
                diagnostic.serverAbrStreamingUrl = StringField(*streaming, "serverAbrStreamingUrl");
                diagnostic.hlsManifestUrl = StringField(*streaming, "hlsManifestUrl");
                diagnostic.dashManifestUrl = StringField(*streaming, "dashManifestUrl");
            }

            if (probeDirectUrls) {
                const StreamInfo* bestVideo{};
                const StreamInfo* bestAudio{};
                const StreamInfo* bestMuxed{};
                for (const auto& stream : diagnostic.streams) {
                    if (!stream.Direct())
                        continue;
                    if (stream.IsVideoOnly() && stream.IsH264() && stream.height > 0 && stream.height <= 1080 &&
                        (!bestVideo || stream.height > bestVideo->height ||
                         (stream.height == bestVideo->height && stream.bitrate > bestVideo->bitrate)))
                        bestVideo = &stream;
                    if (stream.IsAudioOnly() && stream.IsAac() && (!bestAudio || stream.bitrate > bestAudio->bitrate))
                        bestAudio = &stream;
                    if (stream.IsMuxed() && stream.IsH264() && stream.IsAac() && stream.height > 0 &&
                        stream.height <= 1080 &&
                        (!bestMuxed || stream.height > bestMuxed->height ||
                         (stream.height == bestMuxed->height && stream.bitrate > bestMuxed->bitrate)))
                        bestMuxed = &stream;
                }

                auto probe = [&](const StreamInfo* selected) {
                    if (!selected)
                        return;
                    auto it =
                        std::ranges::find_if(diagnostic.streams, [&](const auto& item) { return &item == selected; });
                    if (it == diagnostic.streams.end())
                        return;
                    try {
                        it->probeStatus = http_.ProbeRange(
                            Utf8ToWide(it->url),
                            {
                                {L"User-Agent",
                                 Utf8ToWide(client.userAgent.empty() ? "Mozilla/5.0" : client.userAgent)},
                                {L"Referer", L"https://www.youtube.com/"},
                            });
                    } catch (...) {
                        it->probeStatus = 0;
                    }
                };
                probe(bestVideo);
                probe(bestAudio);
                probe(bestMuxed);
            }
        } catch (const std::exception& error) {
            http_.CheckCancellation();
            diagnostic.requestError = error.what();
        }
        result.clients.push_back(std::move(diagnostic));
    }

    return result;
}

}
