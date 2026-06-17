#include "cache.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <climits>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "proc.h"

namespace forge::cache {

// === --- Internal helpers ------------------------------------------- ===
//

namespace {

constexpr char        kCacheSubdir[] = "/.cache/xrun";
constexpr char        kCacheParent[] = "/.cache";
constexpr std::size_t kSha256HexLen  = 64;
constexpr mode_t      kDirMode       = 0755;

// Returns the user's home directory. Returns nullopt (setting `*err_msg`) when
// HOME is missing or empty.
std::optional<std::string>
home_dir( std::string *err_msg )
{
        const char *home = std::getenv( "HOME" );
        if ( home != nullptr && home[0] != '\0' ) return std::string( home );
        if ( err_msg != nullptr ) *err_msg = "HOME is not set";
        return std::nullopt;
}

// Creates a single directory if absent. Returns false only on a real failure
// (an already-existing directory is success).
bool
make_dir( const std::string &path )
{
        if ( mkdir( path.c_str( ), kDirMode ) == 0 ) return true;
        return errno == EEXIST;
}

}  // namespace

// === --- Public API ------------------------------------------------- ===
//

std::optional<CacheInput>
CacheKeyFor( const std::string &src_path, std::string *err_msg )
{
        char resolved[PATH_MAX];  // PATH_MAX from <climits> (limits.h)
        if ( realpath( src_path.c_str( ), resolved ) == nullptr ) {
                if ( err_msg != nullptr )
                        *err_msg = "cannot resolve '" + src_path +
                                   "': " + std::strerror( errno );
                return std::nullopt;
        }
        struct stat st;
        if ( stat( resolved, &st ) != 0 ) {
                if ( err_msg != nullptr )
                        *err_msg = "cannot stat '" + std::string( resolved ) +
                                   "': " + std::strerror( errno );
                return std::nullopt;
        }
        CacheInput input;
        input.abs_path = resolved;
        input.mtime    = static_cast<long long>( st.st_mtime );
        return input;
}

std::optional<std::string>
ComputeChecksum( const CacheInput &input, std::string *err_msg )
{
        std::string payload =
            input.abs_path + "\n" + std::to_string( input.mtime ) + "\n";
        std::optional<proc::CaptureResult> result =
            proc::RunCapture( { "sha256sum" }, payload, err_msg );
        if ( !result.has_value( ) ) return std::nullopt;
        if ( result->exit_code != 0 ) {
                if ( err_msg != nullptr )
                        *err_msg = "sha256sum exited with code " +
                                   std::to_string( result->exit_code );
                return std::nullopt;
        }
        // sha256sum prints "<hex>  -\n"; keep the leading hex field.
        const std::string &out = result->out;
        std::size_t        end = out.find_first_of( " \t\n" );
        std::string        hex =
            ( end == std::string::npos ) ? out : out.substr( 0, end );
        if ( hex.size( ) != kSha256HexLen ) {
                if ( err_msg != nullptr )
                        *err_msg = "unexpected sha256sum output";
                return std::nullopt;
        }
        return hex;
}

std::optional<std::string>
CacheDir( std::string *err_msg )
{
        std::optional<std::string> home = home_dir( err_msg );
        if ( !home.has_value( ) ) return std::nullopt;
        return *home + kCacheSubdir;
}

std::optional<std::string>
CachePathFor( const std::string &checksum, std::string *err_msg )
{
        std::optional<std::string> dir = CacheDir( err_msg );
        if ( !dir.has_value( ) ) return std::nullopt;
        return *dir + "/" + checksum;
}

bool
IsCached( const std::string &cache_path )
{
        struct stat st;
        if ( stat( cache_path.c_str( ), &st ) != 0 ) return false;
        return S_ISREG( st.st_mode );
}

bool
EnsureCacheDir( std::string *err_msg )
{
        std::optional<std::string> home = home_dir( err_msg );
        if ( !home.has_value( ) ) return false;
        if ( !make_dir( *home + kCacheParent ) ) {
                if ( err_msg != nullptr )
                        *err_msg = "cannot create " + *home + kCacheParent +
                                   ": " + std::strerror( errno );
                return false;
        }
        std::optional<std::string> dir = CacheDir( err_msg );
        if ( !dir.has_value( ) ) return false;
        if ( !make_dir( *dir ) ) {
                if ( err_msg != nullptr )
                        *err_msg = "cannot create " + *dir + ": " +
                                   std::strerror( errno );
                return false;
        }
        return true;
}

}  // namespace forge::cache
