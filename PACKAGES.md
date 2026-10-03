# Packages in epp-v2-image

What the default `epp-v2-image` installs on the board: **907 packages**, 253 of them software and libraries and 654 kernel modules.

Generated from the image manifest of the build on 2026-10-03 (BSP 6.18.20-2.0.0, `imx95-15x15-lpddr4x-frdm`). After each build the current list is in
`images/epp-v2-image-imx95-15x15-lpddr4x-frdm.rootfs.manifest`.

## Where the packages come from

| Source | Adds |
|---|---|
| `core-image-minimal` | Boot essentials: systemd, busybox, base-files, kernel, udev |
| `epp-v2-image.bb` | OpenSSH (`ssh-server-openssh`), apt/dpkg (`package-management`), all kernel modules (`kernel-modules`), `EPP_EXTRA_PACKAGES` from `board.conf` |
| NXP distro `fsl-imx-wayland` | Xen, Jailhouse, QEMU, Wayland, Mesa/Vulkan, PipeWire. `fsl-imx-extended.inc` enables the `xen` and `jailhouse` distro features for every image |
| Dependencies | Everything the above needs (libc, openssl, glib, python3, perl, …) |

The NXP defaults make the image much bigger than "minimal", and building
qemu and LLVM is a large part of the build time. They can be removed in
`meta-epp-v2` if the board does not need virtualization or 3D graphics.

## Packages by purpose

### Boot, kernel and init (33)

| Package | Version |
|---|---|
| `base-files` | 3.0.14-r0 |
| `base-passwd` | 3.6.8-r0 |
| `ca-certificates` | 20260223-r0 |
| `cryptodev-module` | 1.14-r0 |
| `dtc` | 1.7.2-r0 |
| `kernel` | 6.18.20+git0+b096ce610e-r0 |
| `kernel-image` | 6.18.20+git0+b096ce610e-r0 |
| `kernel-image-image` | 6.18.20+git0+b096ce610e-r0 |
| `kernel-modules` | 6.18.20+git0+b096ce610e-r0 |
| `kmod` | 34.2-r0 |
| `ldconfig` | 2.43+git0+ce1013a197-r1 |
| `libkmod2` | 34.2-r0 |
| `libnss-myhostname2` | 1:259.5-r0 |
| `libnss-resolve2` | 1:259.5-r0 |
| `libnss-systemd2` | 1:259.5-r0 |
| `libsystemd-shared` | 1:259.5-r0 |
| `libsystemd0` | 1:259.5-r0 |
| `libudev1` | 1:259.5-r0 |
| `netbase` | 1:6.5-r0 |
| `os-release` | 1.0-r0 |
| `packagegroup-core-boot` | 1.0-r0 |
| `systemd` | 1:259.5-r0 |
| `systemd-conf` | 1:1.0-r0 |
| `systemd-extra-utils` | 1:259.5-r0 |
| `systemd-mime` | 1:259.5-r0 |
| `systemd-networkd` | 1:259.5-r0 |
| `systemd-serialgetty` | 1.0-r0 |
| `systemd-udev-rules` | 1:259.5-r0 |
| `systemd-vconsole-setup` | 1:259.5-r0 |
| `udev` | 1:259.5-r0 |
| `udev-hwdb` | 1:259.5-r0 |
| `update-alternatives-opkg` | 0.7.0-r0 |
| `volatile-binds` | 1.0-r0 |

### Shell and base utilities (52)

