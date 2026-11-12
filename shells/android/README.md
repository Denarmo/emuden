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

## Limits for now

- **Storage:** games are read by path. On Android 11 and later the app asks for "all files
  access" at every start until it is granted; without it the folder browser sees nothing
  under `/storage`. Folder grants (SAF) are not built. The folder browser starts at the
  shared storage root (`HOME` is set to it).
- **Cores:** the published plugin manifests have no `android-arm64` downloads for libretro
  cores yet, so every core console reports "not available on this device yet". Adding those
  downloads to `Denarmo/emuden-plugins` (signed index) is all that is missing on the app side.
- Secrets are kept in memory only (no Keystore), there is no second-screen surface, and the
  Switch 2 engine is not available on Android.
