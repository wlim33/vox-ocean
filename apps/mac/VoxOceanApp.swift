import SwiftUI
import MetalKit

@main
struct VoxOceanApp: App {
    var body: some Scene {
        WindowGroup { MetalSurface().frame(minWidth: 960, minHeight: 540) }
    }
}

struct MetalSurface: NSViewRepresentable {
    func makeCoordinator() -> EngineRenderer { EngineRenderer() }
    func makeNSView(context: Context) -> InteractiveMTKView {
        let v = InteractiveMTKView(frame: .zero)
        v.device = MTLCreateSystemDefaultDevice()
        v.renderer = context.coordinator
        v.delegate = context.coordinator
        context.coordinator.attach(view: v)
        DispatchQueue.main.async { v.window?.makeFirstResponder(v) }
        return v
    }
    func updateNSView(_ v: InteractiveMTKView, context: Context) {}
}