| Package | Version |
|---|---|
| `bash` | 5.3-r0 |
| `bridge-utils` | 1.7.1-r0 |
| `busybox` | 1.37.0-r0 |
| `busybox-syslog` | 1.37.0-r0 |
| `busybox-udhcpc` | 1.37.0-r0 |
| `dbus-1` | 1.16.2-r0 |
| `dbus-common` | 1.16.2-r0 |
| `dbus-tools` | 1.16.2-r0 |
| `e2fsprogs-e2fsck` | 1.47.4-r0 |
| `ifupdown` | 0.8.45-r0 |
| `kbd` | 2.9.0-r0 |
| `kbd-consolefonts` | 2.9.0-r0 |
| `kbd-keymaps` | 2.9.0-r0 |
| `kbd-keymaps-pine` | 2.9.0-r0 |
| `libpam` | 1.7.2-r0 |
| `libpam-runtime` | 1.7.2-r0 |
| `lz4` | 1:1.10.0-r0 |
| `ncurses-terminfo-base` | 6.6-r0 |
| `pam-plugin-deny` | 1.7.2-r0 |
| `pam-plugin-env` | 1.7.2-r0 |
| `pam-plugin-faildelay` | 1.7.2-r0 |
| `pam-plugin-group` | 1.7.2-r0 |
| `pam-plugin-keyinit` | 1.7.2-r0 |
| `pam-plugin-limits` | 1.7.2-r0 |
| `pam-plugin-loginuid` | 1.7.2-r0 |
| `pam-plugin-mail` | 1.7.2-r0 |
| `pam-plugin-motd` | 1.7.2-r0 |
| `pam-plugin-namespace` | 1.7.2-r0 |
| `pam-plugin-nologin` | 1.7.2-r0 |
| `pam-plugin-permit` | 1.7.2-r0 |
| `pam-plugin-rootok` | 1.7.2-r0 |
| `pam-plugin-securetty` | 1.7.2-r0 |
| `pam-plugin-shells` | 1.7.2-r0 |
| `pam-plugin-umask` | 1.7.2-r0 |
| `pam-plugin-unix` | 1.7.2-r0 |
| `pam-plugin-warn` | 1.7.2-r0 |
| `shadow` | 4.19.4-r0 |
| `shadow-base` | 4.19.4-r0 |
| `shadow-securetty` | 4.6-r0 |
| `util-linux-agetty` | 2.41.3-r0 |
| `util-linux-flock` | 2.41.3-r0 |
| `util-linux-fsck` | 2.41.3-r0 |
| `util-linux-mkswap` | 2.41.3-r0 |
| `util-linux-mount` | 2.41.3-r0 |
| `util-linux-prlimit` | 2.41.3-r0 |
| `util-linux-sulogin` | 2.41.3-r0 |
| `util-linux-swapoff` | 2.41.3-r0 |
| `util-linux-swapon` | 2.41.3-r0 |
| `util-linux-swaponoff` | 2.41.3-r0 |
| `util-linux-umount` | 2.41.3-r0 |
| `xxhash` | 0.8.3-r0 |
| `xz` | 5.8.2-r0 |

### SSH (from board.conf / epp-v2-image) (7)

| Package | Version |
|---|---|
| `openssh` | 10.3p1-r0 |
| `openssh-keygen` | 10.3p1-r0 |
| `openssh-scp` | 10.3p1-r0 |
| `openssh-sftp-server` | 10.3p1-r0 |
| `openssh-ssh` | 10.3p1-r0 |
| `openssh-sshd` | 10.3p1-r0 |
| `packagegroup-core-ssh-openssh` | 1.0-r0 |

### Package management (apt / dpkg) (4)

| Package | Version |
|---|---|
| `apt` | 3.0.3-r0 |
| `db` | 1:5.3.28-r0 |
| `dpkg` | 1.23.7-r0 |
| `dpkg-start-stop` | 1.23.7-r0 |

### Security and crypto (6)

| Package | Version |
|---|---|
| `libcrypto3` | 3.5.6-r0 |
| `libssl3` | 3.5.6-r0 |
| `openssl` | 3.5.6-r0 |
| `openssl-bin` | 3.5.6-r0 |
| `openssl-conf` | 3.5.6-r0 |
| `openssl-ossl-module-legacy` | 3.5.6-r0 |

### Virtualization: Xen, Jailhouse, QEMU (NXP distro default) (70)

