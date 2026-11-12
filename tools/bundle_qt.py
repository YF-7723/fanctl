#!/usr/bin/env python3
"""把 bundle 里所有 @rpath 依赖对应的 Qt 框架补齐。

比解析运行时错误可靠：直接读 Mach-O 的 LC_LOAD_DYLIB，
只要出现 @rpath/X.framework 而 Frameworks 里没有 X，就从 Qt 目录拷过来。
"""
import os, shutil, struct, subprocess, sys

QT_LIB = sys.argv[1] if len(sys.argv) > 1 else "/opt/homebrew/opt/qt6/lib"
APP = sys.argv[2] if len(sys.argv) > 2 else "dist/FanCtl.app"

LC_LOAD_DYLIB = 0xc
LC_LOAD_WEAK_DYLIB = 0x80000018
LC_REEXPORT_DYLIB = 0x8000001f


def macho_slices(data):
    """返回 [(offset, size)]，兼容 fat 和 thin"""
    magic = struct.unpack_from(">I", data, 0)[0]
    if magic in (0xcafebabe, 0xcafebabf):
        n = struct.unpack_from(">I", data, 4)[0]
        out = []
        for i in range(n):
            base = 8 + i * 20
            if magic == 0xcafebabf:
                base = 8 + i * 32
            off, size = struct.unpack_from(">II", data, base + 8)
            out.append((off, size))
        return out
    return [(0, len(data))]


def deps(path):
    try:
        data = open(path, "rb").read()
    except OSError:
        return []
    found = []
    for off, _size in macho_slices(data):
        try:
            magic = struct.unpack_from("<I", data, off)[0]
        except struct.error:
            continue
        if magic != 0xfeedfacf:
            continue
        ncmds = struct.unpack_from("<I", data, off + 16)[0]
        p = off + 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", data, p)
            if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB):
                s_off = struct.unpack_from("<I", data, p + 8)[0]
                s = data[p + s_off:p + size].split(b"\0")[0].decode("utf-8", "replace")
                found.append(s)
            p += size
    return found


def all_macho(root):
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            with open(p, "rb") as fh:
                head = fh.read(4)
            if head[:4] in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
                yield p


def main():
    fw_dir = os.path.join(APP, "Contents", "Frameworks")
    os.makedirs(fw_dir, exist_ok=True)
    added = []
    for _ in range(30):
        missing = set()
        for path in all_macho(APP):
            for dep in deps(path):
                if not dep.startswith("@rpath/"):
                    continue
                name = dep[len("@rpath/"):].split("/")[0]
                if not name.endswith(".framework"):
                    continue
                if not os.path.exists(os.path.join(fw_dir, name)):
                    missing.add(name)
        if not missing:
            break
        for name in sorted(missing):
            src = os.path.join(QT_LIB, name)
            if not os.path.isdir(src):
                print(f"  ! {name} 不在 {QT_LIB}")
                continue
            print(f"  + {name}")
            shutil.copytree(src, os.path.join(fw_dir, name), dirs_exist_ok=True)
            added.append(name)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", APP],
                       capture_output=True)
    print(f"补齐 {len(added)} 个框架")


if __name__ == "__main__":
    main()
