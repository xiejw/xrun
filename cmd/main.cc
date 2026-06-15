// xrun — a go-run-style launcher for single-file C++ programs.
//
// Usage:
//   xrun <file.cc> [args...]
//
//   <file.cc>   C++ source to compile (cached under ~/.cache/xrun) and run.
//   [args...]   Forwarded verbatim to the compiled program.
//
// The program's exit code is propagated as xrun's exit code.

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cache.h"
#include "proc.h"

namespace {

constexpr int kUsageError    = 2;
constexpr int kInternalError = 1;

// Compiles `src_abs_path` into `cache_path` atomically: build to a temp file,
// then rename into place only on a clean compile. Returns true on success.
bool
compile_source( const std::string &src_abs_path, const std::string &cache_path )
{
        std::string tmp_path =
            cache_path + ".tmp." + std::to_string( getpid( ) );
        int rc = forge::proc::RunWait(
            { "c++", "-std=c++17", src_abs_path, "-o", tmp_path } );
        if ( rc != 0 ) {
                unlink(
                    tmp_path
                        .c_str( ) );  // compiler already printed diagnostics
                return false;
        }
        if ( rename( tmp_path.c_str( ), cache_path.c_str( ) ) != 0 ) {
                std::cerr << "xrun: cannot place cached binary: "
                          << std::strerror( errno ) << "\n";
                unlink( tmp_path.c_str( ) );
                return false;
        }
        return true;
}

}  // namespace

int
main( int argc, char **argv )
{
        if ( argc < 2 ) {
                std::cerr << "usage: xrun <file.cc> [args...]\n";
                return kUsageError;
        }

        std::string src = argv[1];

        std::optional<forge::cache::CacheInput> input =
            forge::cache::CacheKeyFor( src );
        if ( !input.has_value( ) ) return kInternalError;

        std::optional<std::string> checksum =
            forge::cache::ComputeChecksum( *input );
        if ( !checksum.has_value( ) ) return kInternalError;

        std::string cache_path = forge::cache::CachePathFor( *checksum );

        if ( !forge::cache::IsCached( cache_path ) ) {
                if ( !forge::cache::EnsureCacheDir( ) ) return kInternalError;
                if ( !compile_source( input->abs_path, cache_path ) )
                        return kInternalError;
        }

        // Run the cached program: argv[0] = the binary, then the forwarded
        // args.
        std::vector<std::string> run_argv;
        run_argv.reserve( static_cast<std::size_t>( argc - 1 ) );
        run_argv.push_back( cache_path );
        for ( int i = 2; i < argc; ++i ) run_argv.push_back( argv[i] );

        forge::proc::RunExec( cache_path, run_argv );
        return kInternalError;  // only reached if exec failed
}
