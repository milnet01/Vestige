// Fixture pinning that the exemption prefix "engine/core/input_manager."
// is EXACT and does not swallow a neighbouring filename. This file's
// name shares the stem but not the trailing dot, so it is NOT exempt
// and the audit MUST flag it. A loose prefix here would let any
// input_manager*-named file hide a raw poll -- the file-scoped-exemption
// trap docs/engine/input/spec.md section 12 documents. Not compiled.
void helper(InputManager& input)
{
    if (input.isMouseButtonDown(0)) { fire(); }
}
