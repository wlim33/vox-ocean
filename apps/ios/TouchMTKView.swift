import MetalKit

final class TouchMTKView: MTKView {
    weak var renderer: EngineRenderer?
    private var last: CGPoint?

    private func event(_ kind: vox.InputKind) -> vox.InputEvent {
        var e = vox.InputEvent(); e.kind = kind; return e
    }
    override func touchesBegan(_ touches: Set<UITouch>, with: UIEvent?) {
        last = touches.first?.location(in: self)
        renderer?.push(event(.MouseDown))
    }
    override func touchesMoved(_ touches: Set<UITouch>, with: UIEvent?) {
        guard let p = touches.first?.location(in: self), let l = last else { return }
        var e = event(.MouseMove); e.x = Float(p.x - l.x); e.y = Float(p.y - l.y)
        renderer?.push(e); last = p
    }
    override func touchesEnded(_ touches: Set<UITouch>, with: UIEvent?) {
        last = nil; renderer?.push(event(.MouseUp))
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with: UIEvent?) {
        last = nil; renderer?.push(event(.MouseUp))
    }
    @objc func pinch(_ g: UIPinchGestureRecognizer) {
        var e = event(.Scroll); e.scroll = Float(g.scale - 1.0) * 2.0
        renderer?.push(e); g.scale = 1.0
    }
}
