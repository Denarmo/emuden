# In-app browser

A page of the app (route `web`) that shows a web site with the platform's own web engine. The
user browses the real site; when the user presses a download button there, the app receives
the file, the way a browser's download manager would, and hands its path to whoever opened
the page. A feature plugin can then, for example, patch the user's own base ROM with it.

## Rules

These hold for every backend and every opener.

- **The user browses, not the app.** The app loads the address the opener gave and nothing
  else. It makes no request of its own to the site, follows no link by itself and starts no
  download by itself.
- **The page is never read.** No call returns the page's text, markup or pictures; no script
  is run in the page; cookies and site storage are not touched (they live in the engine's
  profile folder `<data>/webview`, which the app never opens). `pal::WebView` has no method
  for any of it, and the Windows backend switches off host objects and web messages, the two
  ways a page and an app could talk.
- **What the app does know** is what a browser shows around a page: the address, the
  document's title, whether it is loading, whether there is history. The automation endpoint
  reports those and nothing more.
- **Navigation stays on the opener's hosts.** The top-level page may only be on a host of
  the allowed list: exact names, or `*.example.com` for a name and its subdomains; https
  only, http for loopback addresses. A link elsewhere is refused with a toast. A link that
  asks for a new window opens in the same view under the same rule. A server redirect to
  another host is let through as far as the server's answer, because a download often comes
  from a second host; if the answer is a page and not a file, it is undone before it shows
  and refused like a link. Frames and the files a page loads (pictures, scripts, a
  checkpoint's challenge) are not filtered: they are the site's own business.
- **Downloads go to the inbox**, `<data>/inbox/web`, and only files the opener asked for by
  extension. The default list is `bps ips ups xdelta vcdiff aps ppf zip 7z`. Anything else is
  refused with a toast before a byte is written.
- **Never a ROM.** Cartridge, disc and card images (`gb gbc gba nds 3ds iso bin cue chd nes
  sfc n64 z64` and the rest of the list in `app/screens/web/web_policy.cpp`) are refused even
  when an opener lists them. An archive is taken as it is; what is inside is the opener's to
  check.
- **No sign-in by the app, no saved passwords.** Password saving and form autofill are off.
  Camera, microphone, location, notification and every other permission request is denied.
  The engine's developer tools, context menu, browser shortcuts and download bubble are off.
  The user agent is the engine's own.
- **Pad input is input.** The pad's pointer, clicks, wheel, typed text and Enter reach the
  page as the mouse and key events the real devices would send.

## Using it from a feature (an opener)

```cpp
#include "web_browser.h"   // app/screens/web

den::app::web::OpenRequest request;
request.url = "https://example.com/hacks";
request.title = "ROM hacks";
request.allowed_hosts = {"example.com", "*.example.com"};   // empty = the host of the url
request.opener.extensions = {"bps", "xdelta"};              // empty = the default list
request.opener.on_download = [](const den::app::web::Download& d) {
    // d.file is in the inbox and is yours now: move or delete it when done.
    // d.name is the site's file name, d.host where it came from, d.mime its type.
};
den::app::web::Open(std::move(request));
```

`on_download` runs on the main thread, from the frame, after the file is complete. The page
stays open; the opener may `Svc().stage->Pop()` it or push its own page on top.

A route pushed by hand works too, with an opener registered under a name:

```cpp
den::app::web::RegisterOpener("romhacks", opener);
Svc().stage->Push("web", {{"url", url}, {"hosts", "example.com,*.example.com"},
                          {"title", "ROM hacks"}, {"opener", "romhacks"}, {"ext", "bps"}});
```

Files nobody collected stay in the inbox; nothing cleans it yet.

## The page

`app/screens/web/web_screen.cpp`. The kit draws a bar at the top (Back, Forward, Reload, the
page's title and host, the download's progress, Close) and the hint bar at the bottom. The
web view is a native view over the rest of the window. A native view sits above everything
the app draws, so:

- while a dialog or the on-screen text entry is open, the view is hidden;
- while toasts show, the view starts below them (one toast fits in the bar, so nothing moves
  for it; more push the view down);
- when another page is pushed on top, the view is hidden until the page is back on top.

| Pad | Does |
|---|---|
| Left stick, D-pad | Moves the pointer |
| A | Click |
| Right stick | Scrolls under the pointer |
| B | Back in the page's history; closes the page when there is none |
| X | Opens the app's text entry and types the result into the page's focused field |
| View (Select) | Enter |
| Y | Reload |
| LB / RB | Back / forward |
| Start | Close |

Mouse and touch go straight to the page. The pointer is a small window of the app above the
page (clicks pass through it); it appears with the first pad input and hides when the mouse
moves over the bar.

## `pal::WebView`

`platform/include/den/platform/webview.h`.

| Call | What |
|---|---|
| `WebView::Available()`, `UnavailableReason()` | Whether the platform has an engine, and the sentence to show when not |
| `WebView::Create(window, bounds, config)` | A hidden view over `bounds` (pixels of the window). `config`: the profile folder and the allowed hosts |
| `SetBounds`, `Show`, `Hide`, `Visible` | Placement |
| `Navigate`, `Back`, `Forward`, `Reload`, `Stop`, `State()` | `WebState`: url, title, loading, can go back / forward, ready |
| `SetAllowedHosts` | The navigation filter's list |
| `SetCallbacks` | `on_state`; `on_navigate` (the decision, default: allowed hosts only); `on_refused`; `on_download` returning `WebDownloadDecision::Accept(file)` or `Refuse()`; `on_download_progress`, `on_download_finished`, `on_download_failed` |
| `CancelDownload(id)` | Stops a download that was accepted |
| `SetPointerVisible`, `MovePointer`, `Click`, `Scroll`, `TypeText`, `Key` | Pad control, positions in pixels of the view |
| `SetZoom`, `Zoom` | The page's zoom factor |
| `WebUrlHost`, `WebUrlAllowed`, `WebHostAllowed`, `WebSafeFileName` | The address checks, shared by the backends |

Everything runs on the main thread; callbacks come from the platform's event pump.

Backends:

- **Windows**: WebView2 (`platform/src/win/webview_win*.cpp`). The Evergreen runtime is part
  of Windows 11; when it is missing `Available()` is false and the page shows the reason. The
  SDK headers and the static loader come from the `Microsoft.Web.WebView2` NuGet package,
  fetched at configure time with a pinned hash (`cmake/deps.cmake`). The view is a child
  window of the app's window. `DownloadStarting` is handled, so the engine's download bubble
  never shows and the file goes to the path the app gives. A view hosted in a window takes no
  synthetic input through its own interface, so pad input goes in through the engine's
  `Input.dispatchMouseEvent`, `Input.dispatchKeyEvent` and `Input.insertText`; no other
  method of that protocol is used.
- **Fake** (`platform/src/webview_fake.cpp`): the filter, the history and the download
  decision without an engine. `CreateFakeWebView` for tests (`SimulateLink`,
  `SimulateDownload`, `Input()`); `EMUDEN_FAKE_WEBVIEW=1` makes `Create` return it.
- **Linux, macOS, Android**: none yet; `Available()` is false with a reason
  (`platform/src/posix/webview_posix.cpp`). WebKitGTK, WKWebView and `android.webkit.WebView`
  are the engines to wrap.

## Test endpoint

With `EMUDEN_AUTOMATION=<port>` the page's endpoint listens on `<port>+3`, one line in and one
line out (`app/screens/web/web_endpoint.cpp`):

| Line | Reply |
|---|---|
| `open url=<url> [hosts=a,b] [ext=bps,zip] [title=<text>]` | `ok`; opens the page for a test opener |
| `state` | JSON: `open`, `available`, `ready`, `view_visible`, `url`, `title`, `loading`, `can_go_back`, `can_go_forward`, `view` (x, y, w, h in logical units), `pointer`, `cursor`, `downloading`, `progress`, `downloads`, `last_download`, `refused_downloads`, `last_refused_download`, `refused_navigations`, `last_refused_host`, and what the test opener was handed (`opener_calls`, `opener_file`, `opener_name`, `opener_host`) |
| `cursor <x> <y>` | `ok`; puts the pad's pointer at a point of the view (logical units) |
| `close` | `ok`; closes the page |

The app's own screenshot shows the bar and not the page (the view is a native window); a
picture of the whole needs a capture of the app's window by the system. Pads only reach an
app whose window is in front; a test that cannot guarantee that starts the app with
`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1`.
