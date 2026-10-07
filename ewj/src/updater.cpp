#include "updater.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/filesystem.h>
#include <rex/logging.h>

#include "trg/download.h"
#include "trg/platform.h"

#ifndef EWJ_VERSION
#define EWJ_VERSION "0.0.0"
#endif

namespace ewj::update {
namespace {

namespace fs = std::filesystem;

constexpr const char* kLatestApi =
    "https://api.github.com/repos/TekRantGaming/earthworm-jim-hd-recompiled/releases/latest";
constexpr const char* kZipSuffix = "-windows-x64.zip";

// "v1.2.3" / "1.2.3-preview" -> {1, 2, 3}
std::vector<int> ParseVersion(const std::string& text) {
  std::vector<int> parts;
  std::smatch m;
  std::string rest = text;
  static const std::regex number(R"((\d+))");
  while (parts.size() < 4 && std::regex_search(rest, m, number)) {
    parts.push_back(std::stoi(m[1].str()));
    rest = m.suffix().str();
    if (rest.empty() || rest[0] != '.') break;
  }
  while (parts.size() < 3) parts.push_back(0);
  return parts;
}

// The first "key": "value" string in a JSON text, from `from` on.
std::string JsonString(const std::string& json, const std::string& key, size_t from = 0, size_t* at = nullptr) {
  const std::string needle = "\"" + key + "\"";
  size_t k = json.find(needle, from);
  if (k == std::string::npos) return {};
  size_t q1 = json.find('"', json.find(':', k + needle.size()) + 1);
  if (q1 == std::string::npos) return {};
  std::string value;
  for (size_t i = q1 + 1; i < json.size() && json[i] != '"'; ++i) {
    if (json[i] == '\\' && i + 1 < json.size()) ++i;
    value += json[i];
  }
  if (at) *at = k;
  return value;
}

fs::path ExeDir() { return rex::filesystem::GetExecutableFolder(); }

// Program files an update may replace (never the game folder, settings or logs).
bool IsProgramFile(const fs::path& p) {
  const auto ext = p.extension().string();
  return ext == ".exe" || ext == ".dll" || ext == ".txt" || ext == ".md";
}

#if defined(_WIN32)
// Runs a command hidden and waits; returns its exit code (-1 if it could not start).
int RunHidden(std::wstring command) {
  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                      &pi))
    return -1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return int(code);
}
#endif

}  // namespace

const char* CurrentVersion() {
  // Testing aid: EWJ_UPDATE_TEST_VERSION=0.0.1 makes this build look old, so
  // the update pop-up and install can be tried against the live release.
  static const char* test = std::getenv("EWJ_UPDATE_TEST_VERSION");
  return test && *test ? test : EWJ_VERSION;
}

std::optional<Release> CheckLatest(std::string* error) {
  std::string json;
  if (std::string err = trg::HttpGet(kLatestApi, json); !err.empty()) {
    if (error) *error = err;
    return std::nullopt;
  }
  Release r;
  r.tag = JsonString(json, "tag_name");
  r.page = JsonString(json, "html_url");
  if (r.tag.empty()) {
    if (error) *error = "GitHub did not return a release.";
    return std::nullopt;
  }
  // The asset whose download URL ends in -windows-x64.zip.
  for (size_t pos = 0;;) {
    size_t at = 0;
    const std::string url = JsonString(json, "browser_download_url", pos, &at);
    if (url.empty()) break;
    if (url.size() > std::strlen(kZipSuffix) && url.ends_with(kZipSuffix)) {
      r.zip = url;
      break;
    }
    pos = at + 1;
  }
  if (r.zip.empty()) return std::nullopt;
  if (ParseVersion(r.tag) <= ParseVersion(CurrentVersion())) return std::nullopt;
  REXLOG_INFO("EWJ: update {} available (this is {})", r.tag, CurrentVersion());
  return r;
}

std::string Install(trg::Task& task, const Release& release) {
#if defined(_WIN32)
  std::error_code ec;
  const fs::path work = fs::temp_directory_path(ec) / "earthworm_jim_hd_update";
  fs::remove_all(work, ec);
  fs::create_directories(work / "files", ec);
  const fs::path zip = work / "update.zip";

  task.SetLabel("Downloading " + release.tag);
  if (std::string err = trg::DownloadFile(task, release.zip, zip.string()); !err.empty()) return err;
  if (task.cancelled()) return "Cancelled.";

  task.SetLabel("Unpacking");
  task.Progress(-1, -1);
  wchar_t system_dir[MAX_PATH];
  GetSystemDirectoryW(system_dir, MAX_PATH);
  const fs::path tar = fs::path(system_dir) / "tar.exe";
  const std::wstring cmd = L"\"" + tar.wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" +
                           (work / "files").wstring() + L"\"";
  if (const int code = RunHidden(cmd); code != 0)
    return "Could not unpack the update (tar exit code " + std::to_string(code) + ").";

  // The new program folder: wherever earthworm_jim_hd.exe is inside the zip.
  fs::path from;
  for (auto& e : fs::recursive_directory_iterator(work / "files", ec))
    if (e.path().filename() == "earthworm_jim_hd.exe") {
      from = e.path().parent_path();
      break;
    }
  if (from.empty()) return "The update does not contain earthworm_jim_hd.exe.";

  // Swap the files in. A running exe or loaded dll can be renamed but not
  // overwritten, so move each old file aside to *.old first.
  task.SetLabel("Installing");
  const fs::path to = ExeDir();
  for (auto& e : fs::directory_iterator(from, ec)) {
    if (!e.is_regular_file() || !IsProgramFile(e.path())) continue;
    const fs::path target = to / e.path().filename();
    const fs::path old = target.string() + ".old";
    fs::remove(old, ec);
    if (fs::exists(target)) {
      fs::rename(target, old, ec);
      if (ec) return "Could not replace " + target.filename().string() + ": " + ec.message();
    }
    fs::copy_file(e.path(), target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      fs::rename(old, target, ec);  // put the old one back
      return "Could not install " + target.filename().string() + ".";
    }
  }
  fs::remove_all(work, ec);
  REXLOG_INFO("EWJ: installed update {}", release.tag);
  return "";
#else
  (void)task;
  (void)release;
  return "Updates are installed automatically on Windows only.";
#endif
}

void CleanUpPreviousUpdate() {
  std::error_code ec;
  for (auto& e : fs::directory_iterator(ExeDir(), ec))
    if (e.path().extension() == ".old") fs::remove(e.path(), ec);
}

}  // namespace ewj::update
