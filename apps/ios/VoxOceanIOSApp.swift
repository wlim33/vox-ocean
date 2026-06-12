import SwiftUI
import MetalKit

@main
struct VoxOceanIOSApp: App {
    var body: some Scene { WindowGroup { MetalSurface().ignoresSafeArea() } }
}

struct MetalSurface: UIViewRepresentable {
    func makeCoordinator() -> EngineRenderer { EngineRenderer() }
    func makeUIView(context: Context) -> TouchMTKView {
        let v = TouchMTKView(frame: .zero)
        v.renderer = context.coordinator
        v.delegate = context.coordinator
        v.isMultipleTouchEnabled = true
        v.addGestureRecognizer(UIPinchGestureRecognizer(target: v, action: #selector(TouchMTKView.pinch(_:))))
        context.coordinator.attach(view: v)
        return v
    }
    // No SwiftUI state flows into the view; the engine drives itself per frame.
    func updateUIView(_ v: TouchMTKView, context: Context) {}
}
