# FanCtl

**Native fan control for Apple Silicon Macs.** A menu-bar app with a Qt 6 / QML
interface and a small root helper that talks to the AppleSMC.

![FanCtl](docs/screenshot.png)

[中文说明](README.zh-CN.md)

---

## Why

macOS decides fan speeds on its own, and on Apple Silicon there is no user-facing
way to say “keep it at 2000 rpm” or “follow the CPU temperature”. FanCtl fills that
gap with a UI that stays out of the way: one window, a menu-bar icon, and a helper
that only exists so the app never has to run as root.

## Features

| | |
|---|---|
| **Three control modes** | *Auto* hands the fans back to macOS and shows the current speed read-only. *Manual* fixes a speed with a slider or a typed value. *Curve* follows a temperature sensor. |
| **Draggable fan curve** | Drag points on a live chart, double-click to add or remove nodes, watch the current temperature and actual rpm on the same graph. |
| **Presets** | Save named curves, switch with one click, delete what you no longer use. Stored in `QSettings`, survive updates. |
| **Live temperatures** | A fixed right-hand sidebar with the 12 most useful sensors, human-readable names (CPU Performance Core, GPU Cluster, Battery, …) instead of raw SMC keys. |
| **Day / night themes** | Warm light and warm dark palettes, remembered across launches. |
| **中文 / English** | Full UI switch, including sensor names and diagnostics. |
| **Menu-bar companion** | Closing the window keeps FanCtl running; the icon shows `自动` or the live rpm. Quit from the menu restores system control. |
| **Diagnostics built in** | *Diagnose* enumerates the SMC from user space; *Verify* writes a small rpm delta and checks the fans actually moved, then restores auto. |
| **CLI** | `fanctl` offers the same diagnostics for scripting and troubleshooting. |

<p align="center">
  <img src="docs/english.png" width="49%">
  <img src="docs/curve.png" width="49%">
</p>

## Requirements

- Apple Silicon Mac (arm64). Intel Macs are **not** supported by the prebuilt binary.
- macOS 13.0 or newer.
- To build: Xcode or the Command Line Tools, CMake ≥ 3.20, and Qt 6.7 (`brew install qt`).
- One administrator approval on first run, to install the background helper.

## Install

### Prebuilt

Download `FanCtl.zip` from [Releases](../../releases), unzip, and move `FanCtl.app`
to `/Applications`. The bundle ships its own copy of Qt, so nothing else is needed.

The app is ad-hoc signed, not notarized. On first launch macOS will refuse it —
right-click → **Open**, or:

```bash
xattr -dr com.apple.quarantine /Applications/FanCtl.app
```

### Build from source

```bash
git clone https://github.com/YF-7723/fanctl.git
cd fanctl
./build.sh          # -> build/FanCtl.app
open build/FanCtl.app
```

`build.sh` is a thin wrapper around CMake. It exists because on some machines
`/usr/bin/clang` refuses to run until the Xcode license is accepted; the script uses
Xcode's internal toolchain instead and pre-defines a CMake target to work around
Qt's obsolete `AGL.framework` reference.

To produce a self-contained bundle (Qt frameworks, plugins and QML modules embedded):

```bash
./package.sh        # -> dist/FanCtl.app, ~89 MB
```

## Usage

Open the app. The first time, click **Enable fan control** and authorize once — the
app installs its helper into `/usr/local/libexec/fanctl` and registers a LaunchDaemon.

**Auto** — fans are managed by macOS. The panel is read-only and shows what the
system is doing.

**Manual** — drag the slider or type a number. The blue knob is your target, the
thin white tick is the actual speed; they separate while the fans spin up.

**Curve** — pick one of the 12 sensors, then drag the points. Below the first point
the fans hold its value; above the last point they hold that one. The dashed
vertical line is the current temperature, the chip in the corner is the live rpm.

Closing the window does not quit: FanCtl keeps running in the menu bar and keeps
applying your setting. Choose **Quit FanCtl** from the menu-bar icon to exit — the
fans are handed back to macOS.

### Command line

```bash
fanctl status                     # fans, mode, temperatures
fanctl sensors                    # temperatures only
fanctl auto                       # hand fans back to macOS
fanctl set all 2500               # fixed speed, all fans
fanctl set 0 3000                 # only fan 0
fanctl curve Tp01 35 1500 50 2500 70 4000 88 6000
fanctl doctor                     # read-only SMC health check
fanctl selftest                   # prove control works, restores auto
```

## How it works

```
FanCtl.app (QML)                fanctld (root, LaunchDaemon)
      │                                  │
      │  unix socket, 0660 root:admin    │  IOKit / AppleSMC
      └──── STATUS · MANUAL · CURVE ─────┘
                                     read: unprivileged
                                     write: root only
```

Reading the SMC needs no privileges, so the UI reads temperatures directly. Writing
does need root, so every write goes through a ~150 KB daemon that the app installs
itself. The daemon owns the control loop: it re-applies the target every two seconds
(so macOS cannot quietly take the fans back), caches sensor readings for cheap
status polls, and restores automatic control when it is stopped.

The SMC protocol itself is an 80-byte struct sent through `IOConnectCallStructMethod`
with selector 2, where one byte selects read (5), write (6), enumerate (8) or key
info (9). `src/core/smc.cpp` implements it from scratch.

One quirk worth knowing: not every Mac uses the classic big-endian layout. The M1 Pro
this was developed on stores some keys **reversed** (`FNum` is literally `muNF`) and
values **little-endian**. FanCtl enumerates the key space once to build an alias
table, then probes `F0Mn`/`F0Mx` to pick the right byte order at runtime, so the same
binary works on both conventions. The *Diagnose* panel reports which one it found.

## Safety

This app writes to the SMC. Wrong values can stop the fans, make them noisy, or
leave the machine under-cooled. There is no warranty — see [LICENSE](LICENSE).

A few deliberate choices keep it boring:

- Writes are clamped to each fan's `F{i}Mn`…`F{i}Mx` range.
- The daemon restores automatic control on `SIGTERM`, on uninstall, and when you quit.
- *Verify* always ends by restoring auto, whatever the result.
- The UI refuses to write when the helper reports an incompatible protocol version.

## Troubleshooting

**“Helper isn't running”** — click **Start** in the window (one approval), or:

```bash
./restart-daemon.command
```

**“Update helper” badge** — the daemon is older than the app. Run `./install.command`.

**No fans detected** — the machine may have none (MacBook Air), or the SMC may not
expose `FNum`. `fanctl doctor` prints exactly what it found.

Logs: `tail -20 /var/log/fanctl.log`, job state: `launchctl print system/com.fanctl.daemon`.

## Project layout

```
src/core/     AppleSMC client, IPC protocol, QML-facing controller
src/app/      QML engine entry point + macOS window/tray glue (Objective-C++)
src/daemon/   fanctld, the root helper (no Qt dependency)
src/cli/      fanctl
qml/          theme, cards, panels, the curve chart
packaging/    Info.plist and the LaunchDaemon plist shipped inside the bundle
```

## License

MIT — see [LICENSE](LICENSE).
