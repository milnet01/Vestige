# Vestige 3D Engine

## Project Overview
Vestige is a 3D exploration engine built in C++17 with OpenGL 4.5. Its primary purpose is creating first-person architectural walkthroughs, starting with biblical structures (Tabernacle/Tent of Meeting, Solomon's Temple).

## Technology Stack
- **Language:** C++17
- **Graphics API:** OpenGL 4.5
- **Windowing:** GLFW
- **Math:** GLM
- **Build System:** CMake
- **Platforms:** Linux, Windows
- **Input:** Keyboard, mouse, Xbox/PlayStation controllers (via GLFW)
- **Testing:** Google Test (from the start)

## Development Hardware
- CPU: AMD Ryzen 5 5600 (6-core/12-thread)
- GPU: AMD Radeon RX 6600 (RDNA2, OpenGL 4.6, Vulkan 1.3, basic HW ray tracing)
- RAM: 32GB
- OS: Linux (Ubuntu-based)

## Performance Target
- **Minimum 60 FPS** at all times — this is a hard requirement
- Profile before optimizing — measure, don't guess
- Prefer GPU-efficient approaches (batching, instancing, frustum culling)

## Future Goals
- Vulkan rendering backend
- Ray tracing (rudimentary, then hardware-accelerated)
- Steam distribution
- Game development (beyond exploration experiences)

## Key Rules
1. **Plan before coding.** The user wants thorough documentation and discussion before implementation.
2. **Follow coding standards strictly.** See CODING_STANDARDS.md — all naming conventions, formatting, and structure rules must be followed exactly.
3. **Explain concepts clearly.** The user is learning — no assumed knowledge of graphics programming or C++.
4. **Keep it modular.** Every subsystem should be independent and extensible. New features slot in without breaking existing code.
5. **No over-engineering.** Start simple, add complexity only when needed.
6. **Security first.** See SECURITY.md — memory safety, input validation, and secure coding practices are mandatory.
7. **60 FPS minimum.** All rendering and logic must sustain at least 60 frames per second.

## Architecture
Subsystem + Event Bus pattern. See ARCHITECTURE.md for full details.

## Coding Standards Summary
- Files: `snake_case.cpp` / `snake_case.h`
- Classes: `PascalCase`
- Functions: `camelCase`
- Members: `m_camelCase`
- Constants: `UPPER_SNAKE_CASE`
- Braces: Allman style (opening brace on new line)
- Indentation: 4 spaces
- One class per file
- `#pragma once` for include guards
- See CODING_STANDARDS.md for complete standards
