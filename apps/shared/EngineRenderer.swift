import MetalKit

final class EngineRenderer: NSObject, MTKViewDelegate {
    private var engine: OpaquePointer?

    override init() {
        super.init()
        var configPath = ""
        var overrides: [String] = []
        var args = CommandLine.arguments.dropFirst().makeIterator()
        while let a = args.next() {
            if a == "--config", let v = args.next() { configPath = v }
            else if a == "--set", let v = args.next() { overrides.append(v) }
        }
        engine = vox.engine_create(configPath, overrides.joined(separator: "\n"))
    }
    deinit { if let e = engine { vox.engine_destroy(e) } }

    func attach(view: MTKView) {
        guard let e = engine else { return }
        vox.engine_attach_view(e, Unmanaged.passUnretained(view).toOpaque())
    }
    func push(_ ev: vox.InputEvent) {
        guard let e = engine else { return }
        vox.engine_push_input(e, ev)
    }
    func draw(in view: MTKView) {
        guard let e = engine else { return }
        vox.engine_render(e)
        if vox.engine_bench_should_exit(e) { exit(0) }
    }
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard let e = engine else { return }
        vox.engine_resize(e, Int32(size.width), Int32(size.height))
    }
}
