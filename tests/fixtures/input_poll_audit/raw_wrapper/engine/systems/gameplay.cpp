// Fixture for input_poll_audit check 2 (raw-poll-wrapper).
// Gameplay code polling a raw-poll wrapper instead of an action.
// The audit MUST flag this. Not compiled.
void tick(InputManager& input)
{
    if (input.isKeyDown(GLFW_KEY_W)) { moveForward(); }
}
