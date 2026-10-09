#pragma once
#include <filesystem>
#include <optional>
#include <string>
// Native UI stays in the graphical app; cancellation returns no path.
// Reports use exclusive creation, so Save As deliberately refuses replacement.
std::optional<std::filesystem::path> save_report_dialog(bool csv,const std::string& suggested,std::string& error);
