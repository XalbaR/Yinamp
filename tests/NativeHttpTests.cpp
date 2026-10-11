#include "TemporaryMediaRoot.h"
#include "media/native/HttpClient.h"
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace {
using namespace std::chrono_literals;
using namespace yinamp::media::native;
void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
class LoopbackResponse final {
  public:
    LoopbackResponse(std::string response, bool stall) : response_(std::move(response)), stall_(stall) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Check(listener_ != INVALID_SOCKET, "Loopback socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "Loopback bind");
        int size = sizeof(address);
        Check(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) == 0, "Loopback address");
        url = L"http://127.0.0.1:" + std::to_wstring(ntohs(address.sin_port)) + L"/media";
        Check(listen(listener_, 1) == 0, "Loopback listen");
        worker_ = std::jthread([this](std::stop_token stop) {
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(listener_, &ready);
            timeval timeout{3, 0};
            if (select(0, &ready, nullptr, nullptr, &timeout) <= 0)
                return;
            const SOCKET client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                return;
            DWORD receiveTimeout = 3000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout),
                       sizeof(receiveTimeout));
            char request[4096]{};
            if (recv(client, request, sizeof(request), 0) > 0) {
                send(client, response_.data(), static_cast<int>(response_.size()), 0);
                served_.set_value();
                if (stall_) {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock, stop, [] { return false; });
                }
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        });
    }
    ~LoopbackResponse() {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
        closesocket(listener_);
    }
    std::wstring url;
    std::future<void> Served() {
        return served_.get_future();
    }

  private:
    SOCKET listener_{INVALID_SOCKET};
    std::string response_;
    bool stall_;
    std::promise<void> served_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::jthread worker_;
};
}
int main() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    struct Cleanup {
        ~Cleanup() {
            WSACleanup();
        }
    } cleanup;
    try {
        TemporaryMediaRoot temporary;
        {
            LoopbackResponse server("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", false);
            HttpClient http;
            bool denied{};
            try {
                http.DownloadToFile(server.url, temporary.path / L"denied", 0, {});
            } catch (const HttpStatusError& error) {
                denied = error.status == 403;
            }
            Check(denied && !std::filesystem::exists(temporary.path / L"denied"), "HTTP authorization rejection");
        }
        {
            LoopbackResponse server("HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nabc", false);
            HttpClient http;
            bool failed{};
            try {
                http.DownloadToFile(server.url, temporary.path / L"truncated", 8, {});
            } catch (const std::exception&) {
                failed = true;
            }
            Check(failed && !std::filesystem::exists(temporary.path / L"truncated"),
                  "Incomplete HTTP body never published");
        }
        {
            LoopbackResponse server("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nx", true);
            std::promise<void> finished;
            auto received = server.Served();
            auto done = finished.get_future();
            std::jthread download([&](std::stop_token stop) {
                try {
                    HttpClient http(stop);
                    http.DownloadToFile(server.url, temporary.path / L"cancelled", 100, {});
                } catch (...) {
                }
                finished.set_value();
            });
            Check(received.wait_for(3s) == std::future_status::ready, "Stalled download reached read");
            const auto start = std::chrono::steady_clock::now();
            download.request_stop();
            Check(done.wait_for(2s) == std::future_status::ready, "Cancellation interrupts active WinHTTP read");
            download.join();
            Check(std::chrono::steady_clock::now() - start < 2s &&
                      !std::filesystem::exists(temporary.path / L"cancelled"),
                  "Bounded cancellation and no completed download");
        }
        std::cout << "Offline loopback HTTP validation and in-flight cancellation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
