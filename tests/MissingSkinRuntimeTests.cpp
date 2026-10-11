#include "app/BuildPaths.h"
#include "platform/windows/WindowsRuntime.h"
#include "skin/SkinLoader.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
class Process final {
  public:
    explicit Process(const std::filesystem::path& executable) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        auto command = L"\"" + executable.wstring() + L"\"";
        const auto workingDirectory = std::filesystem::temp_directory_path();
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            workingDirectory.c_str(), &startup, &information_)) {
            throw std::runtime_error("Could not launch Yinamp.exe.");
        }
    }
    ~Process() {
        // Başarısız testte yalnızca bu testin başlattığı süreci kapatarak modal pencerenin kalmasını önlüyoruz.
        if (WaitForSingleObject(information_.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(information_.hProcess, 99);
            WaitForSingleObject(information_.hProcess, 3000);
        }
        CloseHandle(information_.hThread);
        CloseHandle(information_.hProcess);
    }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    [[nodiscard]] const PROCESS_INFORMATION& Information() const {
        return information_;
    }

  private:
    PROCESS_INFORMATION information_{};
};
struct DialogSearch {
    DWORD processId;
    HWND dialog{};
    std::wstring text;
};
BOOL CALLBACK FindDialog(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<DialogSearch*>(parameter);
    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    wchar_t className[64]{};
    GetClassNameW(window, className, 64);
    if (processId == search.processId && std::wstring_view(className) == L"#32770") {
        search.dialog = window;
        return FALSE;
    }
    return TRUE;
}
BOOL CALLBACK ReadDialog(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<DialogSearch*>(parameter);
    const int length = GetWindowTextLengthW(window);
    if (length > 0) {
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(window, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(copied));
        search.text += text + L'\n';
    }
    return TRUE;
}
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        using namespace yinamp;
        if (argc != 2) {
            throw std::runtime_error("Expected the Yinamp.exe path.");
        }
        const std::filesystem::path directory(app::DefaultSkinDirectory);
        const auto missing = skin::FindMissingAssets(directory);
        if (missing.empty()) {
            std::cout << "Skipped: authentic assets are present.\n";
            return 77;
        }
        Process process(argv[1]);
        DialogSearch search{process.Information().dwProcessId, nullptr, {}};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline && !search.dialog) {
            EnumWindows(&FindDialog, reinterpret_cast<LPARAM>(&search));
            if (!search.dialog) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
        if (!search.dialog) {
            throw std::runtime_error("Yinamp startup diagnostic did not appear.");
        }
        EnumChildWindows(search.dialog, &ReadDialog, reinterpret_cast<LPARAM>(&search));
        if (search.text.find(L"Classic skin assets are missing") == std::wstring::npos ||
            search.text.find(directory.wstring()) == std::wstring::npos) {
            // CMake yolları ileri eğik çizgi içerir; filesystem'in yerel gösterimi de aynı şekilde kalabilir.
            auto alternate = directory.wstring();
            for (auto& character : alternate) {
                if (character == L'/') {
                    character = L'\\';
                }
            }
            if (search.text.find(L"Classic skin assets are missing") == std::wstring::npos ||
                search.text.find(alternate) == std::wstring::npos) {
                throw std::runtime_error("Yinamp diagnostic omitted the missing-skin reason or expected directory.");
            }
        }
        for (const auto& file : missing) {
            if (search.text.find(platform::ToUtf16(file)) == std::wstring::npos) {
                throw std::runtime_error("Yinamp diagnostic omitted a missing required file.");
            }
        }
        PostMessageW(search.dialog, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.Information().hProcess, 5000) != WAIT_OBJECT_0) {
            throw std::runtime_error("Yinamp did not shut down after diagnostic dismissal.");
        }
        DWORD exitCode{};
        if (!GetExitCodeProcess(process.Information().hProcess, &exitCode) || exitCode != 1) {
            throw std::runtime_error("Yinamp did not return the expected missing-skin exit code.");
        }
        std::cout << "Launched Yinamp.exe: verified native missing-skin diagnostic, expected directory, "
                     "all missing filenames and clean exit code 1.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
