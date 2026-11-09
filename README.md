# FanCtl

**Native fan control for Apple Silicon Macs.** · **Apple Silicon Mac 原生风扇控制。**

![FanCtl](docs/screenshot.png)

[English](#fanc) · [中文](#中文)

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

---

## 中文

### 为什么做它

macOS 自己决定风扇转速，Apple Silicon 上也没有任何官方途径说「保持在 2000 转」或者
「跟着 CPU 温度走」。FanCtl 补上这个缺口：一个窗口、一个菜单栏图标，以及一个存在的
唯一理由就是「让界面本身不必以 root 运行」的后台组件。

### 功能

| | |
|---|---|
| **三种模式** | *自动*：交还 macOS，只读显示当前转速。*手动*：滑杆或直接输入固定转速。*曲线*：跟随某个温度传感器。 |
| **可拖拽风扇曲线** | 在实时图表上拖节点调整，双击空白加点、双击节点删点；同一张图里能看到当前温度和实际转速。 |
| **预设** | 保存命名曲线，一键切换、随时删除。存在 `QSettings` 里，升级不丢。 |
| **实时温度** | 右侧固定栏里 12 个最常用的传感器，显示成人能看懂的名字（CPU 性能核、GPU 集群、电池……）而不是 SMC 原始 key。 |
| **日夜双主题** | 暖白 / 暖褐两套配色，选择被记住。 |
| **中英文切换** | 整个界面都能切，包括传感器名称和诊断输出。 |
| **菜单栏常驻** | 关窗不退出；图标显示「自动」或实时转速。从菜单里「退出」才真正结束并交还系统控制。 |
| **内置诊断** | *诊断* 以普通用户身份枚举 SMC；*验证* 写入一个小幅转速并确认真实变化，无论成败都会还原。 |
| **命令行** | `fanctl` 提供同一套诊断，方便脚本化和排错。 |

<p align="center">
  <img src="docs/english.png" width="49%">
  <img src="docs/curve.png" width="49%">
</p>

### 环境要求

- Apple Silicon Mac（arm64）。预编译版本**不支持** Intel Mac。
- macOS 13.0 及以上。
- 自行编译需要：Xcode 或命令行工具、CMake ≥ 3.20、Qt 6.7（`brew install qt`）。
- 首次运行需要一次管理员授权，用来安装后台组件。

### 安装

#### 直接下载

从 [Releases](../../releases) 下载 `FanCtl.zip`，解压后把 `FanCtl.app` 拖进 `/Applications`。
bundle 里已经带了自己的 Qt，不需要额外装东西。

App 只做了 ad-hoc 签名、未公证，首次打开会被 Gatekeeper 拦住——右键 →「打开」，或者：

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

`build.sh` 只是 CMake 的薄封装。它存在的原因是：某些机器上 `/usr/bin/clang` 会因为
「未接受 Xcode 许可」直接拒绝运行，脚本改用 Xcode 内部工具链，并在 CMake 里预先定义
一个 target 绕开 Qt 对早已消失的 `AGL.framework` 的引用。

要打出自包含的包（把 Qt 框架、插件、QML 模块全部塞进 bundle）：

```bash
./package.sh        # 产出 dist/FanCtl.app，约 89 MB
```

### 使用

打开 App，第一次点「启用风扇控制」并授权一次——它会把 helper 装到
`/usr/local/libexec/fanctl` 并注册 LaunchDaemon。

**自动**：风扇完全交给 macOS，面板只读。

**手动**：拖滑杆或输入数字。蓝色圆点是目标，白色细刻度是实际转速；加速过程中两者会分开。

**曲线**：从 12 个传感器里选一个，然后拖节点。低于首点按首点转速，高于末点按末点转速。
虚线是当前温度，角落里的小胶囊是实时转速。

关闭窗口不会退出：FanCtl 继续在菜单栏运行并保持你的设置。要从菜单栏图标里选
**退出 FanCtl** 才结束，退出时风扇交还 macOS。

#### 命令行

```bash
fanctl status                     # 风扇、模式、温度
fanctl sensors                    # 只看温度
fanctl auto                       # 交还系统
fanctl set all 2500               # 全部固定 2500
fanctl set 0 3000                 # 只改 1 号风扇
fanctl curve Tp01 35 1500 50 2500 70 4000 88 6000
fanctl doctor                     # 只读 SMC 体检
fanctl selftest                   # 端到端验证，结束自动还原
```

### 实现原理

```
FanCtl.app (QML)                fanctld (root, LaunchDaemon)
      │                                  │
      │  unix socket, 0660 root:admin    │  IOKit / AppleSMC
      └──── STATUS · MANUAL · CURVE ─────┘
                                     读：免权限
                                     写：需要 root
```

读 SMC 不需要特权，所以界面直接读温度；写需要 root，于是所有写操作都交给一个约
150 KB 的守护进程，由 App 自己安装。控制循环在守护进程里：每两秒重新施加一次目标
（防止 macOS 悄悄把控制权拿回去），传感器读数缓存在后台线程里让状态查询保持廉价，
被终止时自动交还系统控制。

SMC 协议本身是一个 80 字节结构体，通过 `IOConnectCallStructMethod` 以 selector 2 发送，
其中一个字节决定操作：读（5）、写（6）、枚举（8）、读 key 信息（9）。
`src/core/smc.cpp` 是从零实现的。

有个值得知道的坑：并非所有 Mac 都用经典的大端布局。开发用的这台 M1 Pro 上，部分 key
是**倒序存储**的（`FNum` 实际存成 `muNF`），数值是**小端**。FanCtl 会枚举一次 key 空间
建立别名表，再用 `F0Mn`/`F0Mx` 的读数探测字节序，因此同一份二进制在两种约定下都能工作。
「诊断」面板会告诉你它识别到的是哪一种。

### 安全

这个程序会写 SMC。写错参数可能导致风扇停转、噪音变大或散热不足，不提供任何担保——
见 [LICENSE](LICENSE)。

几个刻意的设计让它尽量「无聊」：

- 写入会被夹到每台风扇的 `F{i}Mn`…`F{i}Mx` 区间；
- 守护进程在 `SIGTERM`、卸载、以及你退出 App 时都会还原自动控制；
- 「验证」无论成功失败，结束时一定还原自动；
- 后台组件报告协议版本不兼容时，界面会拒绝写入。

### 排错

**「后台组件没有运行」**：点窗口里的「启动」（需要一次授权），或者：

```bash
./restart-daemon.command
```

**标题栏显示「组件需更新」**：守护进程比 App 旧，跑一次 `./install.command`。

**没有检测到风扇**：这台机器可能本来就没有（MacBook Air），也可能 SMC 没有暴露
`FNum`。`fanctl doctor` 会打印它到底看到了什么。

日志：`tail -20 /var/log/fanctl.log`；任务状态：`launchctl print system/com.fanctl.daemon`。

### 目录结构

```
src/core/     AppleSMC 客户端、IPC 协议、面向 QML 的控制器
src/app/      QML 引擎入口 + macOS 窗口/菜单栏桥接（Objective-C++）
src/daemon/   fanctld，root 守护进程（不依赖 Qt）
src/cli/      fanctl
qml/          主题、卡片、面板、曲线图表
packaging/    Info.plist 与随包分发的 LaunchDaemon 配置
```

### 许可

MIT，见 [LICENSE](LICENSE)。
