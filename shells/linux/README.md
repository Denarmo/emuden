# Linux shell

Packaging only: the desktop entry, the icon and the AppImage entry point. The app itself is
the same binary as everywhere; `tools/linux/build.sh` builds it and `tools/linux/package.sh`
wraps it into `EmuDEN-x86_64.AppImage`.

- Data lives in `$XDG_DATA_HOME/EmuDEN` (default `~/.local/share/EmuDEN`), or in a `user`
  folder beside the executable when that exists. Inside an AppImage there is no such folder.
- HTTP uses the system's libcurl, loaded at run time (`libcurl.so.4`, else
  `libcurl-gnutls.so.4`). libstdc++ is linked statically; the binary needs glibc 2.38.
- SteamOS: copy the AppImage to `~/Games/EmuDEN/`, `chmod +x`, and add it to Steam as a
  non-Steam game. Under gamescope the app starts full screen with the handheld layout.
