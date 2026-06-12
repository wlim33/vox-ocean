#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "bench/BenchPaths.h"
#import <Foundation/Foundation.h>
namespace vox {
std::string bench_output_dir() {
    NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    if (dirs.count == 0) return ".";
    return std::string(dirs.firstObject.UTF8String);
}
}
#endif
