// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file frameledger_header_check.cpp
/// @brief Compiles the vendored frameledger.h as C++17 at the strictest warning
///        level, as promised to frameledger (3D_E-0786), and writes one row.

#include <frameledger.h>

#include <cstdio>
#include <cstring>

int main()
{
    static char buffer[4096];
    fled_writer writer;
    if (fled_open(&writer, buffer, sizeof buffer, "check", "{\"schema\":1}", nullptr) != FLED_OK
        || fled_row(&writer, nullptr, 0, "frame", "wall", 0, 16.5) != FLED_OK
        || fled_close(&writer, 1) != FLED_OK)
    {
        std::puts("frameledger.h: a call was refused");
        return 1;
    }
    const char* bytes = nullptr;
    const std::size_t count = fled_pending(&writer, &bytes);
    static const char expected[] =
        "# frameledger {\"schema\":1}\n"
        "run_id,segment,frame,category,name,depth,value\n"
        "check,,0,frame,wall,0,16.500\n"
        "# frameledger-close {\"rows_dropped\":0,\"frames\":1}\n";
    if (count != sizeof expected - 1 || std::memcmp(bytes, expected, count) != 0)
    {
        std::puts("frameledger.h: unexpected output");
        return 1;
    }
    return 0;
}
