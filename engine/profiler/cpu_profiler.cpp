/// @file cpu_profiler.cpp
/// @brief CpuProfiler implementation — scope timing with hierarchical storage.
#include "profiler/cpu_profiler.h"

namespace Vestige
{

// Global instance
static CpuProfiler s_cpuProfiler;

CpuProfiler& getCpuProfiler()
{
    return s_cpuProfiler;
}

void CpuProfiler::beginFrame()
{
    m_currentFrame.clear();
    m_scopeStack.clear();
    m_frameStart = Clock::now();
}

void CpuProfiler::endFrame()
{
    auto now = Clock::now();
    m_lastFrameTimeMs = std::chrono::duration<float, std::milli>(now - m_frameStart).count();

    // Snapshot for display (move avoids heap allocation)
    m_lastFrame = std::move(m_currentFrame);
}

void CpuProfiler::pushScope(const char* name)
{
    auto now = Clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - m_frameStart).count();

    ProfileEntry entry;
    entry.name = name;
    entry.startMs = elapsed;
    entry.parentIndex = m_scopeStack.empty() ? -1 : m_scopeStack.back();
    entry.depth = static_cast<int>(m_scopeStack.size());

    int index = static_cast<int>(m_currentFrame.size());
    m_currentFrame.push_back(entry);
    m_scopeStack.push_back(index);
}

void CpuProfiler::popScope()
{
    if (m_scopeStack.empty()) return;

    auto now = Clock::now();
    float elapsed = std::chrono::duration<float, std::milli>(now - m_frameStart).count();

    int index = m_scopeStack.back();
    m_scopeStack.pop_back();
    m_currentFrame[index].endMs = elapsed;
}

// RAII scope helper
CpuProfileScope::CpuProfileScope(const char* name)
{
    getCpuProfiler().pushScope(name);
}

CpuProfileScope::~CpuProfileScope()
{
    getCpuProfiler().popScope();
}

} // namespace Vestige
