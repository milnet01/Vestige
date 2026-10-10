---
paths: ["assets/shaders/**","tests/test_shader_copies_registry.cpp"]
---

# Shader source sharing

Moved verbatim from `CLAUDE.md` on 2026-10-10.

**There is no GLSL `#include`** — the shader loader does no preprocessing, so two shaders cannot share source the obvious way. Two options, in this order: (a) **link the same stage file into both programs** where the stages genuinely agree — the grass shadow caster pairs the unmodified `grass.vert.glsl` with `grass_shadow.frag.glsl`, so there is one blade generator and drift is impossible (3D_E-0042); (b) **copy the function and register the copy** in `tests/test_shader_copies_registry.cpp` where the stages must differ. That test fails on any function defined in two shaders without an entry, and on any registered copy that drifts; a copy that must differ is listed with its reason. Never copy without registering it. Drift fails at load time, never at build time: the loader logs an error, and a shadow-caster program logs a warning and carries on, so the feature just silently stops working.
