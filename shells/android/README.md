# Android shell

The main activity (`EmuDenActivity`, SDL's `SDLActivity`) around the same C++ app as the
desktop builds, and `PanelActivity` for the second display, drawn by the same native loop.
Gradle only wraps the root CMake project; there are no screens or logic here.

## Building

Needs JDK 21, the Android SDK (platform 36, CMake 3.31.5) and NDK 29.0.14206865.

```
set ANDROID_HOME=C:\Users\<you>\AppData\Local\Android\Sdk
cd shells\android
gradlew.bat :app:assembleDebug
```

The APK is `buildndroid\gradle\app\outputs\apk\debug\app-debug.apk`; the native build is
in `buildndroid\cxx`. arm64-v8a only, OpenGL ES 3, minSdk 28. The application id is
`io.github.denarmo.emuden.next`, so it installs beside the previous app.

## How it fits together

- The app target `den_app` is built as `libmain.so` with SDL linked in
  (`app/CMakeLists.txt`); `app/src/main/cpp/entry.cpp` is `SDL_main`.
- SDL's Java sources are compiled from `.deps/sdl3-src/android-project`, which the first
  CMake configure fetches.
- The repository's `assets` folder is packed as `assets/emuden` and unpacked to
  `<files>/assets` at the first start after an install or update, because the app reads its
  assets as files. `pal::ExecutableDir()` is the private files folder on Android, so
  `pal::AssetsDir()` and `pal::DataDir()` are `<files>/assets` and `<files>`.
- HTTP is `platform/src/android/http_android.cpp`: JNI into `HttpCall`
  (`HttpURLConnection`, redirects followed by hand).
- Cores are installed under the data folder, which is app-private, and loaded with
  `dlopen` from there like on the desktop.

## Running on a device

`tools\android\run.ps1 -Device <serial> -Port 5601` builds, installs and starts the app with
the automation endpoint on (debug builds only: the intent extra `--ei automation <port>`),
forwarded over adb, so `python tools/drive.py --port 5601 ...` drives it as on the desktop.
`-Apk <file>` installs that APK instead of building, `-GrantStorage` allows all files access
without the screens, `-Stop` stops the app. The app's log is `files/log.txt`
(`adb shell run-as io.github.denarmo.emuden.next cat files/log.txt`), not logcat.

`gradlew.bat :app:assembleRelease` writes `app-release.apk`, signed with the debug key (there
is no release key yet), so it installs over a debug build. It has no automation endpoint.

The device has two displays, so `screencap` needs `-d <id>` (`dumpsys SurfaceFlinger
--display-id`) and `uiautomator dump` does not work. The built-in pad is
`/dev/input/event8`, which the adb shell may write: `sendevent /dev/input/event8 1 545 1`,
then `0 0 0`, is a real D-pad press.

## Back

Back is one thing for the app wherever it comes from. `EmuDenActivity` registers an
`OnBackInvokedCallback` (Android 13 and later, `android:enableOnBackInvokedCallback="true"`),
which the system calls for the gesture, the navigation bar and Back keys alike, and it also
catches `KEYCODE_BACK` in `dispatchKeyEvent` for older systems. Both send SDL's Back key, so
the app sees `Nav::Back`: the pause menu in a game, one page back elsewhere.

The Back key of the Retroid's built-in pad is scan code 158 (`KEY_BACK`) on the gamepad
device (`Vendor_2022_Product_3001.kl`: `key 0x9e BACK`). SDL's gamepad code would hand a
`KEYCODE_BACK` from a gamepad on as the pad's Select button, which is why that key did
nothing useful in a game. The pad's own Select (`BTN_SELECT`) is untouched. A real press:
`sendevent /dev/input/event8 1 158 1`, `0 0 0`, `1 158 0`, `0 0 0`.

## Second screen

`SecondScreen` puts one `PanelSurface` (a `SurfaceView`) on the first presentation display
the main activity is not on itself (display 2 on the Retroid Pocket Duo, 1280x960). The
Surface and the touches go to `platform/src/android/second_display_android.cpp`, which makes
an EGL window surface with the app's own display and config: the one GL context draws both
screens, the second without waiting for its display (it does wait while the main window is
hidden, as it paces the loop then). What is drawn there is the app's business
(`app/screens/companion`, `docs/interfaces.md`).

The surface lives in `PanelActivity`: an activity in a task of its own (`singleInstance`,
its own affinity, not in the recents), started on that display with
`ActivityOptions.setLaunchDisplayId`, in the same process. It replaces the Presentation of
before, for three reasons:

- A Presentation is a window of the main activity's context and was dismissed in `onStop`;
  a stopped activity also leaves the process "cached", which Android may freeze. From
  Android 10 every display has a resumed activity of its own, so `PanelActivity` stays
  resumed while another app covers the main display, keeps the process in the foreground
  (no foreground service, no notification) and keeps showing the game.
- A Presentation is a layer above every app of its display, so an app opened there was
  hidden under it and the quick settings had to be taken away. An app now simply opens over
  `PanelActivity`, which is shown again by itself when the app is left with Back.
- A Presentation cannot take the focus. `PanelActivity` can: keys and the pad that arrive
  there are handed to SDL (`SDLActivity.handleKeyEvent`,
  `SDLControllerManager.handleJoystickMotionEvent`), and Back is the app's Back.

Where the system does not let an activity start on that display
(`isActivityStartAllowedOnDisplay`, or Android 9) the Presentation is used as before: it
exists while the main activity is started, lies over every app there, and an app asked for
on that display opens on the main one with the game paused.

`PanelActivity` exists while the display exists and the app wants it (`ui.companion.enabled`,
or a claim by the player). Starting it moves the input focus to its display, so the main
activity is brought to the front again right after (`FocusHop.home`). While the main
activity is stopped the app gives the second display up, unless a game plays on it.

## The app goes on while the main activity is stopped

Before, `onStop` of the main activity ended everything the user saw: SDL blocked the native
thread and paused the sound, and the Presentation was dismissed. With the game swapped to
the second display, an app opened on the main one (from the quick settings there, or from
the launcher) therefore looked like EmuDEN closing. Now:

- `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` is off (`platform/src/android/lifecycle_android.cpp`):
  SDL still pauses its window (the surface is gone, it takes the GL context off the thread),
  but the loop and the sound carry on. `pal::MainWindowHidden()` is true from SDL's
  "did enter background" to "did enter foreground".
- SDL's event pump waits 100 ms per call while its window is paused. `EmuDenActivity` tells
  the native side when it is stopped (`nativeStopped`); while it is, `den::input` takes the
  events without the pump (`pal::EventPumpHeld()`); pad and key events are queued by SDL's
  Java side directly. From `onStart` (and in `onDestroy`) the pump runs again, so resume and
  quit are seen.
- `pal::PrepareFrame()` makes the app's GL context current again without the window, on a
  1x1 pixel buffer (or with no surface where the window's config has none), so the frame,
  texture uploads and the second display's EGL surface work as always. The main window's
  frame is drawn for nobody and not swapped.
- The app (`app/screens/apps/beside.cpp`, `system::AppBeside`) holds the game when the
  display it is on is covered (paused, battery save and resume state written) and lets it go
  on when the display is back. With nothing of the app shown anywhere the loop idles at
  10 Hz, as the process may be ended there without a word.

Not handled: a game whose core draws with the GPU cannot be *opened* while the main window
is hidden (SDL waits for the window when it creates a context); HID pads SDL opens itself
(USB, some Bluetooth) are frozen by SDL in `onPause`, the built-in pad is not one of them.

## Apps

`AppList` lists the launcher's activities (`queryIntentActivities` for `MAIN` / `LAUNCHER`;
the manifest's `<queries>` element makes them visible, there is no `QUERY_ALL_PACKAGES`),
except the app's own. Icons, adaptive ones included, are drawn into 192 pixel PNGs in
`<cache>/app_icons` and drawn again when the package was updated; a receiver for package
added, removed, changed and replaced makes the native side list again. An app is opened
with its launcher intent, `FLAG_ACTIVITY_NEW_TASK` and the display it is wanted on.

While a game runs the app opens on the display the quick settings are on, whichever that is:
the second one over `PanelActivity`, or, with the game swapped to the second display, the
main one over `EmuDenActivity`, which is then stopped while the game goes on (above). What
is where is decided by `system::AppBeside` (unit-tested); `AppBeside.java` only does what it
is asked:

- **Raise** brings our own activity of a display to the front of it, over the other app,
  which stays as it is underneath: the quick settings over the app. **Lower** brings the
  app's task forward again with its launcher intent (`AppList.toFront`), as a launcher
  does for an app that is already open: "Back to <app>" in the quick settings, and the
  system's Back on that display while they are over an app (`nativeBack`). The task and
  its activity stay the same, and our activity is right under it, so leaving the app with
  Back shows the quick settings. `moveTaskToBack` was tried first and put our task behind
  the launcher too; it is only the fallback now.
- **The handle** is a 44 dp round tab over the other app (a Presentation holding nothing
  else; that is the one window an app may put over other apps without a permission, and
  only on a presentation display). A tap pulls the quick settings over the app. It can be
  dragged anywhere, comes to rest on the nearest edge, is found there again
  (`SharedPreferences` "beside") and is 45% opaque until touched. Its square is excluded
  from the system gestures: resting on an edge, a drag on it was the Back gesture. The main display is no
  presentation display: with the app there the game's own display shows a small tab at its
  right edge instead, which does the same.
- **The app left with Back**: our activity under it is shown again, which the native side
  sees as its surface coming back; the plain quick settings are there.
- **Home** is told to nobody. On the second display with no app of ours there, the quick
  settings' surface goes and nothing was asked for: they are raised again after 0.4 s (three
  tries). With an app in front the system's `ACTION_CLOSE_SYSTEM_DIALOGS` broadcast with the
  reason `homekey` is used: when the game is still shown half a second later, Home was on
  the app's display, and the quick settings are raised there. Devices that do not send that
  broadcast (gesture navigation may not) keep the handle as the way back. On the main
  display nothing is raised by itself: Home there leaves EmuDEN's window as the user asked,
  and a game on the second display goes on.

The keys and the built-in pad go to the focused window of the focused display, which is the
other app while it is in front, and Android has no way to keep them: the game shows "The pad
is with the app on the other screen. Tap the game to take it back", because a touch on a
display moves the focus there. `onTopResumedActivityChanged` of both activities is what
tells (`AppBeside.focus`). With one display the app takes it: the game waits in its pause
menu with its saves written.

The media volume (`AudioManager.STREAM_MUSIC`) is read and set for `pal::audio`
(`EmuDenActivity.volumeGet`, `volumeSet`, `volumeSteps`); both activities name it as the
stream the hardware keys change. Android has no volume for one other app: the game's own
gain is a share of the system volume, the other app plays at the system volume.

Seen on the Retroid Pocket Duo (Android 16, gesture navigation, release APK, `adb` only),
with Settings as the other app: `PanelActivity` starts on display 2 and the focus comes
back to the main activity; Tetris swapped to the second display goes on (picture, 59 fps,
AAudio stream started, same pid) with Settings over the stopped main activity, and the
process is `TOP (resumed-multi-display-activity)`; "Back to Settings" and Back on either
display leave the same task and activity record in front; the handle dragged to another
edge is found there again; both volume sliders; Home on the second display. Not run: an
app that plays sound, the Presentation fallback, a GPU core, one display. See
`docs/history.md`.

## System keyboard

`TextBridge` opens a window that can take the focus on the display the keyboard should be
on: a `Presentation` on the other display when the app asks for the second screen and it is
there, else a `Dialog` on the activity's own. The keyboard serves the focused window of the
focused display only, and a window shown on the other display does not move that focus: an
activity that shows nothing is started there first and ends at once (`FocusHop`), which
leaves that display focused, and the main activity is brought to the front again when the
session ends. Without this no keyboard came on the Retroid and the app's own keys took over
after a second. It holds one invisible `EditText`, is one
pixel wide and not touch-modal, so the quick settings under it can still be touched. The
keyboard is shown for the field (`SOFT_INPUT_STATE_ALWAYS_VISIBLE` plus `showSoftInput`);
its text (`TextWatcher`), its done action and Back go to
`platform/src/android/text_input_android.cpp` with the number of the session, and the app
draws the text itself. The window's insets say when the keyboard is on screen and how much
of the display it covers (`WindowInsets.Type.ime()` from Android 11, the system window
inset before), which moves the app's text line up when both are on one display.

Pad keys and stick motion that arrive at this window, because it has the focus, are handed
to SDL (`SDLActivity.handleKeyEvent`, `SDLControllerManager.handleJoystickMotionEvent`), and
`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` is on for Android, so the pad keeps driving the
app while the keyboard is open. A keyboard that refuses the second display never reports
itself shown; after a second the app ends the session and draws its own keys on the second
display. The same happens when the keyboard is put away (the system's Back closes it first,
and a touch on the other display takes the focus from it).

Seen on the Retroid Pocket Duo: the field of the Search page tapped, Gboard on the second
display, `input text tetris` in the line on the main one with the result filtered, Enter
closing it, the focus back on the main display. Not run: one display (the device has two),
a request made with the pad, a keyboard put away by hand.

## In-app browser

`WebPage` puts an `android.webkit.WebView` into SDL's layout above the surface, inside the
bounds the app gives, for `platform/src/android/webview_android.cpp` (`docs/in-app-browser.md`).
`WebDownloads` saves what the user downloads there. The browser page's test endpoint is on
the automation port + 3, forwarded by `run.ps1`. `screencap` shows the page; the app's own
screenshot does not. A local test server is reached with `adb reverse tcp:<port> tcp:<port>`
and `http://127.0.0.1:<port>/` (plain http is allowed for the device itself only).

## Storage

Games are read by path. `StorageAccess` shows a dialog at every start until the app has all
files access or a copied folder:

- **Allow access** opens the system's "All files access" screen for the app. Back in the
  app with the switch on, the folder browser starts at the shared storage root (`HOME`).
- **Choose a folder** (also offered when the user comes back without allowing) opens the
  system folder picker. A granted folder cannot be read by path, so `FolderImport` copies it
  to `Android/data/io.github.denarmo.emuden.next/files/Games/<folder>` on shared storage,
  which needs no permission; the folder browser starts in `Games` then. Files already there
  with the same size are kept. The copy takes the space again and is not kept in step with
  the original; removing the app removes it.
- Android 10 and older get the storage permission request instead.

## Seen on the Retroid Pocket Duo

Android 16, Adreno 722, two screens, on 2026-11-12:

- First run: setup with `/storage/emulated/0/ROMS`, Tetris (Game Boy), Pokemon - Lazarus
  (GBA) and Gran Turismo 2 (PlayStation) with an AAudio stream started, touch taps, Back
  opening the pause menu.
- Storage: the dialog, "Allow access" to the system screen and the switch, the toast back in
  the app; coming back without allowing, "Choose a folder", `ROMS/GB` copied (2 files).
- PoPoLoCrois (PSP): the PPSSPP core and its assets downloaded on Play, intro with text and
  the title screen. Explorers of Skies (NDS, melonDS downloaded on Play) and Mario Golf
  (GBC). Save and load state in all three.
- The built-in pad through its input device: D-pad, Start and A in games; every button,
  both sticks and both triggers on the "Test buttons" page.
- Home and back again in each of the three games, the display rotated to the other
  landscape and to portrait and back, and the app moved to the second screen and back while
  the NDS game ran: same process, picture back every time.

## Orientation, insets, and haptics

`EmuDenActivity` uses `android:screenOrientation="fullUser"` with all four SDL orientations (`SDL_ORIENTATIONS`) enabled, and updates Settings > Display `Orientation` (`Auto` / `Landscape` / `Portrait`) via `requestOrientation(int)`. Window insets combine display cutouts and mandatory system gestures (`safeInsets()`) so portrait bottom rails, portrait controller decks, landscape peripheral overlays, and headers clear camera holes and gesture bars without shrinking horizontal content with side gesture padding. On-screen button taps and D-pad changes trigger `hapticTick()` (`VibrationEffect.EFFECT_TICK`). `DeviceFacts.builtInPad()` checks `getMotionRanges()` / `getControllerNumber() > 0` to ignore virtual kernel daemons such as `uinput-xiaomi`.

## Limits for now

- **Storage:** a picked folder is copied, not read in place, and once a folder has been
  copied the app does not offer all files access again (allow it in the system settings).
- **BIOS files:** the BIOS folder is inside the app's private data, where a file manager
  cannot put files.
- Secrets are kept in memory only (no Keystore) and the Switch 2 engine is not available on
  Android.
