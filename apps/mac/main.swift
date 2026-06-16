import Foundation

// Intercept the headless snapshot before SwiftUI's app lifecycle starts.
let args = CommandLine.arguments
if args.contains("--snapshot") {
    var configPath = "", out = "./snapshot.png", views = "top,front,side"
    var size: Int32 = 512, warmup: Int32 = 90
    var separate = false
    var overrides: [String] = []
    var it = args.dropFirst().makeIterator()
    while let a = it.next() {
        switch a {
        case "--config":   configPath = it.next() ?? configPath
        case "--set":      if let v = it.next() { overrides.append(v) }
        case "--out":      out = it.next() ?? out
        case "--views":    views = it.next() ?? views
        case "--size":     size = Int32(it.next() ?? "") ?? size
        case "--warmup":   warmup = Int32(it.next() ?? "") ?? warmup
        case "--separate": separate = true
        default: break
        }
    }
    let rc = vox.engine_snapshot(configPath, overrides.joined(separator: "\n"),
                                 out, views, size, separate, warmup)
    exit(rc)
}

VoxOceanApp.main()
