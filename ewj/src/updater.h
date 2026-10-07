// Updates from GitHub releases: the launcher asks the repository's latest
// release, and when it is newer than this build, downloads its Windows zip,
// swaps in the new program files and restarts. Only the program (exe, dlls,
// text files) is replaced; the game folder, settings, saves and logs are left
// alone. Nothing is contacted unless ewj_updates allows it.

#pragma once

#include <optional>
#include <string>

namespace trg {
class Task;
}

namespace ewj::update {

// This build's version ("0.9.0"), from CMake's project(VERSION).
const char* CurrentVersion();

struct Release {
  std::string tag;  // "v0.9.1"
  std::string zip;  // download URL of the windows-x64 zip
  std::string page; // release page, for the player
};

// Asks GitHub for the latest release (blocking; call from a Task). Returns it
// when it is newer than this build and has a Windows zip, else nullopt.
// `error` gets a message when the check itself failed.
std::optional<Release> CheckLatest(std::string* error = nullptr);

// Task job: downloads the release zip, unpacks it with Windows' tar and
// replaces the program files next to this exe (the running exe and dlls are
// renamed to *.old first, which Windows allows). Returns "" on success.
std::string Install(trg::Task& task, const Release& release);

// Removes *.old files left by the previous update. Call at startup.
void CleanUpPreviousUpdate();

}  // namespace ewj::update
