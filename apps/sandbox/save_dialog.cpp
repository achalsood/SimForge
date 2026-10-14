#include "save_dialog.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <array>
#include <algorithm>
#include <SDL3/SDL.h>
std::optional<std::filesystem::path> open_arrival_csv_dialog(const std::string&,std::string& error) {
    std::array<wchar_t,32768> buffer{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    if(auto* window=SDL_GetKeyboardFocus())dialog.hwndOwner=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    dialog.lpstrFilter=L"Shipment arrival CSV (*.csv)\0*.csv\0\0";dialog.lpstrTitle=L"Import local shipment arrivals";dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
    if(!GetOpenFileNameW(&dialog)){if(CommDlgExtendedError())error="Arrival CSV dialog failed";return {};}
    error.clear();return std::filesystem::path(buffer.data());
}
std::optional<std::filesystem::path> project_dialog(bool open,const std::string& suggested,std::string& error)
{
    std::array<wchar_t,32768> buffer{};
    auto filename=std::filesystem::u8path(suggested).filename(); filename.replace_extension(L".simforge");
    const auto name=filename.wstring(); if(name.size()>=buffer.size()) {error="Project filename too long";return {};}
    std::copy(name.begin(),name.end(),buffer.begin());
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=buffer.data();dialog.nMaxFile=static_cast<DWORD>(buffer.size());
    if(auto* window=SDL_GetKeyboardFocus())
        dialog.hwndOwner=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    dialog.lpstrFilter=L"SimForge projects (*.simforge)\0*.simforge\0\0";dialog.lpstrDefExt=L"simforge";
    dialog.lpstrTitle=open?L"Open SimForge project":L"Save SimForge project as";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(open?OFN_FILEMUSTEXIST:OFN_OVERWRITEPROMPT);
    if(!(open?GetOpenFileNameW(&dialog):GetSaveFileNameW(&dialog))) {
        if(CommDlgExtendedError()) error="Project file dialog failed"; return {};
    }
    std::filesystem::path path(buffer.data());
    // Save As never silently replaces a file. Ordinary Save updates its associated path.
    if(!open && std::filesystem::exists(path)) {error="Choose a new project filename. Use Save to update the open project.";return {};}
    error.clear();return path;
}
std::optional<std::filesystem::path> save_report_dialog(bool csv,const std::string& suggested,std::string& error)
{
    try
    {
        std::array<wchar_t,32768> buffer{};
        auto filename=std::filesystem::u8path(suggested).filename(); filename.replace_extension(csv?L".csv":L".json");
        const auto name=filename.wstring();
        if(name.size()>=buffer.size()) { error="Suggested filename is too long"; return {}; }
        std::copy(name.begin(),name.end(),buffer.begin());
        OPENFILENAMEW dialog{};
        if(auto* window=SDL_GetKeyboardFocus())
            dialog.hwndOwner=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
        dialog.lStructSize=sizeof(dialog); dialog.lpstrFile=buffer.data(); dialog.nMaxFile=static_cast<DWORD>(buffer.size());
        dialog.lpstrFilter=csv?L"CSV summary (*.csv)\0*.csv\0\0":L"JSON report (*.json)\0*.json\0\0";
        dialog.lpstrDefExt=csv?L"csv":L"json"; dialog.lpstrTitle=L"Export report to a new file";
        dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
        if(!GetSaveFileNameW(&dialog)) { const auto code=CommDlgExtendedError(); if(code) error="Save As failed (Windows error "+std::to_string(code)+")"; return {}; }
        std::filesystem::path path{buffer.data()};
        if(std::filesystem::exists(path)) { error="This file exists. Choose a new filename; reports are never overwritten."; return {}; }
        error.clear(); return path;
    }
    catch(const std::exception& e) { error=e.what(); return {}; }
}

#else
std::optional<std::filesystem::path> open_arrival_csv_dialog(const std::string&,std::string& error){error="Enter a local CSV path; native dialog is Windows-only";return {};}
std::optional<std::filesystem::path> project_dialog(bool,const std::string&,std::string& error)
{error="Enter a project path; native dialogs are available on Windows.";return {};}
std::optional<std::filesystem::path> save_report_dialog(bool,const std::string&,std::string& error)
{ error="Native Save As is available on Windows. Enter an explicit new-file path below."; return {}; }
#endif
