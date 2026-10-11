#pragma once

#include "HttpClient.h"
#include "Json.h"

#include <optional>
#include <string>
#include <vector>

namespace yinamp::media::native {

struct StreamInfo {
    int itag{};
    std::string mimeType;
    int width{};
    int height{};
    int fps{};
    bool hdr{};
    long long bitrate{};
    long long contentLength{};
    std::string qualityLabel;
    std::string audioQuality;
    std::string url;
    std::string signatureCipher;
    std::string sourceClient;
    unsigned long probeStatus{};

    [[nodiscard]] bool Direct() const noexcept {
        return !url.empty();
    }
    [[nodiscard]] bool IsH264() const;
    [[nodiscard]] bool IsAac() const;
    [[nodiscard]] bool HasVideo() const noexcept;
    [[nodiscard]] bool HasAudio() const noexcept;
    [[nodiscard]] bool IsVideoOnly() const noexcept {
        return HasVideo() && !HasAudio();
    }
    [[nodiscard]] bool IsAudioOnly() const noexcept {
        return !HasVideo() && HasAudio();
    }
    [[nodiscard]] bool IsMuxed() const noexcept {
        return HasVideo() && HasAudio();
    }
};

struct ClientDiagnostic {
    std::string id;
    std::string clientName;
    std::string clientVersion;
    int clientNumber{};
    std::string userAgent;
    std::string playabilityStatus;
    std::string playabilityReason;
    std::string serverAbrStreamingUrl;
    std::string hlsManifestUrl;
    std::string dashManifestUrl;
    std::string requestError;
    std::vector<StreamInfo> streams;
};

struct ExtractionResult {
    std::string videoId;
    std::string title;
    std::string author;
    int durationSeconds{};
    std::string thumbnailUrl;
    std::vector<ClientDiagnostic> clients;
};

class YouTubeExtractor final {
  public:
    using Request = std::function<HttpResponse(std::wstring_view, std::wstring_view, std::string_view,
        const std::map<std::wstring, std::wstring, std::less<>>&)>;
    explicit YouTubeExtractor(HttpClient& http, Request request = {}) : http_(http), request_(std::move(request)) {}

    ExtractionResult Extract(std::wstring_view inputUrl, bool probeDirectUrls = true);
    ExtractionResult ExtractMetadata(std::wstring_view inputUrl);
    static std::optional<std::string> ParseVideoId(std::wstring_view inputUrl);

  private:
    HttpClient& http_;
    Request request_;
    HttpResponse Send(std::wstring_view method, std::wstring_view url, std::string_view body,
                      const std::map<std::wstring, std::wstring, std::less<>>& headers);
    ExtractionResult ExtractImpl(std::wstring_view inputUrl, bool probeDirectUrls, bool metadataOnly);

    static std::optional<std::string> FindQuotedConfigValue(std::string_view html, std::string_view key);
    static std::optional<std::string> PickThumbnail(const json::Value& root);
};

}
