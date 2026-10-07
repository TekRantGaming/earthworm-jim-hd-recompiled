#include "stfs.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace ewj::stfs {
namespace {

constexpr uint64_t kBlocksPerLevel[3] = {0xAA, 0x70E4, 0x4AF768};

class Package {
 public:
  bool Open(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    data_.resize(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data_.data()), data_.size());
    if (!f || data_.size() < 0xB000) return false;
    if (!IsMagic("LIVE") && !IsMagic("PIRS") && !IsMagic("CON ")) return false;
    base_ = (uint64_t(BE32(0x340)) + 0xFFF) & ~uint64_t(0xFFF);
    shift_ = (data_[0x37B] & 1) ? 0 : 1;  // read-only packages have one hash table per level
    return true;
  }

  uint32_t TitleId() const { return BE32(0x360); }

  std::string ExtractAll(const std::filesystem::path& out_dir, Progress* progress) {
    struct Entry {
      std::string name;
      uint8_t flags;
      uint32_t valid_blocks, start_block, size;
      uint16_t parent;
    };
    std::vector<Entry> entries;
    uint64_t block = LE24(0x37E);
    for (int i = 0, n = LE16(0x37C); i < n; ++i) {
      const uint64_t off = BlockToOffset(block);
      if (off + 0x1000 > data_.size()) return "Corrupt package (file table out of range)";
      for (int e = 0; e < 0x40; ++e) {
        const uint64_t p = off + e * 0x40;
        const uint8_t flags = data_[p + 0x28];
        const size_t len = flags & 0x3F;
        entries.push_back({std::string(reinterpret_cast<const char*>(&data_[p]), len), flags,
                           LE24(p + 0x29), LE24(p + 0x2F), BE32(p + 0x34),
                           static_cast<uint16_t>(BE16(p + 0x32))});
      }
      block = NextBlock(block);
    }

    std::unordered_map<size_t, std::filesystem::path> paths;
    auto path_of = [&](auto&& self, size_t i) -> std::filesystem::path {
      if (auto it = paths.find(i); it != paths.end()) return it->second;
      const auto& e = entries[i];
      std::filesystem::path p = (e.parent == 0xFFFF || e.parent >= entries.size())
                                    ? std::filesystem::path(e.name)
                                    : self(self, e.parent) / e.name;
      return paths[i] = p;
    };

    if (progress) {
      uint64_t total = 0;
      for (auto& e : entries)
        if (!e.name.empty() && !(e.flags & 0x80)) total += e.size;
      progress->bytes_total = total;
      progress->bytes_done = 0;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      if (e.name.empty()) continue;
      if (progress && progress->cancel) return "Cancelled";
      const auto full = out_dir / path_of(path_of, i);
      std::error_code ec;
      if (e.flags & 0x80) {
        std::filesystem::create_directories(full, ec);
        continue;
      }
      std::filesystem::create_directories(full.parent_path(), ec);
      std::ofstream out(full, std::ios::binary | std::ios::trunc);
      if (!out) return "Cannot write " + full.string();
      const bool contiguous = (e.flags & 0x40) != 0;
      uint64_t remaining = e.size, b = e.start_block;
      for (uint32_t k = 0; k < e.valid_blocks && remaining > 0; ++k) {
        const uint64_t off = BlockToOffset(b);
        const uint64_t n = std::min<uint64_t>(0x1000, remaining);
        if (off + n > data_.size()) return "Corrupt package (data out of range)";
        out.write(reinterpret_cast<const char*>(&data_[off]), static_cast<std::streamsize>(n));
        remaining -= n;
        if (progress) progress->bytes_done += n;
        b = contiguous ? b + 1 : NextBlock(b);
      }
      if (!out) return "Write failed: " + full.string();
    }
    return {};
  }

 private:
  bool IsMagic(const char* m) const { return std::equal(m, m + 4, data_.begin()); }
  uint32_t BE32(uint64_t o) const {
    return uint32_t(data_[o]) << 24 | uint32_t(data_[o + 1]) << 16 | uint32_t(data_[o + 2]) << 8 | data_[o + 3];
  }
  uint32_t BE24(uint64_t o) const { return uint32_t(data_[o]) << 16 | uint32_t(data_[o + 1]) << 8 | data_[o + 2]; }
  uint32_t BE16(uint64_t o) const { return uint32_t(data_[o]) << 8 | data_[o + 1]; }
  uint32_t LE24(uint64_t o) const { return data_[o] | uint32_t(data_[o + 1]) << 8 | uint32_t(data_[o + 2]) << 16; }
  uint32_t LE16(uint64_t o) const { return data_[o] | uint32_t(data_[o + 1]) << 8; }

  uint64_t BlockToOffset(uint64_t block) const {
    uint64_t b = block;
    for (uint64_t level : kBlocksPerLevel) {
      b += ((block + level) / level) << shift_;
      if (block < level) break;
    }
    return base_ + (b << 12);
  }

  // Raw block number of the level-0 hash table covering `block`.
  uint64_t HashBlockNumber(uint64_t block) const {
    const uint64_t step0 = shift_ == 0 ? 0xAB : 0xAC;
    if (block < 0xAA) return 0;
    uint64_t num = (block / 0xAA) * step0;
    num += ((block / 0x70E4) + 1) << shift_;
    if (block / 0x70E4 == 0) return num;
    return num + (uint64_t(1) << shift_);
  }

  uint64_t NextBlock(uint64_t block) const {
    const uint64_t entry = base_ + (HashBlockNumber(block) << 12) + (block % 0xAA) * 0x18;
    return entry + 0x18 <= data_.size() ? BE24(entry + 0x15) : 0xFFFFFF;
  }

  std::vector<uint8_t> data_;
  uint64_t base_ = 0;
  int shift_ = 0;
};

}  // namespace

uint32_t ReadTitleId(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  uint8_t h[0x364];
  if (!f.read(reinterpret_cast<char*>(h), sizeof(h))) return 0;
  const bool magic = std::equal(h, h + 4, "LIVE") || std::equal(h, h + 4, "PIRS") ||
                     std::equal(h, h + 4, "CON ");
  if (!magic) return 0;
  return uint32_t(h[0x360]) << 24 | uint32_t(h[0x361]) << 16 | uint32_t(h[0x362]) << 8 | h[0x363];
}

std::string Extract(const std::filesystem::path& package, const std::filesystem::path& out_dir,
                    Progress* progress) {
  Package pkg;
  if (!pkg.Open(package)) return "Not a valid Xbox 360 package: " + package.filename().string();
  return pkg.ExtractAll(out_dir, progress);
}

}  // namespace ewj::stfs
