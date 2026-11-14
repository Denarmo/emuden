# Engine contract

EmuDEN runs games in engines outside the app with the **Emulator Control Protocol (ECP)**:
the specification, schemas, SDKs and conformance probe are at
<https://github.com/Denarmo/emulator-control-protocol> (MIT). The EmuDEN-only level 1 and 2
design this file used to describe is replaced by it.

How EmuDEN maps the levels (`den::engine`, `engine/`):

| ECP level | In EmuDEN | Link |
|---|---|---|
| 0 | The Switch 2 engine: started with the game on its command line (`-f -g <game>`), the session ends when it exits. Found by the `engine.<system>.path` setting, `<data>/engines/<system>/`, or a desktop copy (`engine/src/host/locate.cpp`). Desktop only. | `ExitWaitLink` (`protocol/level0.cpp`) |
| 1, 2 | Engine apps a plugin manifest offers as one of a console's cores (`"type": "engine-app"`). Desktop: a child on `--ecp stdio` in a job object (Windows) or process group (Linux). Android: the PLAY intent and a loopback connection with a token. | `EcpLink` (`protocol/ecp_link.cpp`, `ecp/channel.cpp`) |

What EmuDEN uses of level 2: `pause`, `menu`, `states`, `state_thumbnails`, `reset`,
`fast_forward`, `rewind`, `settings`, `perf`, `volume`. See `docs/engines.md`.
