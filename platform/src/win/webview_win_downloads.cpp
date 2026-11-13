// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Downloads of the WebView2 backend: the user started one in the page, the app says where the
// file goes (or that it does not), and the engine's own download interface stays out of it.

#include "webview_win.h"

#include <filesystem>

#include "den/base/log.h"

namespace den::pal {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

const char* InterruptText(COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason) {
    switch (reason) {
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED:
        return "cancelled";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_NO_SPACE:
        return "the disk is full";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_ACCESS_DENIED:
        return "the file could not be written";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED_BY_POLICY:
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_SECURITY_CHECK_FAILED:
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_FILE_MALICIOUS:
        return "Windows blocked the file";
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_SERVER_UNAUTHORIZED:
    case COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_SERVER_FORBIDDEN:
        return "the site refused the download";
    default:
        return "the connection was lost";
    }
}

} // namespace

void WinWebView::HookDownloads() {
    ComPtr<ICoreWebView2_4> view4;
    if (FAILED(view_.As(&view4))) {
        log::Warn("webview", "this WebView2 runtime is too old to hand over downloads");
        return;
    }
    const Guard guard = alive_;
    EventRegistrationToken token{};
    view4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [this, guard](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                return guard.expired() ? S_OK : OnDownload(args);
            })
            .Get(),
        &token);
}

// The engine's own download interface never shows: the file goes where the app says, or the
// download is cancelled before a byte is written.
HRESULT WinWebView::OnDownload(ICoreWebView2DownloadStartingEventArgs* args) {
    args->put_Handled(TRUE);
    ComPtr<ICoreWebView2DownloadOperation> operation;
    args->get_DownloadOperation(&operation);
    if (!operation) {
        args->put_Cancel(TRUE);
        return S_OK;
    }
    CoString uri, mime, suggested;
    operation->get_Uri(&uri.text);
    operation->get_MimeType(&mime.text);
    args->get_ResultFilePath(&suggested.text);
    INT64 total = -1;
    operation->get_TotalBytesToReceive(&total);

    WebDownload download;
    download.id = ++last_download_;
    download.file_name = WebSafeFileName(suggested.Utf8());
    download.mime = mime.Utf8();
    download.url = uri.Utf8();
    download.host = WebUrlHost(download.url);
    download.size = total > 0 ? total : -1;
    const WebDownloadDecision decision =
        callbacks_.on_download ? callbacks_.on_download(download) : WebDownloadDecision::Refuse();
    if (!decision.accept || decision.target.empty()) {
        args->put_Cancel(TRUE);
        log::Info("webview", "refused the download '{}'", download.file_name);
        return S_OK;
    }
    std::error_code ec;
    std::filesystem::create_directories(decision.target.parent_path(), ec);
    args->put_ResultFilePath(decision.target.c_str());
    log::Info("webview", "download {} '{}' from {}", download.id, download.file_name, download.host);

    const std::uint64_t id = download.id;
    const Guard guard = alive_;
    downloads_[id] = operation;
    EventRegistrationToken token{};
    operation->add_BytesReceivedChanged(
        Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
            [this, guard, id](ICoreWebView2DownloadOperation* op, IUnknown*) -> HRESULT {
                if (guard.expired() || !callbacks_.on_download_progress) {
                    return S_OK;
                }
                INT64 received = 0, size = -1;
                op->get_BytesReceived(&received);
                op->get_TotalBytesToReceive(&size);
                callbacks_.on_download_progress(id, received, size > 0 ? size : -1);
                return S_OK;
            })
            .Get(),
        &token);
    operation->add_StateChanged(Callback<ICoreWebView2StateChangedEventHandler>(
                                    [this, guard, id](ICoreWebView2DownloadOperation* op, IUnknown*) -> HRESULT {
                                        if (!guard.expired()) {
                                            FinishDownload(id, op);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
    return S_OK;
}

void WinWebView::FinishDownload(std::uint64_t id, ICoreWebView2DownloadOperation* operation) {
    COREWEBVIEW2_DOWNLOAD_STATE state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
    operation->get_State(&state);
    if (state == COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) {
        return;
    }
    // Kept alive past the erase: this runs inside the operation's own event.
    const ComPtr<ICoreWebView2DownloadOperation> keep = operation;
    downloads_.erase(id);
    if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
        CoString file;
        operation->get_ResultFilePath(&file.text);
        if (callbacks_.on_download_finished) {
            callbacks_.on_download_finished(id, std::filesystem::path(file.text ? file.text : L""));
        }
        return;
    }
    COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason = COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;
    operation->get_InterruptReason(&reason);
    if (callbacks_.on_download_failed) {
        callbacks_.on_download_failed(id, InterruptText(reason));
    }
}

void WinWebView::CancelDownload(std::uint64_t id) {
    if (const auto it = downloads_.find(id); it != downloads_.end()) {
        it->second->Cancel();
    }
}

} // namespace den::pal
