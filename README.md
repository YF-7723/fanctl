# FanCtl

Fan control for Apple Silicon Macs. A Qt 6 / QML window, a menu-bar icon, and a small
root helper that writes to the SMC.

![FanCtl](docs/screenshot.png)

[English](#why) · [中文](#中文)

---

## Why

macOS sets fan speed by itself. On Apple Silicon there is no switch for "keep it at
2000 rpm", and no way to tie the fans to one temperature sensor. That is what this app
is for.

## What it does

**Three modes.** Auto gives the fans back to macOS and only shows the speed. Manual holds
a speed you pick. Curve follows a sensor you pick.

**You draw the curve.** Drag the points. Double-click empty space to add one, double-click
a point to delete it. The chart shows the current temperature and the real rpm, so you can
tell whether the curve is doing what you asked.

**Presets.** Name a curve, save it, click to switch. They live in QSettings and survive
updates.

**Temperatures on the right.** Twelve sensors, named "CPU Performance Core" instead of the
raw SMC key. They stay in the same order when the numbers change.

**Light and dark.** Both warm. It remembers which one you picked.

**Chinese and English.** The whole window switches, sensor names included.

**Menu bar.** Closing the window does not quit. The icon shows the current speed. Pick
Quit to stop and hand the fans back to macOS.

**Checks built in.** Diagnose reads the SMC and writes nothing. Verify writes a small
change, checks the fans actually moved, then puts them back.

**A CLI.** `fanctl` does the same from a terminal.

<p align="center">
  <img src="docs/english.png" width="49%">
  <img src="docs/curve.png" width="49%">
</p>

## Requirements

- Apple Silicon Mac. The prebuilt app is arm64 only.
- macOS 13 or newer.
- To build: Xcode or the Command Line Tools, CMake 3.20+, Qt 6.7 (`brew install qt`).
- Admin rights once, to install the helper.

## Install

### Prebuilt

Download `FanCtl.zip` from [Releases](../../releases), unzip it, drag `FanCtl.app` into
`/Applications`. Qt is inside the bundle, so there is nothing else to install.

The app is signed ad-hoc, not notarized, so macOS blocks it the first time. Right-click
and choose Open, or run:

```bash
xattr -dr com.apple.quarantine /Applications/FanCtl.app
```

### Build

```bash
git clone https://github.com/YF-7723/fanctl.git
cd fanctl
./build.sh          # -> build/FanCtl.app
open build/FanCtl.app
```

`build.sh` is a thin wrapper around CMake. On some machines `/usr/bin/clang` refuses to
run until the Xcode licence is accepted, so the script calls Xcode's own toolchain. It
also defines a CMake target so Qt stops looking for the AGL framework that disappeared
years ago.

To make a bundle you can copy to another Mac:

```bash
./package.sh        # -> dist/FanCtl.app, about 89 MB
```

## Usage

Open the app. The first time, click Enable fan control and approve once. That installs the
helper into `/usr/local/libexec/fanctl` and registers a LaunchDaemon.

**Auto.** macOS controls the fans. The panel only shows the current speed.

**Manual.** Use the slider or type a number. The blue dot is your target, the thin white
line is the real speed. They drift apart while the fans spin up.

**Curve.** Pick one of the twelve sensors, then drag the points. Below the first point the
fans hold its value; above the last point they hold the last one. The dashed line is the
current temperature, the small label in the corner is the live rpm.

Closing the window does not quit. FanCtl keeps running in the menu bar and keeps your
setting. To stop it, choose Quit FanCtl from the menu-bar icon. The fans go back to macOS.

### CLI

```bash
fanctl status                     # fans, mode, temperatures
fanctl sensors                    # temperatures only
fanctl auto                       # give the fans back to macOS
fanctl set all 2500               # fixed speed, all fans
fanctl set 0 3000                 # only fan 0
fanctl curve Tp01 35 1500 50 2500 70 4000 88 6000
fanctl doctor                     # read-only SMC check
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

Reading the SMC needs no privileges, so the window reads temperatures directly. Writing
does, so every write goes through a daemon of about 150 KB that the app installs itself.

The daemon runs the control loop. It writes the target again every two seconds, otherwise
macOS would quietly take the fans back. It caches sensor readings so a status call stays
cheap. When it stops, it restores automatic control.

The protocol is an 80-byte struct sent with `IOConnectCallStructMethod`, selector 2. One
byte picks the operation: 5 read, 6 write, 8 enumerate, 9 key info. `src/core/smc.cpp`
implements it from scratch.

Not every Mac uses the classic big-endian layout. The M1 Pro this was written on stores
some keys reversed (`FNum` is literally `muNF`) and values little-endian. FanCtl
enumerates the key space once to build an alias table, then probes `F0Mn` and `F0Mx` to
work out the byte order at runtime. Diagnose tells you which one it found.

## Safety

This app writes to the SMC. A wrong value can stop the fans or leave the machine too hot.
No warranty. See [LICENSE](LICENSE).

Four things keep it out of trouble:

- Values are clamped to each fan's `F{i}Mn`…`F{i}Mx` range.
- The daemon restores auto on `SIGTERM`, on uninstall, and when you quit.
- Verify always restores auto, whether it passes or fails.
- The app will not write if the helper reports a protocol version it does not know.

## Troubleshooting

**Helper isn't running.** Click Start in the window and approve once. Or:

```bash
./restart-daemon.command
```

**Update helper badge.** The daemon is older than the app. Run `./install.command`.

**No fans detected.** The machine may not have any (MacBook Air), or the SMC does not
expose `FNum`. `fanctl doctor` prints what it found.

Logs: `tail -20 /var/log/fanctl.log`. Job state: `launchctl print system/com.fanctl.daemon`.

## Project layout

```
src/core/     AppleSMC client, IPC protocol, QML-facing controller
src/app/      QML entry point + macOS window and menu bar glue (Objective-C++)
src/daemon/   fanctld, the root helper (no Qt dependency)
src/cli/      fanctl
qml/          theme, cards, panels, the curve chart
packaging/    Info.plist and the LaunchDaemon plist shipped inside the bundle
```

## License

MIT. See [LICENSE](LICENSE).

---

## 中文

### 为什么做

macOS 自己决定风扇转速。Apple Silicon 上没有开关能设成固定的 2000 转，也没法让风扇跟着
某一个温度传感器走。这个程序做的就是这件事。

### 能做什么

**三种模式。** 自动把风扇交回 macOS，只显示转速。手动固定在你选的速度。曲线跟着你选的
传感器走。

**曲线自己画。** 拖动节点就行。双击空白处加点，双击节点删掉。图上同时有当前温度和实际
转速，能看出曲线有没有按你想的工作。

**预设。** 起个名字存下来，点一下切过去。存在 QSettings 里，升级不会丢。

**右侧温度。** 十二个传感器，名字是「CPU 性能核」这种，不是 SMC 原始 key。数字变了，
名字也不会跳位置。

**日夜主题。** 两套都是暖色，选过会记住。

**中英文。** 整个窗口都会切，传感器名字也是。

**菜单栏。** 关掉窗口不会退出，图标上显示当前转速。想停就选「退出」，风扇交回 macOS。

**自带检查。** 诊断只读，不写任何东西。验证写一个小变化，看风扇动没动，然后再放回去。

**命令行。** `fanctl` 在终端里做同样的事。

### 环境要求

- Apple Silicon Mac。预编译版本只有 arm64。
- macOS 13 或更新。
- 自己编译需要 Xcode 或命令行工具、CMake 3.20+、Qt 6.7（`brew install qt`）。
- 装后台组件时要授权一次。

### 安装

#### 直接下载

从 [Releases](../../releases) 下载 `FanCtl.zip`，解压，把 `FanCtl.app` 拖进 `/Applications`。
Qt 已经在 bundle 里，不用另外装。

App 只做了 ad-hoc 签名，没有公证，第一次打开会被 macOS 拦住。右键选「打开」，或者：

```bash
xattr -dr com.apple.quarantine /Applications/FanCtl.app
```

#### 从源码编译

```bash
git clone https://github.com/YF-7723/fanctl.git
cd fanctl
./build.sh          # 产出 build/FanCtl.app
open build/FanCtl.app
```

`build.sh` 只是 CMake 外面套了一层。有些机器上 `/usr/bin/clang` 会因为没接受 Xcode 许可
拒绝运行，所以脚本改用 Xcode 自己的工具链；另外在 CMake 里先定义一个 target，免得 Qt 去找
早就没了的 AGL framework。

想拷到别的 Mac 上跑，先打个自带 Qt 的包：

```bash
./package.sh        # 产出 dist/FanCtl.app，约 89 MB
```

### 使用

打开 App，第一次点「启用风扇控制」，授权一次。它会把 helper 装到
`/usr/local/libexec/fanctl`，再注册一个 LaunchDaemon。

**自动。** 风扇归 macOS 管，面板只显示当前转速。

**手动。** 拖滑杆或者输数字。蓝色圆点是目标，白色细线是实际转速，风扇加速时两者会分开。

**曲线。** 从十二个传感器里选一个，然后拖节点。低于第一个点就按它的转速，高于最后一个点
就按最后一个。虚线是当前温度，角落里的小标签是实时转速。

关掉窗口不会退出。FanCtl 继续在菜单栏跑着，也继续用你的设置。要停下来，点菜单栏图标里的
「退出 FanCtl」，风扇交回 macOS。

#### 命令行

```bash
fanctl status                     # 风扇、模式、温度
fanctl sensors                    # 只看温度
fanctl auto                       # 交还系统
fanctl set all 2500               # 全部固定 2500
fanctl set 0 3000                 # 只改 1 号风扇
fanctl curve Tp01 35 1500 50 2500 70 4000 88 6000
fanctl doctor                     # 只读检查 SMC
fanctl selftest                   # 实际验证一遍，结束自动还原
```

### 实现

```
FanCtl.app (QML)                fanctld (root, LaunchDaemon)
      │                                  │
      │  unix socket, 0660 root:admin    │  IOKit / AppleSMC
      └──── STATUS · MANUAL · CURVE ─────┘
                                     读：不需要权限
                                     写：需要 root
```

读 SMC 不需要权限，所以窗口自己读温度。写需要 root，所以每次写都交给一个约 150 KB 的
守护进程，由 App 自己装上。

控制循环在守护进程里。它每两秒重新写一次目标，不然 macOS 会悄悄把风扇拿回去；传感器读数
缓存在后台线程，状态查询就不用每次都读一遍；被停掉时自动还原。

协议是一个 80 字节的结构体，用 `IOConnectCallStructMethod` 发出去，selector 是 2。
其中一个字节决定做什么：5 读、6 写、8 枚举、9 读 key 信息。`src/core/smc.cpp` 从零写的。

不是每台 Mac 都用经典的大端布局。写这个程序的那台 M1 Pro 上，部分 key 是倒着存的
（`FNum` 实际存成 `muNF`），数值是小端。FanCtl 先枚举一遍 key 空间建一张别名表，再用
`F0Mn`、`F0Mx` 的读数判断该用哪种字节序，所以同一份二进制两边都能跑。诊断里会写它认出来的是
哪一种。

### 安全

这个程序会写 SMC。参数写错可能让风扇停转，或者让机器散热不够。没有担保，见 [LICENSE](LICENSE)。

四件事让它不至于出事：

- 写入会夹在每台风扇的 `F{i}Mn`…`F{i}Mx` 之间；
- 守护进程收到 `SIGTERM`、被卸载、或者你退出 App 时，都会还原自动控制；
- 验证不管成功还是失败，最后都还原；
- 后台组件报的协议版本对不上时，App 不会写。

### 排错

**「后台组件没有运行」。** 点窗口里的「启动」，授权一次。或者：

```bash
./restart-daemon.command
```

**标题栏显示「组件需更新」。** 守护进程比 App 旧，跑一次 `./install.command`。

**没检测到风扇。** 这台机器可能本来就没有（MacBook Air），也可能 SMC 没暴露 `FNum`。
`fanctl doctor` 会打印它到底看到了什么。

日志：`tail -20 /var/log/fanctl.log`。任务状态：`launchctl print system/com.fanctl.daemon`。

### 目录结构

```
src/core/     AppleSMC 客户端、IPC 协议、给 QML 用的控制器
src/app/      QML 入口 + macOS 窗口和菜单栏的胶水（Objective-C++）
src/daemon/   fanctld，root 守护进程（不依赖 Qt）
src/cli/      fanctl
qml/          主题、卡片、面板、曲线图
packaging/    Info.plist 和随包分发的 LaunchDaemon 配置
```

### 许可

MIT，见 [LICENSE](LICENSE)。
