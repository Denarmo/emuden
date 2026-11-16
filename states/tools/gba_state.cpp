// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// gba_state convert <from core> <state> <to core> <state of the running target> <out>
// gba_state check <core> <state>
// States are a core's own bytes (not EmuDEN's DENSTAT1 files).

#include <cstdio>
#include <string>

#include "den/base/files.h"
#include "den/states/gba.h"

int main(int argc, char** argv) {
    using namespace den;
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "check" && argc == 4) {
        const auto state = ReadBinaryFile(PathFromUtf8(argv[3]));
        if (!state) {
            std::fprintf(stderr, "cannot read %s\n", argv[3]);
            return 2;
        }
        const std::string why = states::GbaStateNotPortable(argv[2], *state);
        std::printf("%s\n", why.empty() ? "portable" : why.c_str());
        return why.empty() ? 0 : 1;
    }
    if (command == "convert" && argc == 7) {
        const auto state = ReadBinaryFile(PathFromUtf8(argv[3]));
        const auto target = ReadBinaryFile(PathFromUtf8(argv[5]));
        if (!state || !target) {
            std::fprintf(stderr, "cannot read the states\n");
            return 2;
        }
        auto out = states::ConvertGbaState(argv[2], *state, argv[4], *target);
        if (!out) {
            std::fprintf(stderr, "%s\n", out.error().c_str());
            return 1;
        }
        const std::string_view bytes(reinterpret_cast<const char*>(out->data()), out->size());
        if (!WriteFileAtomic(PathFromUtf8(argv[6]), bytes)) {
            std::fprintf(stderr, "cannot write %s\n", argv[6]);
            return 2;
        }
        return 0;
    }
    std::fprintf(stderr, "usage: gba_state convert <from> <state> <to> <target state> <out> | check <core> <state>\n");
    return 2;
}
