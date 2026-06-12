#pragma once
#include <string>
namespace vox {
// Returns the directory for bench CSVs (cwd on macOS/tests, app Documents on iOS).
std::string bench_output_dir();
}
