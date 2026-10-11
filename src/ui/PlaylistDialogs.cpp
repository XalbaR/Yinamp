#include "ui/PlaylistDialogs.h"
#include "platform/windows/WindowsRuntime.h"
#include "playlist/PlaylistOperations.h"
#include "ui/YouTubeUrlListResultWindow.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace yinamp::ui {
std::optional<std::filesystem::path> PickPlaylistPath(HWND owner, bool save, bool html, bool urlList) {
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    platform::CheckHResult(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())),
                           "Playlist dialog creation");
    DWORD options{};
    platform::CheckHResult(dialog->GetOptions(&options), "Playlist dialog options");
    platform::CheckHResult(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                                              (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST)),
                           "Playlist dialog options");
    const COMDLG_FILTERSPEC playlistFilters[]{{L"UTF-8 playlist (*.m3u8)", L"*.m3u8"},
                                              {L"M3U playlist (*.m3u)", L"*.m3u"},
                                              {L"PLS playlist (*.pls)", L"*.pls"}};
    const COMDLG_FILTERSPEC htmlFilter{L"HTML playlist (*.html)", L"*.html"};
    const COMDLG_FILTERSPEC textFilter{L"Text Files (*.txt)", L"*.txt"};
    platform::CheckHResult(dialog->SetFileTypes(html || urlList ? 1U : 3U,
                           urlList ? &textFilter : html ? &htmlFilter : playlistFilters),
                           "Playlist file types");
    platform::CheckHResult(dialog->SetDefaultExtension(urlList ? L"txt" : html ? L"html" : L"m3u8"), "Playlist extension");
    platform::CheckHResult(dialog->SetTitle(urlList ? L"ADD URL LIST" : html ? L"Generate HTML Playlist"
                                            : save ? L"Save Playlist"
                                                   : L"Load Playlist (append)"),
                           "Playlist dialog title");
    const auto status = dialog->Show(owner);
    if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    platform::CheckHResult(status, "Playlist dialog show");
    Microsoft::WRL::ComPtr<IShellItem> item;
    platform::CheckHResult(dialog->GetResult(item.GetAddressOf()), "Playlist dialog result");
    PWSTR name{};
    platform::CheckHResult(item->GetDisplayName(SIGDN_FILESYSPATH, &name), "Playlist dialog path");
    struct Free {
        PWSTR value;
        ~Free() {
            CoTaskMemFree(value);
        }
    } free{name};
    std::filesystem::path path(name);
    if (save && !html) {
        UINT index{};
        platform::CheckHResult(dialog->GetFileTypeIndex(&index), "Playlist format selection");
        if (path.extension().empty())
            path.replace_extension(index == 2 ? L".m3u" : index == 3 ? L".pls" : L".m3u8");
    }
    return path;
}
bool RecyclePlaylistFile(HWND owner, const std::filesystem::path& path) {
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(operation.GetAddressOf()))) ||
        FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.GetAddressOf()))))
        return false;
    if (FAILED(operation->SetOwnerWindow(owner)) ||
        FAILED(operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD |
                                            FOF_NO_CONNECTED_ELEMENTS | FOF_NORECURSION | FOF_WANTNUKEWARNING)) ||
        FAILED(operation->DeleteItem(item.Get(), nullptr)) || FAILED(operation->PerformOperations()))
        return false;
    BOOL aborted = TRUE;
    if (FAILED(operation->GetAnyOperationsAborted(&aborted)) || aborted)
        return false;
    std::error_code error;
    return !std::filesystem::exists(path, error) && !error;
}
bool ConfirmPlaylistDeletion(HWND owner, std::size_t local, std::size_t youtube) {
    const auto text =
        L"Remove " + std::to_wstring(local) +
        L" selected local file entry/entries from storage?\n\n"
        L"The files will be removed from storage using Windows shell deletion (Recycle Bin where supported). "
        L"Only successfully deleted files will be removed from the playlist.\n\n" +
        std::to_wstring(youtube) +
        L" selected YouTube item(s) will be skipped. Their MediaStore content will remain intact.";
    return MessageBoxW(owner, text.c_str(), L"Physically remove selected files",
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}
void ShowPlaylistInformation(HWND owner, const playlist::Entry& entry, const skin::SkinAssets& assets,
                             core::IntegerScale scale) {
    const auto text = playlist::EntryInformation(entry);
    YouTubeUrlListResultWindow dialog(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE)),
        owner, assets, scale, text, YouTubeUrlListResultWindow::Title::FileInfo);
    dialog.Run();
}
}
