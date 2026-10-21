# SPDX-License-Identifier: MIT
# PlatformIO post-build: write .pio/build/<env>/lynx.bin, one image with bootloader, partition
# table, boot_app0 and the app at their offsets, flashable at 0x0 (what ESP Web Tools expects).
Import("env")  # noqa: F821 (provided by PlatformIO)


def merge(source, target, env):
    build = env.subst("$BUILD_DIR")
    images = [(off, env.subst(path)) for off, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET"), env.subst("$BUILD_DIR/${PROGNAME}.bin")))
    args = [f'"{p}"' if " " in p else p for off, p in images for p in (off, p)]
    cmd = " ".join([
        '"$PYTHONEXE"', '"$OBJCOPY"', "--chip", env.BoardConfig().get("build.mcu"), "merge_bin",
        "-o", f"{build}/lynx.bin", "--flash_mode", "keep", "--flash_size", "keep", *args,
    ])
    env.Execute(cmd)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
