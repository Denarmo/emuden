# Linux shell

Packaging only: the desktop entry, the icon and the AppImage entry point. The app itself is
the same binary as everywhere; `tools/linux/build.sh` builds it and `tools/linux/package.sh`
wraps it into `EmuDEN-x86_64.AppImage`.

- Data lives in `$XDG_DATA_HOME/EmuDEN` (default `~/.local/share/EmuDEN`), or in a `user`
  folder beside the executable when that exists. An AppImage is portable the same way:
  create a `user` folder beside the AppImage file (the app reads `$APPIMAGE`).
- The AppImage carries `emuden.desktop` and `emuden.svg` (also as `.DirIcon`), so launchers
  that integrate AppImages show the name and icon.
- HTTP uses the system's libcurl, loaded at run time (`libcurl.so.4`, else
  `libcurl-gnutls.so.4`). libstdc++ is linked statically; the binary needs glibc 2.38.
- The in-app browser uses the system's WebKitGTK (`libwebkit2gtk-4.1.so.0`), loaded at run
  time on X11 only; it is never a dependency of the binary. Where it is missing (SteamOS)
  the page opens the site in the user's browser and picks up the download from the
  Downloads folder instead (`docs/in-app-browser.md`).
- SteamOS: `tools/linux/install-steamos.sh <AppImage>` (run on the device, no root) copies
  the AppImage to `~/Games/EmuDEN/`, adds a desktop entry with the icon to the menu and the
  desktop, and with `--portable` creates the `user` folder there; `--steam` also adds it to
  Steam as a non-Steam game. Under gamescope the app starts full screen with the handheld layout.
- `AppRun` sets `GLIBC_TUNABLES=glibc.rtld.execstack=2`: glibc 2.41 and later (SteamOS)
  refuse to load a core that asks for an executable stack (melonDS) without it.
- In Game Mode only what Steam starts is shown, and links open in Steam's own browser.
  Results on the Legion Go S: `docs/steamos.md`.
