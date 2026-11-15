// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/achievements/games.h"
#include "den/platform/pal.h"

namespace den::achievements {

HttpFn PlatformHttp(std::string user_agent) {
    return [user_agent = std::move(user_agent)](const HttpRequest& request) {
        pal::HttpRequest out;
        out.url = request.url;
        out.headers.emplace_back("User-Agent", user_agent);
        if (!request.post_data.empty()) {
            out.method = "POST";
            out.body = request.post_data;
            out.headers.emplace_back("Content-Type", request.content_type.empty()
                                                         ? "application/x-www-form-urlencoded"
                                                         : request.content_type);
        }
        const pal::HttpResponse answer = pal::HttpGet(out);
        return HttpResponse{answer.error.empty() ? answer.status : (answer.status ? answer.status : 0), answer.body};
    };
}

} // namespace den::achievements
