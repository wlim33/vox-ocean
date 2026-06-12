#pragma once
#include "core/InputEvent.h"

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
}
