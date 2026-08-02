# Lenovo TB132FU kernel — Alex

Alex is a reconstructed Linux 4.14.186 source tree for the Lenovo TB132FU
(Tab P11 Pro Gen 2) and its MediaTek MT6893 platform. It is intended for device
bring-up, KGDB diagnosis, hardware-fault investigation and maintainable kernel
repair work.

Unlike Lenovo's standalone source snapshot, this branch starts from the closest
public Motorola/MediaTek history, commits the pristine Lenovo source as a
separate auditable import, and then applies the TB132FU fixes and debugging work
as normal commits. The matching MediaTek connectivity drivers are included as
full-history Git subtrees instead of being left as unavailable external paths.

## What is included

- The complete non-generated `kernel-4.14/` source from Lenovo's S000018 drop.
- TB132FU build fixes, boot hardening, touch correction and SMP safety fixes.
- ARM64 KGDB hardware-breakpoint and concurrency support.
- Opt-in early memory tests, MediaTek hardware diagnostics, and cache-parity/RAS
  instrumentation and containment.
- MT6893 conninfra, WLAN gen4m/adaptor, Bluetooth, FM and GPS source trees.

Generated vendor `out/` files, prebuilt kernels and stale artifact hashes are
not included. Potentially unsafe diagnostic behavior remains opt-in through the
dedicated KGDB configuration and kernel command-line controls.

## Configurations

- `p11_pro_2gen_defconfig` — Lenovo's normal device configuration.
- `p11_pro_2gen_debug_defconfig` — Lenovo's original debug configuration.
- `p11_pro_2gen_kgdb_defconfig` — the watchdog-safe KGDB and diagnostic
  configuration used for Alex development.

The tracked `localversion-alex` file gives all configurations the kernel release
suffix `-alex`.

For configuration-only validation:

```sh
make O=out ARCH=arm64 p11_pro_2gen_kgdb_defconfig
```

A complete kernel build requires a compatible Android ARM64/Clang toolchain and
the normal TB132FU device-tree inputs.

## History layout

1. Motorola `MMI-STA32.79-13-2` supplies the public MediaTek kernel history.
2. The pristine Lenovo archive is committed as one source-import commit.
3. The source compatibility, KGDB and device fixes follow as reviewable commits.
4. Each connectivity component is merged as a full-history Git subtree.

## Source provenance

## Kernel history base

- Repository: `https://github.com/MotorolaMobilityLLC/kernel-mtk.git`
- Release tag: `MMI-STA32.79-13-2`
- Commit: `b195552ce415e32bb615b78ed7fd1e9e64915aa4`

## Lenovo source import

- Archive: `tb132fu_opensource_tb132fu_s000018_220516_row.tar.gz`
- Archive size: `1786282010` bytes
- SHA-256: `25a12cc567ec2b097c532b7a92aa0c7ebc8cd121ebf94ccba35c2fb54f8b6b11`
- Imported member: `kernel-4.14/`
- Import commit: `2ee0254cd5fbbaf7101a9a3c12a37f478e8e8cdc`
- Imported tree: `6c28550164bf224cde42468a33687fc99d0e67dc`

The archive's generated `kernel-4.14/out/` directory is not source and is not
imported. The resulting commit contains all 80075 non-`out` paths, including
11 source/tool files hidden by the vendor `.gitignore` rules.

The source-side compatibility fixes, KGDB configuration and later diagnostic
work were reconstructed from `stock` through commit
`821fd2777b277ddce3ffa60bf64dc829b86bfb84`. Stale artifact hashes and the old
resolved build config from `build-provenance/` were intentionally not copied.

## Connectivity subtrees

Every component uses Motorola release tag `MMI-STA32.79-13-2` and is merged
with its complete source history, without `--squash`.

| Prefix | Repository | Release commit |
| --- | --- | --- |
| `vendor/mediatek/kernel_modules/connectivity/conninfra` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-conninfra` | `3f5a4148ad867dc72b5923c229e6357436a634f7` |
| `vendor/mediatek/kernel_modules/connectivity/wlan/adaptor` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-wlan-adaptor` | `2c0ce405985a8d31a87be8f1196b539a4c9ab81f` |
| `vendor/mediatek/kernel_modules/connectivity/wlan/core/gen4m` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-wlan-core-gen4m` | `bc35a9fe182d7ceed3fc8316abd32a05c23d4cd8` |
| `vendor/mediatek/kernel_modules/connectivity/bt/mt66xx` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-bt-mt66xx` | `0add952f116b7aa834a903d77802e7b5f10333ab` |
| `vendor/mediatek/kernel_modules/connectivity/fmradio` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-fmradio` | `f156d6961eee5aa7d5456d0c21f5c51049312d0e` |
| `vendor/mediatek/kernel_modules/connectivity/gps` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-gps` | `57544242053e4b4ed06d5337ce34e947dcc4bfee` |
| `vendor/mediatek/kernel_modules/connectivity/common` | `MotorolaMobilityLLC/vendor-mediatek-kernel_modules-connectivity-common` | `e74b68caf1a2be3463c2a5068df06c5779fed074` |

`drivers/misc/mediatek/connectivity/Makefile` prefers these in-tree sources and
falls back to MediaTek's original Android layout where `vendor/` is beside the
kernel source directory.