| Package | Version |
|---|---|
| `jailhouse-imx` | 2023.03+git0+8d6d397f7f-r0 |
| `libaio1` | 0.3.113-r0 |
| `libseccomp` | 2.6.0-r0 |
| `libslirp0` | 4.9.1-r0 |
| `libxencall1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenctrl4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxendevicemodel1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenevtchn1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenforeignmemory1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenfsimage4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxengnttab1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenguest4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenhypfs1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenlight4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenmanage1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenstat4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenstore4` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxentoolcore1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxentoollog1` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxenvchan4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `libxlutil4.21` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `pyjailhouse` | 2023.03+git0+8d6d397f7f-r0 |
| `qemu` | 10.2.5.imx-r0 |
| `qemu-common` | 10.2.5.imx-r0 |
| `qemu-system-aarch64` | 10.2.5.imx-r0 |
| `qemu-system-arm` | 10.2.5.imx-r0 |
| `qemu-system-i386` | 10.2.5.imx-r0 |
| `qemu-system-loongarch64` | 10.2.5.imx-r0 |
| `qemu-system-mips` | 10.2.5.imx-r0 |
| `qemu-system-mips64` | 10.2.5.imx-r0 |
| `qemu-system-mips64el` | 10.2.5.imx-r0 |
| `qemu-system-mipsel` | 10.2.5.imx-r0 |
| `qemu-system-ppc` | 10.2.5.imx-r0 |
| `qemu-system-ppc64` | 10.2.5.imx-r0 |
| `qemu-system-riscv32` | 10.2.5.imx-r0 |
| `qemu-system-riscv64` | 10.2.5.imx-r0 |
| `qemu-system-sh4` | 10.2.5.imx-r0 |
| `qemu-system-x86-64` | 10.2.5.imx-r0 |
| `qemu-user-aarch64` | 10.2.5.imx-r0 |
| `qemu-user-arm` | 10.2.5.imx-r0 |
| `qemu-user-i386` | 10.2.5.imx-r0 |
| `qemu-user-loongarch64` | 10.2.5.imx-r0 |
| `qemu-user-mips` | 10.2.5.imx-r0 |
| `qemu-user-mips64` | 10.2.5.imx-r0 |
| `qemu-user-mips64el` | 10.2.5.imx-r0 |
| `qemu-user-mipsel` | 10.2.5.imx-r0 |
| `qemu-user-ppc` | 10.2.5.imx-r0 |
| `qemu-user-ppc64` | 10.2.5.imx-r0 |
| `qemu-user-ppc64le` | 10.2.5.imx-r0 |
| `qemu-user-riscv32` | 10.2.5.imx-r0 |
| `qemu-user-riscv64` | 10.2.5.imx-r0 |
| `qemu-user-sh4` | 10.2.5.imx-r0 |
| `qemu-user-x86-64` | 10.2.5.imx-r0 |
| `virglrenderer` | 1.2.0-r0 |
| `xen` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-console` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-fsimage` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-scripts-block` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-scripts-common` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-scripts-network` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-vchan` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-volatiles` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xen-watchdog` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xencommons` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xendomains` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xenhypfs` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xenstore` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xenstored` | 4.21.imx+stable0+5b50ecdab2-r0 |
| `xen-tools-xl` | 4.21.imx+stable0+5b50ecdab2-r0 |

### Graphics: Wayland, Mesa, Vulkan (21)

