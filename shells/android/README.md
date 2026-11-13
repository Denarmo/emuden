# Android shell

One activity (`EmuDenActivity`, SDL's `SDLActivity`) around the same C++ app as the desktop
builds. Gradle only wraps the root CMake project; there are no screens or logic here.

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

`SecondScreen` shows a `Presentation` with one `SurfaceView` on the first presentation
display the activity is not on itself (display 2 on the Retroid Pocket Duo, 1280x960). The Surface and the touches go to
`platform/src/android/second_display_android.cpp`, which makes an EGL window surface with
the app's own display and config: the one GL context draws both screens, the second without
waiting for its display. The Presentation exists while the activity is started, the display
exists and the app wants it (`ui.companion.enabled`, or a claim by the player); it is shown
again after stop and start and when a display is added. Its window cannot take the focus,
so keys and the pad stay with the activity, and it lies over every activity on that display
(a Presentation is a layer above the apps). What is drawn there is the app's business
(`app/screens/companion`, `docs/interfaces.md`).

`screencap -d <id>` shows either screen; `input -d 2 tap <x> <y>` touches the second one.
The companion's test endpoint is on the automation port + 2 (`state`, `tap`, `screenshot`),
forwarded by `run.ps1`. `run.ps1 -BuildName <name>` (Gradle: `-PemudenBuild=<name>`) builds in
`build/<name>` instead of `build/android`.

## Apps

`AppList` lists the launcher's activities (`queryIntentActivities` for `MAIN` / `LAUNCHER`;
the manifest's `<queries>` element makes them visible, there is no `QUERY_ALL_PACKAGES`),
except the app's own. Icons, adaptive ones included, are drawn into 192 pixel PNGs in
`<cache>/app_icons` and drawn again when the package was updated; a receiver for package
added, removed, changed and replaced makes the native side list again. An app is opened
with its launcher intent and `FLAG_ACTIVITY_NEW_TASK`.

From the quick settings the app opens on the other display (`AppBeside`, after
`isActivityStartAllowedOnDisplay`). The Presentation with the quick settings would cover it,
so it is taken away for that time (`SecondScreen.lend`) and a small handle, "Quick
settings", hangs from the top edge of that display over the other app. The quick settings
come back

- when the app is left with Back: `AppHost`, an empty activity of ours started under the
  app on that display, comes up again, which is the only sign Android gives;
- when the handle is tapped, with the app still open under them or after Home, which
  `AppHost` does not notice;
- when the activity is stopped.

Coming back also gives the keys and the pad back to the game (`FocusHop.home`: the main
activity is brought to the front on its own display). When the system does not allow
activities on that display the app opens on the main one instead, a toast says so, and the
game is paused.

While the app is on the other display the activity stays resumed (only "top resumed" moves
on), so SDL keeps running: SDL 3 pauses its thread in `onStop`, not on a lost focus or
`onPause`, and nothing in the app pauses a game for a lost focus. Picture and sound go on.
The keys and the built-in pad go to the focused window of the focused display, which is the
other app now, and Android has no way to keep them: the game shows "The pad is with the app
on the other screen. Tap the game to take it back", because a touch on a display moves the
focus there. Each display keeps a focused window of its own, so the activity's window does
not lose its focus then; `onTopResumedActivityChanged` is what tells (`AppList.nativeFocus`).
With the game swapped to the second display, or with one display, the app takes the
activity's own display: the activity is stopped, SDL pauses, and the game waits paused with
its saves written until the user comes back.

Seen on the Retroid Pocket Duo (release APK, `adb` only): Settings beside a running Tetris
with the handle over it, the game drawing on and its AAudio stream started; Start on the
built-in pad did not reach the game while Settings had the focus and did after a tap on the
game; Back on that display, the handle with the app open, and the handle after Home each
brought the quick settings back, and Start reached the game again without a tap. Not run:
the game swapped to the second display, one display, an app that plays sound.

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

## Limits for now

- **Storage:** a picked folder is copied, not read in place, and once a folder has been
  copied the app does not offer all files access again (allow it in the system settings).
- **BIOS files:** the BIOS folder is inside the app's private data, where a file manager
  cannot put files.
- Secrets are kept in memory only (no Keystore) and the Switch 2 engine is not available on
  Android.
