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

// The release list, not /releases/latest: that one skips prereleases.
constexpr const char* kReleasesApi =
    "https://api.github.com/repos/TekRantGaming/earthworm-jim-hd-recompiled/releases?per_page=20";
constexpr const char* kZipSuffix = "-windows-x64.zip";

// "v1.2.3" / "1.2.3-beta" -> {1, 2, 3}
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
  if (std::string err = trg::HttpGet(kReleasesApi, json); !err.empty()) {
    if (error) *error = err;
    return std::nullopt;
  }
  // Each release object holds "html_url", "tag_name", "draft" and its
  // "assets" (with "browser_download_url"s) before the next release's
  // "html_url"... Walk them by tag and keep the newest published one that has
  // a Windows zip.
  std::optional<Release> best;
  size_t pos = 0;
  while (true) {
    size_t tag_at = 0;
    const std::string tag = JsonString(json, "tag_name", pos, &tag_at);
    if (tag.empty()) break;
    size_t next_at = 0;
    const bool has_next = !JsonString(json, "tag_name", tag_at + 1, &next_at).empty();
    const size_t end = has_next ? next_at : json.size();
    const std::string object = json.substr(tag_at, end - tag_at);
    pos = tag_at + 1;
    if (object.find("\"draft\": true") != std::string::npos || object.find("\"draft\":true") != std::string::npos)
      continue;
    Release r;
    r.tag = tag;
    r.page = "https://github.com/TekRantGaming/earthworm-jim-hd-recompiled/releases/tag/" + tag;
    for (size_t a = 0;;) {
      size_t url_at = 0;
      const std::string url = JsonString(object, "browser_download_url", a, &url_at);
      if (url.empty()) break;
      if (url.ends_with(kZipSuffix)) {
        r.zip = url;
        break;
      }
      a = url_at + 1;
    }
    if (r.zip.empty()) continue;
    if (!best || ParseVersion(r.tag) > ParseVersion(best->tag)) best = r;
  }
  if (!best) {
    if (error && json.find("tag_name") == std::string::npos) *error = "GitHub did not return any releases.";
    return std::nullopt;
  }
  if (ParseVersion(best->tag) <= ParseVersion(CurrentVersion())) return std::nullopt;
  REXLOG_INFO("EWJ: update {} available (this is {})", best->tag, CurrentVersion());
  return best;
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
