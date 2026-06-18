#pragma once
#include "core/InputEvent.h"
#include "ocean/WaterModel.h"

// Threading contract: all functions must be called from the thread that owns
// the MTKView (the main thread), except engine_push_input, which may be called
// from any thread (the input queue is internally locked).
namespace vox {
class Engine;

// config_path: TOML file path, or "" => bundled default-config.toml.
// overrides: newline-joined "section.key=value" pairs (--set semantics), or "".
Engine* engine_create(const char* config_path, const char* overrides);
void    engine_destroy(Engine* e);

void engine_attach_view(Engine* e, void* mtk_view);   // MTKView*
void engine_resize(Engine* e, int width, int height);
void engine_render(Engine* e);                        // call from MTKViewDelegate.draw
void engine_push_input(Engine* e, InputEvent ev);
bool engine_bench_should_exit(Engine* e);

// Headless: render orthographic axis views of the settled scene to a contact-
// sheet PNG (and optional per-view PNGs), then return 0 on success. Creates and
// destroys its own Engine; never attaches a view. views_csv: comma list of
// top|bottom|front|back|side|left|right, or "all". Runs windowless.
int engine_snapshot(const char* config_path, const char* overrides,
                    const char* out_path, const char* views_csv,
                    int cell_size, bool separate, int warmup_frames);
}
