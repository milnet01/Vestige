// Fixture pinning the POSITIVE half: the one module allowed to make a
// raw poll is exempt, so the audit reports clean. Without this, a check
// that flagged everything would look identical to a correct one.
// Not compiled.
bool InputManager::isKeyDown(int keyCode) const
{
    return glfwGetKey(m_window, keyCode) == GLFW_PRESS;
}
