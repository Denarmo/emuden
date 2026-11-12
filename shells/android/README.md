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

The APK is `build\android\gradle\app\outputs\apk\debug\app-debug.apk`; the native build is
in `build\android\cxx`. arm64-v8a only, OpenGL ES 3, minSdk 28. The application id is
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
- **Second screen:** the app draws on one screen; a DS game shows both of its screens there.
- Secrets are kept in memory only (no Keystore) and the Switch 2 engine is not available on
  Android.
