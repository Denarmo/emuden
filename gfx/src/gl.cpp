// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>

#include "den/gfx/gl.h"

namespace den::gl {

#define DEN_GL_DEFINE(ret, name, args) ret(*name) args = nullptr;
DEN_GL_FUNCTIONS(DEN_GL_DEFINE)
#undef DEN_GL_DEFINE

bool Load(ProcLoader loader, const char** missing) {
    bool ok = true;
#define DEN_GL_LOAD(ret, name, args)                                                              \
    name = reinterpret_cast<ret(*) args>(loader("gl" #name));                                     \
    if (!name && ok) {                                                                            \
        ok = false;                                                                               \
        if (missing) {                                                                            \
            *missing = "gl" #name;                                                                \
        }                                                                                         \
    }
    DEN_GL_FUNCTIONS(DEN_GL_LOAD)
#undef DEN_GL_LOAD
    return ok;
}

} // namespace den::gl
