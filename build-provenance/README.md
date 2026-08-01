# Lenovo TB132FU KGDB build provenance

This source snapshot produced the KGDB/watchdog-safe kernel artifacts built on
2026-08-01. The original source drop was
`tb132fu_opensource_tb132fu_s000018_220516_row.tar.gz`, directory
`kernel-4.14`.

- Source archive size: `1,786,282,010` bytes
- Source archive SHA-256:
  `25a12cc567ec2b097c532b7a92aa0c7ebc8cd121ebf94ccba35c2fb54f8b6b11`

## Target

- Device: Lenovo TB132FU / P11 Pro Gen 2
- SoC configuration: `CONFIG_MACH_MT6893=y`
- MediaTek platform: `CONFIG_MTK_PLATFORM="mt6885"`
- Project: `CONFIG_ARCH_MTK_PROJECT="p11_pro_2gen"`
- Kernel: Linux 4.14.186+

## Toolchain

- LineageOS Clang: `clang-r522817`
- Clang/LLD: 18.0.1
- Android GCC cross-prefix:
  `/home/royna/los/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-`
- Build identity embedded in `vmlinux`:
  `Linux version 4.14.186+ (royna@MyLittleAMD) (Android (11967740, +pgo, +bolt, +lto, +mlgo, based on r522817) clang version 18.0.1 (https://android.googlesource.com/toolchain/llvm-project d8003a456d14a3deb8054cdaa529ffbf02d9b262), LLD 18.0.1) #4 SMP PREEMPT Sat Aug 1 07:20:23 KST 2026`

## Configuration proof

- Defconfig: `arch/arm64/configs/p11_pro_2gen_kgdb_defconfig`
- Defconfig SHA-256:
  `a363d0d6c1053debb409f47b3cf0c0b7defb88d168c2caa0d4aed4b3a789efec`
- Resolved config: `build-provenance/p11_pro_2gen_kgdb.config`
- Resolved config SHA-256:
  `59db44c21b0fc91ae6bf8b2de501cdfb4a827e4af6575eff85a2405f72e574f9`
- `CONFIG_IKCONFIG=y` is enabled. Running
  `scripts/extract-ikconfig vmlinux` reproduced the checked-in resolved config
  byte-for-byte with the same SHA-256.

## Artifact hashes

- `vmlinux`:
  `a72b160e71966a7de64b7935431402e4072aaff926b1bbdb7614c2f598301070`
- `Image.gz`:
  `a6f158477b8e8a9b16443368125625fd677a57b9ccf92fe7903cad290a9ebf15`
- `tb132fu-kgdb-nowdt-wait.img`:
  `e811abc4ed06847feaccb031bc13e3241790640ebbc7307ffd9d6ca95b37edb2`
- `tb132fu-kgdb-nowdt-verbose.img`:
  `b3efa868e5177e0bd9bced7cf4475fe4231a5b8610fa8698c263bcafd2458a94`

The two boot images contain the same `Image.gz`; only their boot command lines
differ. The `wait` image adds `kgdbwait`.

## Build command

```sh
make -j16 O=/home/royna/lenovo/out-kgdb \
  ARCH=arm64 \
  CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm \
  OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
  HOSTCC=clang HOSTCXX=clang++ CLANG_TRIPLE=aarch64-linux-gnu- \
  CROSS_COMPILE=/home/royna/los/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  KCFLAGS='-Wno-error -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=unused-but-set-variable' \
  Image.gz dtbs
```

At the owner's request, the `stock` branch is intentionally a single root
commit of the exact current, built-and-tested source snapshot plus this
provenance material. It is therefore the source corresponding to the delivered
KGDB `vmlinux`, not an untouched vendor baseline despite the branch name.
Generated `out/` trees and the
316 MiB `vmlinux` are excluded from Git because they are build artifacts and
exceed GitHub's normal object limit. The canonical `vmlinux` copy is stored at
`G:\My Drive\Lenovo\vmlinux` and is identified by the SHA-256 above.
