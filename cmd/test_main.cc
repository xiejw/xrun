// test_main — end-to-end smoke test for the xrun launcher.
//
// Usage:
//   test_main [xrun-binary]
//
//   [xrun-binary]   Path to the xrun executable to exercise; defaults to
//                   ".build/xrun" (the path produced by `make`).
//
// Builds throwaway source files in a temp directory and drives them through
// xrun, verifying arg forwarding, exit-code propagation, cache hits, rebuilds
// after an edit, and clean failures. Exits 0 only if every check passes.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "cache.h"
#include "proc.h"

namespace {

constexpr char kDefaultBin[] = ".build/xrun";
constexpr int  kProgExitCode = 7;  // exit code baked into kEchoProgram
constexpr char kEchoProgram[] =
    "#include <cstdio>\n"
    "int main(int argc, char** argv) {\n"
    "  std::printf(\"hello\");\n"
    "  for (int i = 1; i < argc; ++i) std::printf(\" %s\", argv[i]);\n"
    "  std::printf(\"\\n\");\n"
    "  return 7;\n"
    "}\n";

// === --- Test harness ----------------------------------------------- ===
//

int pass_count = 0;
int fail_count = 0;

// Records one assertion and prints its outcome.
void
check( const std::string &desc, const std::string &expected,
       const std::string &actual )
{
        if ( expected == actual ) {
                std::cout << "ok:   " << desc << "\n";
                ++pass_count;
        } else {
                std::cout << "FAIL: " << desc << " (expected '" << expected
                          << "', got '" << actual << "')\n";
                ++fail_count;
        }
}

const char *
yes_no( bool v )
{
        return v ? "yes" : "no";
}

std::string
trim_trailing_newline( std::string s )
{
        while ( !s.empty( ) && ( s.back( ) == '\n' || s.back( ) == '\r' ) )
                s.pop_back( );
        return s;
}

// === --- Filesystem helpers ----------------------------------------- ===
//

bool
write_file( const std::string &path, const std::string &content )
{
        std::ofstream os( path, std::ios::binary | std::ios::trunc );
        if ( !os ) return false;
        os << content;
        return static_cast<bool>( os );
}

bool
file_exists( const std::string &path )
{
        struct stat st;
        return stat( path.c_str( ), &st ) == 0;
}

long long
file_mtime( const std::string &path )
{
        struct stat st;
        if ( stat( path.c_str( ), &st ) != 0 ) return -1;
        return static_cast<long long>( st.st_mtime );
}

// Advances the file's mtime by `delta_sec` seconds so xrun derives a new key.
bool
bump_mtime( const std::string &path, long long delta_sec )
{
        struct stat st;
        if ( stat( path.c_str( ), &st ) != 0 ) return false;
        struct timeval times[2];
        times[0].tv_sec  = st.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec  = static_cast<time_t>( st.st_mtime + delta_sec );
        times[1].tv_usec = 0;
        return utimes( path.c_str( ), times ) == 0;
}

// Reproduces the cache path xrun derives for `src`, reusing xrun's own code.
std::optional<std::string>
cache_path_for( const std::string &src )
{
        std::string                             err;
        std::optional<forge::cache::CacheInput> in =
            forge::cache::CacheKeyFor( src, &err );
        if ( !in.has_value( ) ) return std::nullopt;
        std::optional<std::string> sum =
            forge::cache::ComputeChecksum( *in, &err );
        if ( !sum.has_value( ) ) return std::nullopt;
        return forge::cache::CachePathFor( *sum, &err );
}

// === --- Driving xrun ----------------------------------------------- ===
//

struct RunResult {
        int         exit_code;
        std::string out;  // captured stdout
};

// Runs `bin` with `args`, returning its exit code and captured stdout. When
// `quiet` is set, the child's stderr is routed to /dev/null so that expected
// failures (compiler diagnostics, missing-file errors) don't clutter the log.
RunResult
run_xrun( const std::string &bin, const std::vector<std::string> &args,
          bool quiet )
{
        std::vector<std::string> argv;
        argv.reserve( args.size( ) + 1 );
        argv.push_back( bin );
        for ( const std::string &a : args ) argv.push_back( a );

        int saved_err = -1;
        int dev_null  = -1;
        if ( quiet ) {
                dev_null = open( "/dev/null", O_WRONLY );
                if ( dev_null >= 0 ) {
                        saved_err = dup( STDERR_FILENO );
                        dup2( dev_null, STDERR_FILENO );
                }
        }

        std::string                               err;
        std::optional<forge::proc::CaptureResult> cap =
            forge::proc::RunCapture( argv, "", &err );

        if ( saved_err >= 0 ) {
                dup2( saved_err, STDERR_FILENO );
                close( saved_err );
        }
        if ( dev_null >= 0 ) close( dev_null );

        RunResult r;
        r.exit_code = cap.has_value( ) ? cap->exit_code : -1;
        r.out       = cap.has_value( ) ? cap->out : std::string( );
        return r;
}

// === --- Test cases ------------------------------------------------- ===
//

// First run compiles, forwards args, propagates the exit code, and caches.
void
test_arg_forwarding_and_cache( const std::string &bin, const std::string &tmp )
{
        std::string src = tmp + "/hello.cc";
        write_file( src, kEchoProgram );

        RunResult r = run_xrun( bin, { src, "one", "two" }, false );
        check( "args forwarded", "hello one two",
               trim_trailing_newline( r.out ) );
        check( "exit code propagated", std::to_string( kProgExitCode ),
               std::to_string( r.exit_code ) );

        std::optional<std::string> cache = cache_path_for( src );
        check( "cache file created", "yes",
               yes_no( cache.has_value( ) && file_exists( *cache ) ) );
}

// A second run of an unchanged source is a cache hit: no rebuild.
void
test_cache_hit( const std::string &bin, const std::string &tmp )
{
        std::string src = tmp + "/hit.cc";
        write_file( src, kEchoProgram );

        run_xrun( bin, { src }, false );  // populate the cache
        std::optional<std::string> cache = cache_path_for( src );
        if ( !cache.has_value( ) || !file_exists( *cache ) ) {
                check( "cache hit (no rebuild)", "built", "missing" );
                return;
        }
        long long before = file_mtime( *cache );
        run_xrun( bin, { src }, false );
        long long after = file_mtime( *cache );
        check( "cache hit (no rebuild)", std::to_string( before ),
               std::to_string( after ) );
}

// Touching the source changes its mtime, hence its key, hence a rebuild.
void
test_rebuild_on_edit( const std::string &bin, const std::string &tmp )
{
        std::string src = tmp + "/edit.cc";
        write_file( src, kEchoProgram );

        run_xrun( bin, { src }, false );
        std::optional<std::string> key1 = cache_path_for( src );

        bump_mtime( src, 1 );
        run_xrun( bin, { src }, false );
        std::optional<std::string> key2 = cache_path_for( src );

        check( "new key after edit", "yes",
               yes_no( key1.has_value( ) && key2.has_value( ) &&
                       *key1 != *key2 ) );
        check( "rebuilt under new key", "yes",
               yes_no( key2.has_value( ) && file_exists( *key2 ) ) );
}

// A compile error is a nonzero exit and leaves nothing cached.
void
test_compile_error( const std::string &bin, const std::string &tmp )
{
        std::string src = tmp + "/bad.cc";
        write_file( src, "int main() { return frobnicate; }\n" );

        RunResult r = run_xrun( bin, { src }, true );
        check( "compile error nonzero", "yes", yes_no( r.exit_code != 0 ) );

        std::optional<std::string> cache = cache_path_for( src );
        check( "compile error uncached", "no",
               yes_no( cache.has_value( ) && file_exists( *cache ) ) );
}

// A missing source file is a clean nonzero error.
void
test_missing_file( const std::string &bin, const std::string &tmp )
{
        std::string src = tmp + "/nope.cc";
        RunResult   r   = run_xrun( bin, { src }, true );
        check( "missing file nonzero", "yes", yes_no( r.exit_code != 0 ) );
}

// === --- Temp directory --------------------------------------------- ===
//

// Creates a unique temp directory, or an empty string on failure.
std::string
make_temp_dir( )
{
        char        tmpl[] = "/tmp/xrun_test.XXXXXX";
        const char *dir    = mkdtemp( tmpl );
        return dir == nullptr ? std::string( ) : std::string( dir );
}

void
remove_temp_dir( const std::string &dir )
{
        std::string err;
        forge::proc::RunWait( { "rm", "-rf", dir }, &err );
}

}  // namespace

int
main( int argc, char **argv )
{
        std::string bin = ( argc > 1 ) ? argv[1] : kDefaultBin;

        std::string tmp = make_temp_dir( );
        if ( tmp.empty( ) ) {
                std::cerr << "test_main: cannot create temp dir\n";
                return 1;
        }

        test_arg_forwarding_and_cache( bin, tmp );
        test_cache_hit( bin, tmp );
        test_rebuild_on_edit( bin, tmp );
        test_compile_error( bin, tmp );
        test_missing_file( bin, tmp );

        remove_temp_dir( tmp );

        std::cout << "----\n"
                  << pass_count << " passed, " << fail_count << " failed\n";
        return fail_count == 0 ? 0 : 1;
}