| Package | Version |
|---|---|
| `libcairo2` | 1.18.4-r0 |
| `libdecor-0-0` | 0.2.5-r0 |
| `libdrm2` | 2.4.127.imx-r0 |
| `libepoxy0` | 1.5.10-r0 |
| `libfontconfig1` | 2.17.1-r0 |
| `libfreetype6` | 2.14.3-r0 |
| `libfribidi0` | 1.0.16-r0 |
| `libgallium` | 2:26.0.6.imx-r0 |
| `libgbm1` | 2:26.0.6.imx-r0 |
| `libharfbuzz0` | 12.3.2-r0 |
| `libpixman-1-0` | 1:0.46.4-r0 |
| `libpng16-16` | 1.6.56-r0 |
| `libsdl2-2.0-0` | 2.32.10-r0 |
| `libvulkan1` | 1.4.341.0-r0 |
| `llvm` | 22.1.3-r0 |
| `mali-imx-libvulkan` | r54p1.6-r0 |
| `mesa-vulkan-drivers` | 2:26.0.6.imx-r0 |
| `pango` | 1.57.0-r0 |
| `spirv-tools` | 1:1.4.341.0-r0 |
| `vulkan-wsi-layer` | 0.0+git0+5097740a45-r0 |
| `wayland` | 1.24.0-r0 |

### Audio (7)

| Package | Version |
|---|---|
| `alsa-conf` | 1.2.15.3-r0 |
| `alsa-ucm-conf` | 1.2.15.3-r0 |
| `libasound2` | 1.2.15.3-r0 |
| `libpipewire-0.3-0` | 1.6.3-r0 |
| `pipewire-modules-client-node` | 1.6.3-r0 |
| `pipewire-modules-protocol-native` | 1.6.3-r0 |
| `pipewire-spa-plugins-support` | 1.6.3-r0 |

### Python and Perl (18)

| Package | Version |
|---|---|
| `libpython3.14-1.0` | 3.14.4-r0 |
| `perl` | 5.42.0-r0 |
| `perl-module-config-heavy` | 5.42.0-r0 |
| `python3-compression` | 3.14.4-r0 |
| `python3-core` | 3.14.4-r0 |
| `python3-crypt` | 3.14.4-r0 |
| `python3-ctypes` | 3.14.4-r0 |
| `python3-curses` | 3.14.4-r0 |
| `python3-datetime` | 3.14.4-r0 |
| `python3-email` | 3.14.4-r0 |
| `python3-fcntl` | 3.14.4-r0 |
| `python3-io` | 3.14.4-r0 |
| `python3-math` | 3.14.4-r0 |
| `python3-mime` | 3.14.4-r0 |
| `python3-mmap` | 3.14.4-r0 |
| `python3-netclient` | 3.14.4-r0 |
| `python3-shell` | 3.14.4-r0 |
| `python3-stringold` | 3.14.4-r0 |

### Other libraries (35)

| Package | Version |
|---|---|
| `libacl1` | 2.3.2-r0 |
| `libattr1` | 2.5.2-r0 |
| `libblkid1` | 2.41.3-r0 |
| `libbz2-1` | 1.0.8-r0 |
| `libc6` | 2.43+git0+ce1013a197-r1 |
| `libcom-err2` | 1.47.4-r0 |
| `libcrypt2` | 4.5.2-r0 |
| `libdbus-1-3` | 1.16.2-r0 |
| `libe2p2` | 1.47.4-r0 |
| `libedit0` | 20251016-3.1-r0 |
| `libexpat1` | 2.7.5-r0 |
| `libext2fs2` | 1.47.4-r0 |
| `libffi8` | 3.5.2-r0 |
| `libgcc1` | 15.2.0-r0 |
| `libglib-2.0-0` | 1:2.88.0-r0 |
| `libjson-c5` | 0.18-r0 |
| `liblzma5` | 5.8.2-r0 |
| `libmd0` | 1.1.0-r0 |
| `libmount1` | 2.41.3-r0 |
| `libncurses5` | 6.6-r0 |
| `libncursesw5` | 6.6-r0 |
| `libnl-3-200` | 1:3.12.0-r0 |
| `libnl-route-3-200` | 1:3.12.0-r0 |
| `libpanelw5` | 6.6-r0 |
| `libpcre2` | 10.47-r0 |
| `libsmartcols1` | 2.41.3-r0 |
| `libstdc++6` | 15.2.0-r0 |
| `libtinfo5` | 6.6-r0 |
| `libusb-1.0-0` | 1.0.29-r0 |
| `libuuid1` | 2.41.3-r0 |
| `libxml2` | 2.15.2-r0 |
| `libz1` | 1.3.2-r0 |
| `libzstd1` | 1.5.7-r0 |
| `shared-mime-info` | 2.4-r0 |
| `shared-mime-info-data` | 2.4-r0 |

