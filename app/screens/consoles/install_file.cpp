// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Install a console from a file: the way to add a console EmuDEN does not offer, such as one
// whose engine the user brings themselves.

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/strings.h"
#include "den/base/files.h"
#include "den/platform/pal.h"

namespace den::app {

namespace {

void Install(const plugins::PackageProbe& probe) {
    if (const Status installed = Svc().plugins->InstallPackage(probe); !installed.ok) {
        Svc().stage->Toast(installed.error, 6.0f);
        return;
    }
    Svc().stage->Toast(TrF("install_file.done", probe.name));
    RebuildTop();
    if (!probe.engine || Svc().engine == nullptr) {
        return;
    }
    for (const std::string& system : probe.systems) {
        if (!Svc().engine->Available(system) && pal::GetCaps().child_processes) {
            ChooseEnginePath(system);
            return;
        }
    }
}

void Probe(const std::string& path) {
    Result<plugins::PackageProbe> probe = Svc().plugins->ProbePackage(PathFromUtf8(path));
    if (!probe) {
        Svc().stage->Toast(probe.error(), 6.0f);
        return;
    }
    std::string consoles;
    for (const std::string& name : probe->consoles) {
        consoles += (consoles.empty() ? "" : ", ") + name;
    }
    std::string message = TrF("install_file.what", probe->name, probe->version, probe->publisher, consoles);
    if (!probe->replaces.empty()) {
        message += " " + TrF("install_file.replaces", probe->replaces);
    }
    message += " " + Tr(probe->official ? "install_file.signed" : "install_file.unsigned");
    if (probe->engine) {
        message += " " + Tr("install_file.engine");
    }
    Svc().stage->Confirm(TrF("install_file.title", probe->name), message, Tr("install_file.install"),
                         [probe = *probe] { Install(probe); });
}

} // namespace

void InstallConsoleFromFile() {
    PickOptions options;
    options.title = Tr("install_file.pick");
    options.files = true;
    options.extensions = {"emuplugin", "json"};
    Pick(std::move(options), Probe);
}

} // namespace den::app
