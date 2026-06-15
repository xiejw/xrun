#include "proc.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace forge::proc {

// === --- Internal helpers ------------------------------------------- ===
//

namespace {

constexpr int         kReadEnd         = 0;
constexpr int         kWriteEnd        = 1;
constexpr std::size_t kReadChunk       = 4096;
constexpr int         kExecFailureCode = 127;

// Builds a NULL-terminated argv array of C strings referencing `args`. The
// returned pointers are valid only while `args` is alive and unmodified.
std::vector<char *>
build_argv( const std::vector<std::string> &args )
{
        std::vector<char *> out;
        out.reserve( args.size( ) + 1 );
        for ( const std::string &a : args ) {
                out.push_back( const_cast<char *>( a.c_str( ) ) );
        }
        out.push_back( nullptr );
        return out;
}

// Reports a failed exec from within the child and exits without unwinding.
[[noreturn]] void
child_exec_failed( const std::string &prog )
{
        std::cerr << "xrun: exec " << prog
                  << " failed: " << std::strerror( errno ) << "\n";
        _exit( kExecFailureCode );
}

}  // namespace

// === --- Public API ------------------------------------------------- ===
//

std::optional<CaptureResult>
RunCapture( const std::vector<std::string> &argv,
            const std::string              &stdin_data )
{
        if ( argv.empty( ) ) return std::nullopt;

        int in_pipe[2];
        int out_pipe[2];
        if ( pipe( in_pipe ) != 0 ) {
                std::cerr << "xrun: pipe failed: " << std::strerror( errno )
                          << "\n";
                return std::nullopt;
        }
        if ( pipe( out_pipe ) != 0 ) {
                std::cerr << "xrun: pipe failed: " << std::strerror( errno )
                          << "\n";
                close( in_pipe[kReadEnd] );
                close( in_pipe[kWriteEnd] );
                return std::nullopt;
        }

        pid_t pid = fork( );
        if ( pid < 0 ) {
                std::cerr << "xrun: fork failed: " << std::strerror( errno )
                          << "\n";
                close( in_pipe[kReadEnd] );
                close( in_pipe[kWriteEnd] );
                close( out_pipe[kReadEnd] );
                close( out_pipe[kWriteEnd] );
                return std::nullopt;
        }

        if ( pid == 0 ) {
                // Child: stdin <- in_pipe, stdout -> out_pipe.
                dup2( in_pipe[kReadEnd], STDIN_FILENO );
                dup2( out_pipe[kWriteEnd], STDOUT_FILENO );
                close( in_pipe[kReadEnd] );
                close( in_pipe[kWriteEnd] );
                close( out_pipe[kReadEnd] );
                close( out_pipe[kWriteEnd] );
                std::vector<char *> cargv = build_argv( argv );
                execvp( cargv[0], cargv.data( ) );
                child_exec_failed( argv[0] );
        }

        // Parent: keep the writing end of stdin and reading end of stdout.
        close( in_pipe[kReadEnd] );
        close( out_pipe[kWriteEnd] );

        // Feed stdin, then close to signal EOF. Inputs are small, so a plain
        // write-then-read sequence cannot deadlock on a full pipe.
        std::size_t written = 0;
        while ( written < stdin_data.size( ) ) {
                ssize_t n =
                    write( in_pipe[kWriteEnd], stdin_data.data( ) + written,
                           stdin_data.size( ) - written );
                if ( n <= 0 ) {
                        if ( n < 0 && errno == EINTR ) continue;
                        break;
                }
                written += static_cast<std::size_t>( n );
        }
        close( in_pipe[kWriteEnd] );

        // Drain stdout to EOF.
        std::string out;
        char        buf[kReadChunk];
        for ( ;; ) {
                ssize_t n = read( out_pipe[kReadEnd], buf, sizeof( buf ) );
                if ( n < 0 ) {
                        if ( errno == EINTR ) continue;
                        break;
                }
                if ( n == 0 ) break;
                out.append( buf, static_cast<std::size_t>( n ) );
        }
        close( out_pipe[kReadEnd] );

        int status = 0;
        while ( waitpid( pid, &status, 0 ) < 0 && errno == EINTR ) {
        }

        CaptureResult result;
        result.out       = std::move( out );
        result.exit_code = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
        return result;
}

int
RunWait( const std::vector<std::string> &argv )
{
        if ( argv.empty( ) ) return -1;

        pid_t pid = fork( );
        if ( pid < 0 ) {
                std::cerr << "xrun: fork failed: " << std::strerror( errno )
                          << "\n";
                return -1;
        }
        if ( pid == 0 ) {
                std::vector<char *> cargv = build_argv( argv );
                execvp( cargv[0], cargv.data( ) );
                child_exec_failed( argv[0] );
        }

        int status = 0;
        while ( waitpid( pid, &status, 0 ) < 0 && errno == EINTR ) {
        }
        return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
}

void
RunExec( const std::string &path, const std::vector<std::string> &argv )
{
        std::vector<char *> cargv = build_argv( argv );
        execv( path.c_str( ), cargv.data( ) );
        std::cerr << "xrun: exec " << path
                  << " failed: " << std::strerror( errno ) << "\n";
}

}  // namespace forge::proc
