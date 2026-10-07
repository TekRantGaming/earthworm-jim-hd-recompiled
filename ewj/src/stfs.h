// Minimal STFS (LIVE/PIRS/CON) package extractor, used by the launcher's
// installer. Port of tools/StfsExtract.cs.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ewj::stfs {

struct Progress {
  std::atomic<uint64_t> bytes_done{0};
  std::atomic<uint64_t> bytes_total{0};
  std::atomic<bool> cancel{false};
};

// Title ID from the package header, or 0 if `path` is not an STFS package.
uint32_t ReadTitleId(const std::filesystem::path& path);

// Extracts every file in the package under `out_dir`. Returns an empty string
// on success, otherwise an error message.
std::string Extract(const std::filesystem::path& package, const std::filesystem::path& out_dir,
                    Progress* progress = nullptr);

}  // namespace ewj::stfs
