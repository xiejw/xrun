#ifndef FORGE_CACHE_H_
#define FORGE_CACHE_H_

#include <optional>
#include <string>

// === --- forge::cache — source identity & cache lookup -------------- ===
//
// Maps a source file to a content-addressed cache entry under ~/.cache/xrun.

namespace forge::cache {

// Identity of a source file, from which its cache key is derived.
struct CacheInput {
  std::string abs_path;  // canonical absolute path (realpath)
  long long mtime;       // last modified time, seconds since epoch
};

// Resolves the absolute path and mtime of `src_path`. Returns nullopt (and
// prints an error) if the file does not exist or cannot be stat'd.
std::optional<CacheInput> CacheKeyFor(const std::string& src_path);

// Computes the SHA-256 hex digest of "<abs_path>\n<mtime>\n" by invoking the
// `sha256sum` program. Returns nullopt on failure.
std::optional<std::string> ComputeChecksum(const CacheInput& input);

// Returns the cache directory: ~/.cache/xrun.
std::string CacheDir();

// Returns the full cache path for a checksum: ~/.cache/xrun/<checksum>.
std::string CachePathFor(const std::string& checksum);

// Returns true if `cache_path` exists and is a regular file.
bool IsCached(const std::string& cache_path);

// Creates ~/.cache and ~/.cache/xrun if missing. Returns false on failure.
bool EnsureCacheDir();

}  // namespace forge::cache

#endif  // FORGE_CACHE_H_
