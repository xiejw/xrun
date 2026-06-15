#ifndef FORGE_PROC_H_
#define FORGE_PROC_H_

#include <optional>
#include <string>
#include <vector>

// === --- forge::proc — subprocess helpers --------------------------- ===
//
// Thin wrappers over fork/exec. No shell is involved, so arguments are passed
// literally and never subject to shell quoting or injection.

namespace forge::proc {

// Result of a captured subprocess run.
struct CaptureResult {
        int         exit_code;
        std::string out;  // everything the child wrote to stdout
};

// Runs `argv` (argv[0] is the program, looked up on PATH), feeding `stdin_data`
// to its stdin and capturing its stdout. Returns nullopt on spawn failure.
std::optional<CaptureResult> RunCapture( const std::vector<std::string> &argv,
                                         const std::string &stdin_data );

// Runs `argv` to completion, inheriting this process's stdio. Returns the
// child's exit code, or -1 on spawn failure / abnormal termination.
int RunWait( const std::vector<std::string> &argv );

// Replaces the current process image with `path`, invoked as `argv`. Returns
// only on failure (after printing an error).
void RunExec( const std::string &path, const std::vector<std::string> &argv );

}  // namespace forge::proc

#endif  // FORGE_PROC_H_
