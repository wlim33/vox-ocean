import MetalKit

final class InteractiveMTKView: MTKView {
    weak var renderer: EngineRenderer?
    override var acceptsFirstResponder: Bool { true }

    private func event(_ kind: vox.InputKind) -> vox.InputEvent {
        var e = vox.InputEvent(); e.kind = kind; return e
    }
    override func mouseDown(with: NSEvent)  { renderer?.push(event(.MouseDown)) }
    override func mouseUp(with: NSEvent)    { renderer?.push(event(.MouseUp)) }
    override func rightMouseDown(with ev: NSEvent) {
        let pt = convert(ev.locationInWindow, from: nil)   // view points, bottom-left origin
        let scale = Float(window?.backingScaleFactor ?? 1.0)
        var e = event(.Pick)
        e.x = Float(pt.x) * scale
        e.y = Float(bounds.height - pt.y) * scale           // flip to top-left, then to pixels
        renderer?.push(e)
    }
    override func mouseDragged(with ev: NSEvent) {
        var e = event(.MouseMove); e.x = Float(ev.deltaX); e.y = Float(ev.deltaY)
        renderer?.push(e)
    }
    override func scrollWheel(with ev: NSEvent) {
        var e = event(.Scroll); e.scroll = Float(ev.scrollingDeltaY) * 0.05
        renderer?.push(e)
    }
}
