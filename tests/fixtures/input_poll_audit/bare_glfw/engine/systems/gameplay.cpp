// Fixture for input_poll_audit check 1 (bare-glfw-poll).
// Gameplay code calling GLFW directly. The audit MUST flag this.
// Not compiled.
void tick(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { moveForward(); }
}
