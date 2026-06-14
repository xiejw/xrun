#include "cache.h"

#include <climits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "proc.h"

namespace forge::cache {

// === --- Internal helpers ------------------------------------------- ===
//

namespace {

constexpr char kCacheSubdir[] = "/.cache/xrun";
constexpr char kCacheParent[] = "/.cache";
constexpr std::size_t kSha256HexLen = 64;
constexpr mode_t kDirMode = 0755;

// Returns the user's home directory, or an empty string if unset.
std::string home_dir() {
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') return std::string(home);
  return std::string();
}

// Creates a single directory if absent. Returns false only on a real failure
// (an already-existing directory is success).
bool make_dir(const std::string& path) {
  if (mkdir(path.c_str(), kDirMode) == 0) return true;
  return errno == EEXIST;
}

}  // namespace

// === --- Public API ------------------------------------------------- ===
//

std::optional<CacheInput> CacheKeyFor(const std::string& src_path) {
  char resolved[PATH_MAX];
  if (realpath(src_path.c_str(), resolved) == nullptr) {
    std::cerr << "xrun: cannot resolve '" << src_path
              << "': " << std::strerror(errno) << "\n";
    return std::nullopt;
  }
  struct stat st;
  if (stat(resolved, &st) != 0) {
    std::cerr << "xrun: cannot stat '" << resolved
              << "': " << std::strerror(errno) << "\n";
    return std::nullopt;
  }
  CacheInput input;
  input.abs_path = resolved;
  input.mtime = static_cast<long long>(st.st_mtime);
  return input;
}

std::optional<std::string> ComputeChecksum(const CacheInput& input) {
  std::string payload =
      input.abs_path + "\n" + std::to_string(input.mtime) + "\n";
  std::optional<proc::CaptureResult> result =
      proc::RunCapture({"sha256sum"}, payload);
  if (!result.has_value() || result->exit_code != 0) {
    std::cerr << "xrun: sha256sum failed\n";
    return std::nullopt;
  }
  // sha256sum prints "<hex>  -\n"; keep the leading hex field.
  const std::string& out = result->out;
  std::size_t end = out.find_first_of(" \t\n");
  std::string hex = (end == std::string::npos) ? out : out.substr(0, end);
  if (hex.size() != kSha256HexLen) {
    std::cerr << "xrun: unexpected sha256sum output\n";
    return std::nullopt;
  }
  return hex;
}

std::string CacheDir() { return home_dir() + kCacheSubdir; }

std::string CachePathFor(const std::string& checksum) {
  return CacheDir() + "/" + checksum;
}

bool IsCached(const std::string& cache_path) {
  struct stat st;
  if (stat(cache_path.c_str(), &st) != 0) return false;
  return S_ISREG(st.st_mode);
}

bool EnsureCacheDir() {
  std::string home = home_dir();
  if (home.empty()) {
    std::cerr << "xrun: HOME is not set\n";
    return false;
  }
  if (!make_dir(home + kCacheParent)) {
    std::cerr << "xrun: cannot create " << home << kCacheParent << ": "
              << std::strerror(errno) << "\n";
    return false;
  }
  if (!make_dir(CacheDir())) {
    std::cerr << "xrun: cannot create " << CacheDir() << ": "
              << std::strerror(errno) << "\n";
    return false;
  }
  return true;
}

}  // namespace forge::cache
