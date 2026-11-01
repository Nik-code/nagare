# Nagare for Windows

A native C++20 client on top of the same `libtransmission` as the Mac app, drawn with Direct2D and a small Direct3D 11 glass pass. No web view, no runtime to install, one executable.

## Layout

| Folder | What it holds |
| --- | --- |
| `src/engine` | The engine interface, its libtransmission implementation and the demo engine |
| `src/gfx` | The renderer |
| `src/ui` | Views, widgets, the window and the theme |
| `src/app` | The screens, and the `shell_*` files that tie the app into Windows |
| `assets` | Art, icons and the Outfit font, embedded in the executable by CMake |
| `resources` | Icon, version, manifest. `version.h` is the one place the version lives |
| `installer` | The Inno Setup script and a draft winget manifest |
| `dev` | Scripts that build, screenshot, measure and package on the Windows PC |

## Building

The build runs on Windows, driven from the Mac over SSH. Set `NAGARE_WINDOWS_HOST` to its SSH host. The PC needs:

- Visual Studio 2022 Build Tools with the C++ workload, CMake, Ninja and the Windows 11 SDK 10.0.26100
- `C:\dev` for the engine. `dev/remote-engine.ps1` clones the source and vcpkg there and builds `libtransmission` statically for speed. `dev/engine-sync.sh` sends local engine changes there and rebuilds
- For packaging, Inno Setup under `C:\dev\tools\inno`. Its normal installer takes `/PORTABLE=1 /CURRENTUSER /DIR=C:\dev\tools\inno`, which leaves nothing in the registry

```bash
dev/build.sh                               # build
dev/shot.sh out.png --demo                 # run it and fetch a screenshot
dev/package.sh                             # zip and installer into windows/dist
```

`NAGARE_SLOT` keeps parallel worktrees in their own folders under `C:\dev\slots`.

## Flags

| Flag | What it does |
| --- | --- |
| `--demo`, `--demo-light` | Canned transfers, with no session, network or disk |
| `--shot <path>` | Captures a frame to a PNG and exits |
| `--open <screen>` | Opens `add`, `link`, `search`, `details`, `settings`, `default` or `quit` |
| `--light`, `--dark` | Forces the theme |
| `--widgets`, `--widgets-test` | The widget gallery and its self test |
| `--engine-test <folder>` | Engine checks, without a window, logged to the folder |
| `--shell-test <folder>` | Registry and command line checks under test names, without a window |
| `--config <folder>` | A separate profile. Stays out of the registry |
| `--tray` | Turns the tray on for a side by side run, such as `--demo --tray` |
| `--hidden` | Starts in the tray, which is how Start with Windows launches it |
| `--register`, `--unregister` | Used by the installer. `--unregister` also closes a running Nagare |

The demo, the screenshots and the tests run side by side, so they skip the instance lock, the registry and the tray.

## What stays out of the way

Per-user everything: `HKCU` for file handling, `%LOCALAPPDATA%\Programs\Nagare` for the installed app and `%LOCALAPPDATA%\Nagare` for settings and transfers. Registering adds Nagare to the list of choices and overrides nothing, because only the user can change a default on Windows 10 and 11. The installer never asks for administrator rights. There is no auto-updater, background service or scheduled task.

The minimum is Windows 10 1809. Rounded corners and the dark title bar are the only things that need something newer, and both fall back quietly. The glass is drawn by the app, so it looks the same everywhere.
