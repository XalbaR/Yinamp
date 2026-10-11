#pragma once
#include <algorithm>
#include <atomic>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
class HttpRangeFixture final {
  public:
    std::map<std::string, std::string> files;
    std::atomic<unsigned> requests{}, expiredRequests{};
    std::atomic<std::uint64_t> transferred{};
    std::atomic<unsigned> largestRange{};
    explicit HttpRangeFixture(std::map<std::string, std::string> content) : files(std::move(content)) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("WSAStartup");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener_ == INVALID_SOCKET || bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
            throw std::runtime_error("Range fixture bind");
        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        base_ = L"http://127.0.0.1:" + std::to_wstring(ntohs(address.sin_port));
        if (listen(listener_, 16))
            throw std::runtime_error("Range fixture listen");
        worker_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                fd_set ready;
                FD_ZERO(&ready);
                FD_SET(listener_, &ready);
                timeval timeout{0, 100000};
                if (select(0, &ready, nullptr, nullptr, &timeout) <= 0)
                    continue;
                const auto client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET)
                    continue;
                DWORD receiveTimeout = 2000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout),
                           sizeof(receiveTimeout));
                char bytes[8192]{};
                const auto count = recv(client, bytes, sizeof(bytes), 0);
                if (count > 0)
                    Serve(client, std::string(bytes, static_cast<std::size_t>(count)));
                shutdown(client, SD_BOTH);
                closesocket(client);
            }
        });
    }
    ~HttpRangeFixture() {
        worker_.request_stop();
        worker_.join();
        closesocket(listener_);
        WSACleanup();
    }
    std::wstring Url(std::wstring_view path) const {
        return base_ + std::wstring(path);
    }

  private:
    static void Send(SOCKET socket, std::string_view bytes) {
        while (!bytes.empty()) {
            const auto sent =
                send(socket, bytes.data(), static_cast<int>(std::min<std::size_t>(bytes.size(), 16384)), 0);
            if (sent <= 0)
                return;
            bytes.remove_prefix(static_cast<std::size_t>(sent));
        }
    }
    void Serve(SOCKET client, const std::string& request) {
        ++requests;
        const auto first = request.find(' '), last = request.find(' ', first + 1);
        const auto path = request.substr(first + 1, last - first - 1);
        if (path == "/expired") {
            ++expiredRequests;
            Send(client, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        const auto file = files.find(path);
        if (file == files.end()) {
            Send(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        auto lower = request;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto range = lower.find("range: bytes=");
        unsigned long long start{}, end{};
        if (range == std::string::npos ||
            sscanf_s(lower.c_str() + range, "range: bytes=%llu-%llu", &start, &end) != 2 ||
            start >= file->second.size()) {
            Send(client, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        largestRange = std::max(largestRange.load(), static_cast<unsigned>(end - start + 1));
        end = std::min<unsigned long long>(end, file->second.size() - 1);
        const auto size = static_cast<std::size_t>(end - start + 1);
        const auto header = "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\nContent-Range: bytes " +
                            std::to_string(start) + "-" + std::to_string(end) + "/" +
                            std::to_string(file->second.size()) + "\r\nContent-Length: " + std::to_string(size) +
                            "\r\nConnection: close\r\n\r\n";
        Send(client, header);
        Send(client, std::string_view(file->second).substr(static_cast<std::size_t>(start), size));
        transferred += size;
    }
    SOCKET listener_{INVALID_SOCKET};
    std::wstring base_;
    std::jthread worker_;
};