## Kernel modules (654)

All drivers built as modules from `imx_v8_defconfig` plus `epp-v2.cfg` are installed (`kernel-modules`) and loaded on demand. On the board: `ls /lib/modules/$(uname -r)/kernel`, loaded ones: `lsmod`.

<details>
<summary>Full list</summary>

```
adc-keys                      aes-neon-blk                  aes-neon-bs                   af-alg
ak7375                        algif-aead                    algif-hash                    algif-rng
algif-skcipher                altera-freeze-bridge          amlogic-gxl-crypto            anubis
ap1302                        arc4                          aria-generic                  arm-cci
arm-ccn                       arm-cmn                       arm-dmc620-pmu                arm-dsu-pmu
arm-smmuv3-pmu                arm-spe-pmu                   asix                          at24
at25                          atl1c                         atmel-mxt-ts                  authenc
authencesn                    ax88179-178a                  ax88796b                      bcm-phy-lib
bcm-sba-raid                  bcm54140                      bcm7xxx                       bcmsysport
blake2b-generic               blocklayoutdriver             blowfish-common               blowfish-generic
bnx2x                         bq25890-charger               bq25980-charger               br-netfilter
broadcom                      btintel                       btnxpuart                     btrfs
btrtl                         btusb                         caam                          caam-jr
caamalg-desc                  caamhash-desc                 caamkeyblob-desc              camellia-generic
can                           can-bcm                       can-dev                       can-gw
can-raw                       cast-common                   cast5-generic                 cast6-generic
cbc                           ccree                         cdc-acm                       cdc-ether
cdc-ncm                       cdc-subset                    ch7006                        chacha
chacha20poly1305              coda                          coresight-catu                coresight-cpu-debug
coresight-cti                 coresight-etb10               coresight-stm                 coresight-tpiu
cp210x                        cppc-cpufreq                  crc32c-cryptoapi              crc8
cros-ec-baro                  cros-ec-chardev               cros-ec-light-prox            cros-ec-sensors
cros-ec-sensors-core          crypto-engine                 crypto-null                   cryptodev
cts                           cuse                          des-generic                   dh-generic
dm-crypt                      dm-log                        dm-mirror                     dm-mod
dm-region-hash                dm-zero                       dm9601                        drm-dp-aux-bus
drm-exec                      drm-gpuvm                     drm-ttm-helper                drm-vram-helper
ds90ub953                     ds90ub960                     dw-drm-dsi                    dw-hdmi-ahb-audio
dw-hdmi-cec                   dw-hdmi-i2s-audio             dwmac-generic                 edt-ft5x06
emc2305                       error                         essiv                         extcon-ptn5150
fcrypt                        flexcan                       focaltech-ts                  fpga-bridge
fpga-region                   fsl-jr-uio                    fsl-qdma                      fusb302
fuse                          fxls8962af-core               fxls8962af-i2c                g-audio
g-ether                       g-mass-storage                g-serial                      g-zero
genet                         gnss                          gnss-mtk                      gnss-serial
goodix-ts                     gpio-bd9571mwv                gpio-ir-recv                  gpio-pca9570
gpio-wcd934x                  gpu-sched                     hd3ss3220                     hibmc-drm
hid-lg-g15                    hid-logitech                  hid-multitouch                hisi-hpre
hisi-qm                       hisi-sec2                     hisi-trng-v2                  hisi-zip
i2c-atr                       i2c-gpio                      i2c-hid                       i2c-hid-acpi
i2c-hid-of                    imx-audio-rpmsg               imx-common                    imx-dsp-rproc
imx-mipi-csis                 imx-pcm-dma                   imx-pcm-rpmsg                 imx-rpmsg-pingpong
imx-rpmsg-tty                 imx219                        imx519                        imx7-media-csi
imx8-media-dev                imx8mq-mipi-csi2              ina2xx                        ina3221
inv-mpu6050                   inv-mpu6050-i2c               inv-sensors-timestamp         ip-tables
ip-tunnel                     ip-vs                         ip6-tables                    ip6t-reject
ip6table-filter               ip6table-mangle               ip6table-nat                  ipmi-devintf
ipmi-msghandler               ipmi-si                       ipt-reject                    iptable-filter
iptable-mangle                iptable-nat                   ir-imon-decoder               ir-jvc-decoder
ir-kbd-i2c                    ir-mce-kbd-decoder            ir-nec-decoder                ir-rc5-decoder
ir-rc6-decoder                ir-rcmm-decoder               ir-sanyo-decoder              ir-sharp-decoder
ir-sony-decoder               ir-xmp-decoder                irq-imx-mu-msi                jailhouse
khazad                        kirin-drm                     lan78xx                       layerscape-edac-mod
led-class-multicolor          leds-cros-ec                  leds-lm3692x                  leds-pca9532
leds-pca995x                  libchacha                     libdes                        libmd5
libpoly1305                   libsm3                        lima                          lm90
lontium-lt8912b               lontium-lt9611                lp855x-bl                     lrw
macvlan                       macvtap                       mali-dp                       mali-kbase
marvell                       marvell10g                    max17042-battery              max9271
max9286                       max9611                       max96717-lib                  max96724
mcs7830                       md-mod                        md4                           md5
mdio                          mdio-bcm-unimac               microchip                     mlx4-core
mlx4-en                       mlx5-core                     mpt3sas                       ms5611-core
ms5611-i2c                    mscc-felix                    mscc-felix-dsa-lib            mx95mbcam
mxc-jpeg-encdec               nbd                           nci                           net1080
nf-conntrack                  nf-conntrack-netlink          nf-defrag-ipv4                nf-defrag-ipv6
nf-dup-netdev                 nf-flow-table                 nf-log-syslog                 nf-nat
nf-reject-ipv4                nf-reject-ipv6                nf-socket-ipv4                nf-socket-ipv6
nf-tproxy-ipv4                nf-tproxy-ipv6                nfc                           nfnetlink-osf
nft-chain-nat                 nft-compat                    nft-ct                        nft-dup-netdev
nft-fwd-netdev                nft-masq                      nft-nat                       nouveau
nvmem-rmem                    of-fpga-region                option                        ov5640
ov5645                        overlay                       ox03c10                       ox03c10-drv
ox05b1s                       panel-boe-tv101wum-nl6        panel-mantix-mlaf057we51      panel-sitronix-st7703
panel-truly-nt35597           panfrost                      panthor                       parade-ps8640
pcbc                          pci-meson                     pegasus                       pktgen
pl111-drm                     plusb                         polyval-ce                    polyval-generic
pwm-beeper                    pwm-cros-ec                   pwm-fan                       pwm-fsl-ftm
pwm-vibra                     qcom-emac                     qcom-spmi-adc5                qcom-spmi-vadc
qcom-vadc-common              qrtr                          qrtr-smd                      qrtr-tun
r8153-ecm                     raid-class                    raid6-pq                      rc-adstech-dvb-t-pci
rc-alink-dtu-m                rc-anysee                     rc-apac-viewcomp              rc-astrometa-t2hybrid
rc-asus-pc39                  rc-asus-ps3-100               rc-ati-tv-wonder-hd-600       rc-ati-x10
rc-avermedia                  rc-avermedia-a16d             rc-avermedia-cardbus          rc-avermedia-dvbt
rc-avermedia-m135a            rc-avermedia-m733a-rm-k6      rc-avermedia-rm-ks            rc-avertv-303
rc-azurewave-ad-tu700         rc-beelink-gs1                rc-beelink-mxiii              rc-behold
rc-behold-columbus            rc-budget-ci-old              rc-cinergy-1400               rc-cinergy
rc-core                       rc-ct-90405                   rc-d680-dmb                   rc-delock-61959
rc-dib0700-nec                rc-dib0700-rc5                rc-digitalnow-tinytwin        rc-digittrade
rc-dm1105-nec                 rc-dntv-live-dvb-t            rc-dntv-live-dvbt-pro         rc-dreambox
rc-dtt200u                    rc-dvbsky                     rc-dvico-mce                  rc-dvico-portable
rc-em-terratec                rc-encore-enltv               rc-encore-enltv-fm53          rc-encore-enltv2
rc-evga-indtube               rc-eztv                       rc-flydvb                     rc-flyvideo
rc-fusionhdtv-mce             rc-gadmei-rm008z              rc-geekbox                    rc-genius-tvgo-a11mce
rc-gotview7135                rc-hauppauge                  rc-hisi-poplar                rc-hisi-tv-demo
rc-imon-mce                   rc-imon-pad                   rc-imon-rsc                   rc-iodata-bctv7e
rc-it913x-v1                  rc-it913x-v2                  rc-kaiomy                     rc-khadas
rc-khamsin                    rc-kworld-315u                rc-kworld-pc150u              rc-kworld-plus-tv-analog
rc-leadtek-y04g0051           rc-lme2510                    rc-manli                      rc-mecool-kii-pro
rc-mecool-kiii-pro            rc-medion-x10                 rc-medion-x10-digitainer      rc-medion-x10-or2x
rc-minix-neo                  rc-msi-digivox-ii             rc-msi-digivox-iii            rc-msi-tvanywhere
rc-msi-tvanywhere-plus        rc-mygica-utv3                rc-nebula                     rc-nec-terratec-cinergy-xs
rc-norwood                    rc-npgtech                    rc-odroid                     rc-pctv-sedna
rc-pine64                     rc-pinnacle-color             rc-pinnacle-grey              rc-pinnacle-pctv-hd
rc-pixelview-002t             rc-pixelview                  rc-pixelview-mk12             rc-pixelview-new
rc-powercolor-real-angel      rc-proteus-2309               rc-purpletv                   rc-pv951
rc-rc6-mce                    rc-real-audio-220-32-keys     rc-reddo                      rc-siemens-gigaset-rc20
rc-snapstream-firefly         rc-streamzap                  rc-su3000                     rc-tanix-tx3mini
rc-tanix-tx5max               rc-tbs-nec                    rc-technisat-ts35             rc-technisat-usb2
rc-terratec-cinergy-c-pci     rc-terratec-cinergy-s2-hd     rc-terratec-cinergy-xs        rc-terratec-slim-2
rc-terratec-slim              rc-tevii-nec                  rc-tivo                       rc-total-media-in-hand-02
rc-total-media-in-hand        rc-trekstor                   rc-tt-1500                    rc-twinhan-dtv-cab-ci
rc-twinhan1027                rc-vega-s9x                   rc-videomate-m1f              rc-videomate-s350
rc-videomate-tv-pvr           rc-videostrong-kii-pro        rc-wetek-hub                  rc-wetek-play2
rc-winfast                    rc-winfast-usbii-deluxe       rc-x96max                     rc-xbox-360
rc-xbox-dvd                   rc-zx-irdec                   rdacm20                       regmap-sdw
regmap-slimbus                regmap-spmi                   rmd160                        rmnet
rpmsg-char                    rpmsg-client-sample           rpmsg-ctrl                    rpmsg-iio-pedometer
rpmsg-tty                     rtc-ds1307                    rtc-hym8563                   rtc-m41t80
rtc-pcf2127                   rtc-pcf85363                  rtc-rv3028                    rtc-rv8803
rtc-rx8581                    rtl8150                       s3fwrn5                       s3fwrn5-i2c
sbs-battery                   secvio                        seed                          serpent-generic
sha1                          sha3-ce                       sii902x                       sil164
simple-bridge                 sit                           sja1105                       slimbus
sm-test                       sm3-ce                        sm3-generic                   sm4
sm4-generic                   smsc                          smsc75xx                      smsc95xx
snd-aloop                     snd-hwdep                     snd-soc-ak4458                snd-soc-ak4613
snd-soc-ak5558                snd-soc-audio-graph-card2     snd-soc-cros-ec-codec         snd-soc-dmic
snd-soc-es7134                snd-soc-es7241                snd-soc-fsl-asoc-card         snd-soc-fsl-asrc
snd-soc-fsl-aud2htx           snd-soc-fsl-audmix            snd-soc-fsl-easrc             snd-soc-fsl-esai
snd-soc-fsl-micfil            snd-soc-fsl-mqs               snd-soc-fsl-rpmsg             snd-soc-fsl-sai
snd-soc-fsl-spdif             snd-soc-fsl-ssi               snd-soc-fsl-utils             snd-soc-fsl-xcvr
snd-soc-gtm601                snd-soc-imx-audmix            snd-soc-imx-audmux            snd-soc-imx-card
snd-soc-imx-hdmi              snd-soc-imx-pcm512x           snd-soc-imx-rpmsg             snd-soc-imx-sgtl5000
snd-soc-lpass-macro-common    snd-soc-lpass-va-macro        snd-soc-lpass-wsa-macro       snd-soc-max98357a
snd-soc-max98927              snd-soc-msm8916-analog        snd-soc-msm8916-digital       snd-soc-pcm186x
snd-soc-pcm186x-i2c           snd-soc-pcm3168a              snd-soc-pcm3168a-i2c          snd-soc-pcm512x
snd-soc-pcm512x-i2c           snd-soc-rl6231                snd-soc-rt5659                snd-soc-sgtl5000
snd-soc-simple-amplifier      snd-soc-simple-mux            snd-soc-spdif-rx              snd-soc-spdif-tx
snd-soc-tas571x               snd-soc-tlv320aic31xx         snd-soc-tpa6130a2             snd-soc-wcd-classh
snd-soc-wcd-common            snd-soc-wcd-mbhc              snd-soc-wcd934x               snd-soc-wm-hubs
snd-soc-wm8904                snd-soc-wm8960                snd-soc-wm8962                snd-soc-wm8994
snd-soc-wsa881x               snd-sof                       snd-sof-imx8                  snd-sof-imx9
snd-sof-of                    snd-sof-utils                 snd-sof-xtensa-dsp            snd-usb-audio
snd-usbmidi-lib               soundwire-bus                 soundwire-qcom                spi-dw
spi-dw-mmio                   sr9800                        st-accel                      st-accel-i2c
st-accel-spi                  st-gyro                       st-gyro-i2c                   st-gyro-spi
st-magn                       st-magn-i2c                   st-magn-spi                   st-sensors
st-sensors-i2c                st-sensors-spi                stm-core                      streebog-generic
synaptics-dsx-i2c             tag-ocelot                    tag-ocelot-8021q              tag-sja1105
tap                           tcrypt                        tda998x                       tea
tee-crypto                    thc63lvd1024                  ti-sn65dsi86                  tls
tmp108                        tps65132-regulator            tps6598x                      trusted
ttm                           tunnel4                       twofish-common                twofish-generic
uacce                         usb-wwan                      usbtest                       uvcvideo
v4l2-cci                      v4l2-jpeg                     vcnl4000                      vcnl4035
vctrl-regulator               veth                          vhost-net                     wave5
wave5-ctrl                    wcd934x                       wl18xx                        wlcore
wlcore-sdio                   wm8994                        wp512                         x-tables
xcbc                          xen-blkback                   xen-netback                   xhci-pci-renesas
xor                           xor-neon                      xt-addrtype                   xt-checksum
xt-conntrack                  xt-ipvs                       xt-log                        xt-mark
xt-masquerade                 xt-nat                        xt-tcpudp                     xts
xxhash-generic                zaurus
```

</details>
