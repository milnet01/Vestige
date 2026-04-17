/// @file main.cpp
/// @brief Entry point for the FormulaWorkbench standalone tool.
///
/// Sets up a GLFW window with an OpenGL context, initializes ImGui and ImPlot,
/// and runs the workbench main loop.
///
/// CLI mode: `--self-benchmark <csv>` runs headless against the given
/// dataset, emits a markdown leaderboard ranked by AIC, and exits —
/// never initialising GLFW. See `benchmark.h` (§3.3 of the self-learning
/// design).

#include "benchmark.h"
#include "workbench.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cstdio>
#include <string>

static void glfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main(int argc, char** argv)
{
    // §3.3 — CLI benchmark mode. Runs headless, so it must branch
    // BEFORE GLFW / ImGui / ImPlot initialisation; otherwise a
    // headless CI or SSH session would fail trying to open a window.
    if (auto rc = Vestige::runBenchmarkCli(argc, argv))
        return *rc;

    // §3.5 — PySR symbolic regression. Shells out to a Python driver;
    // optional dependency (pip install pysr). Headless.
    if (auto rc = Vestige::runSymbolicRegressionCli(argc, argv))
        return *rc;

    // §3.6 — LLM hypothesis ranking. Shells out to a Python driver
    // that calls the Anthropic API. Needs ANTHROPIC_API_KEY + the
    // anthropic SDK. Headless.
    if (auto rc = Vestige::runSuggestFormulasCli(argc, argv))
        return *rc;

    // --dump-library — emit the built-in FormulaLibrary as JSON to
    // stdout. Useful for downstream tooling; also used internally
    // as stdin to llm_rank.py when --suggest-formulas runs.
    if (auto rc = Vestige::runDumpLibraryCli(argc, argv))
        return *rc;
    // GLFW init
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
    {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    // OpenGL 4.5 core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    std::string windowTitle = std::string("Vestige FormulaWorkbench v")
        + Vestige::WORKBENCH_VERSION;
    GLFWwindow* window = glfwCreateWindow(1600, 900,
        windowTitle.c_str(), nullptr, nullptr);
    if (!window)
    {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // V-sync

    // Load OpenGL functions
    int version = gladLoadGL(glfwGetProcAddress);
    if (!version)
    {
        std::fprintf(stderr, "Failed to initialize glad\n");
        glfwTerminate();
        return 1;
    }

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    // Create workbench
    Vestige::Workbench workbench;

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        workbench.render();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
