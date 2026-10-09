# epp-v2

Docker-based Yocto build for the **epp-v2** board, based on the
**NXP FRDM-IMX95 (i.MX 95 15x15, LPDDR4x)**. It is derived from
[nxp-imx/imx-docker](https://github.com/nxp-imx/imx-docker), and also contains the
board's custom layer `meta-epp-v2` and its configuration (hostname, root
password, device tree overlays).

The image is always built **inside the Docker container**. The host only needs
Docker.

| | |
|---|---|
| BSP release | i.MX `6.18.20-2.0.0` (Yocto wrynose) |
| `MACHINE` | `imx95-15x15-lpddr4x-frdm` |
| `DISTRO` | `fsl-imx-wayland` |
| Image | `epp-v2-image` (core-image-minimal + SSH + kernel modules), see [PACKAGES.md](PACKAGES.md) |

## Layout

```
epp-v2/
├── config.sh             # Host/Docker settings: BSP version, machine, paths, CPU/RAM limits
├── PACKAGES.md           # List of packages in the default image
├── board.conf            # Board settings: hostname, root password, overlays, extra packages
├── Dockerfile            # Ubuntu 24.04 build environment
├── docker-build.sh       # Builds the Docker image
├── docker-run.sh         # Runs the build (or a shell) in the container
├── flash-sd.sh           # Writes the image to an SD card
├── images -> yocto-builds/.../deploy/images/...   # created by the build (gitignored)
├── yocto-builds/         # BSP sources, build dir, caches (gitignored)
├── m7/                 # Cortex-M7 firmware for the boot container (EPP_M7_IMAGE)
├── xen/                # Zephyr image for the Xen dom0less DomU (EPP_XEN_ZEPHYR_IMAGE)
├── zephyr/             # Zephyr module: board frdm_imx95 (M7 / A55), Xen xenstore +
│                         # netfront drivers for the Zephyr DomU; see zephyr/README.md
├── scripts/
│   └── yocto-build.sh    # Runs inside the container: repo sync, setup, bitbake
└── meta-epp-v2/          # Custom Yocto layer
    ├── conf/layer.conf
    ├── recipes-core/images/epp-v2-image.bb       # the epp-v2 Linux
    ├── recipes-core/images/epp-v2-dom0-image.bb  # minimal Xen Dom0 (--xen=domu / dom0less)
    ├── recipes-extended/epp-v2-xen-dom0/         # Dom0: xenbr0, backends, DomU start
    ├── recipes-extended/xen/                     # xen-tools fixes for dom0less PV devices
    ├── files/wic/epp-v2-xen-domu.wks.in          # SD layout with the DomU partition
    ├── recipes-core/base-files/          # hostname
    ├── recipes-connectivity/openssh/     # root SSH login
    ├── recipes-kernel/linux/             # kernel config fragment (epp-v2.cfg)
    └── recipes-bsp/
        ├── epp-v2-m7-firmware/           # EPP_M7_IMAGE -> m7_image.bin in imx-boot
        ├── epp-v2-xen-zephyr/            # EPP_XEN_ZEPHYR_IMAGE -> zephyr-domu.bin
        ├── imx-mkimage/                  # imx-boot: depend on epp-v2-m7-firmware
        ├── epp-v2-overlays/              # device tree overlays (*.dtso)
        └── epp-v2-bootscript/            # U-Boot boot.scr that applies overlays
```

## Prerequisites

Docker, with your user in the `docker` group
(see the [imx-docker README](https://github.com/nxp-imx/imx-docker#prerequisites),
including the proxy setup if needed). Around 300 GB of free disk space.

## Build

```bash
./docker-build.sh     # once, builds the container image
./docker-run.sh       # repo sync + bitbake epp-v2-image, inside the container
```

When it finishes, `docker-run.sh` prints the start and end time and how long
the build took.

### Xen

`--xen=<mode>` builds with the Xen hypervisor. Without it the image is plain
Linux, with no Xen at all.

```bash
./docker-run.sh --xen              # same as --xen=dom0less
./docker-run.sh --xen=dom0less
./docker-run.sh --xen=domu
./docker-run.sh --xen=dom0
SKIP_SYNC=1 ./docker-run.sh --xen  # rebuild without repo sync
```

| Mode | Dom0 | epp-v2 Linux | SD card |
|---|---|---|---|
| (none) | – | runs directly, no Xen | boot, rootfs |
| `dom0` | the epp-v2 Linux | Dom0 (owns all hardware, runs `xl`) | boot, rootfs |
| `domu` | minimal (`epp-v2-dom0-image`) | DomU, started by `xendomains` from `/etc/xen/epp-v2.cfg` | boot, Dom0 rootfs, DomU rootfs |
| `dom0less` | minimal (`epp-v2-dom0-image`) | DomU, created by Xen at boot from the device tree (`/chosen/domU1`); Dom0 then attaches its disk and network (`epp-v2-dom0less.service`) | boot, Dom0 rootfs, DomU rootfs |

- CPU cores and RAM of the epp-v2 Linux and of the minimal Dom0 are set in
  [board.conf](board.conf) (`EPP_XEN_LINUX_*`, `EPP_XEN_DOM0_*`). Defaults:
  epp-v2 Linux 2 vCPUs / 4 GB, minimal Dom0 1 vCPU / 1 GB.
- `domu` / `dom0less`: the DomU disk is partition 3 (`/dev/xvda` in the DomU,
  `/dev/mmcblk1p3` in Dom0). Its network goes through the bridge `xenbr0` in
  Dom0, which contains `EPP_XEN_UPLINK` (default `eth0`); Dom0 and DomU both get
  their address by DHCP. Dom0 is called `epp-v2-dom0`.
- `dom0less` with `EPP_XEN_ZEPHYR_IMAGE` set: a second DomU runs Zephyr
  (`/chosen/domU2`, image `zephyr-domu.bin` on the boot partition,
  `EPP_XEN_ZEPHYR_VCPUS` / `EPP_XEN_ZEPHYR_MEM`, default 1 vCPU / 16 MB). It
  gets xenstore and a vif on `xenbr0` (`vif2.0`), so with the Zephyr Xen
  netfront driver it shares the Ethernet port with the Linux DomU and gets
  its own DHCP address; no disk. See [zephyr/README.md](zephyr/README.md).
- `EPP_XEN_ZEPHYR_CPU` (default core 5): `boot.scr` creates a cpupool with
  that core and the `null` scheduler for the Zephyr DomU (Xen built with
  `CONFIG_BOOT_TIME_CPUPOOLS` and `CONFIG_SCHED_NULL`); the other cores form
  Pool-0 (credit2) for Dom0 and the Linux DomU. Check in Dom0 with
  `xl cpupool-list` and `xl vcpu-list`.
- Domain names: `boot.scr` passes the dom0less DomUs to Dom0 in Xen's
  numbering order (`epp.domus=linux,zephyr` on the Dom0 command line);
  `epp-v2-dom0less.service` (`/usr/libexec/epp-v2-dom0less-setup`) sets up
  their xenstore, renames them to `EPP_HOSTNAME` (`epp-v2`) and `zephyr` and
  attaches their devices by name, so `xl list`, `xl network-list zephyr`
  etc. work with names. Its log: `journalctl -u epp-v2-dom0less`.
- PV devices of dom0less DomUs need the patched Xen tools in
  `meta-epp-v2/recipes-extended/xen/` (`init-dom0less` and `xl
  block-attach` / `network-attach` failed with NXP's version).
- Consoles: `domu`: `xl console epp-v2` in Dom0. `dom0less`: the DomUs use the
  emulated PL011 (`ttyAMA0` in Linux); press Ctrl-a three times on the serial
  console to move the input on: DOM0, DOM1 (epp-v2 Linux), DOM2 (Zephyr), Xen.
  In `screen`, `picocom` or `minicom` Ctrl-a is their own command key: send it
  with Ctrl-a a. Do not type in the Xen input: `R` reboots the board.
- Without Xen for one boot: `setenv epp_xen no; saveenv` at the U-Boot prompt
  (boots partition 2 directly; with `domu` / `dom0less` that is the minimal
  Dom0 image).
- Each build writes `images/epp-v2-sdcard.wic.zst`, the SD card image of that
  build, which `flash-sd.sh` flashes. All variants share `images/`, so the
  last build is the one flashed.

Rebuild without re-syncing the BSP sources:

```bash
SKIP_SYNC=1 ./docker-run.sh
```

Get a shell inside the container (for example to run `bitbake` by hand):

```bash
./docker-run.sh bash
# inside the container:
cd yocto-builds && source setup-environment build_imx95-15x15-lpddr4x-frdm
bitbake epp-v2-image
```

Everything the build produces stays inside this directory, in `yocto-builds/`
(BSP sources, build dir, `downloads/` and `sstate-cache/`). It is gitignored.
Override any setting from `config.sh` with environment variables, for example:

```bash
DOCKER_WORKDIR=/data/yocto ./docker-run.sh
```

Output images: `images/` in this directory. It is a link to
`yocto-builds/build_imx95-15x15-lpddr4x-frdm/tmp/deploy/images/imx95-15x15-lpddr4x-frdm/`,
created after each successful build.

## CPU and memory

The build uses all 24 cores, at most 24 GB of RAM and up to 8 GB of swap. These limits are set in
`config.sh`:

| Variable | Default | Meaning |
|---|---|---|
| `DOCKER_CPUSET` | empty | Cores the container may use, e.g. `0-8,10-23`. Empty = all cores |
| `DOCKER_MEMORY` | `24g` | Maximum RAM for the container. Empty = no limit |
| `DOCKER_MEMORY_SWAP` | `32g` | RAM + swap in total, i.e. up to 8 GB of swap on top of the 24 GB RAM |
| `BB_NUMBER_THREADS` | `8` | How many BitBake tasks run at the same time |
| `PARALLEL_MAKE_JOBS` | `12` | `make -j` value inside each task |

`DOCKER_CPUSET` and `DOCKER_MEMORY` are limits Docker puts on the container.
`BB_NUMBER_THREADS` and `PARALLEL_MAKE_JOBS` decide how much work BitBake
starts inside it, so they set how many cores are actually used. Up to
tasks × jobs compilers can run at once, and a C++ compile can take ~2 GB, so
24 × 24 runs out of 24 GB; 8 × 12 keeps the cores busy without that.

Override them for one build without editing the file:

```bash
BB_NUMBER_THREADS=12 DOCKER_MEMORY=16g ./docker-run.sh
```

If the build fails with `Killed` or exit code `137`, the container ran out of
memory (check with `journalctl -k | grep -i oom`): lower `BB_NUMBER_THREADS` (e.g. to 6) and rerun with
`SKIP_SYNC=1 ./docker-run.sh`. Finished tasks are cached, so it continues
where it stopped. Watch usage with `docker stats` while it builds.

## Flash to SD card

```bash
./flash-sd.sh            # flash the latest image from images/
./flash-sd.sh --list     # only show the SD cards / USB disks found
```

The script lists removable, USB and SD-reader disks with their size, model
and partitions (internal disks and the disk running the system are never
shown), asks which one to use, and asks you to type the device name to
confirm. It then unmounts the card and writes the image with `bmaptool`
(or `dd` if no `.bmap` file is found). It needs `sudo`.

Manual alternative (replace `/dev/sdX`, check with `lsblk` first):

```bash
sudo bmaptool copy images/epp-v2-sdcard.wic.zst /dev/sdX
```

## Board configuration (`board.conf`)

| Variable | Default | Meaning |
|---|---|---|
| `EPP_HOSTNAME` | `epp-v2` | Device hostname |
| `EPP_ROOT_PASSWORD` | `root1234` | Root password. Hashed with SHA-512 at build time |
| `EPP_SSH_ROOT_LOGIN` | `yes` | Allow root login over SSH with password |
| `EPP_OVERLAYS` | empty | `.dtbo` files U-Boot applies at boot |
| `EPP_FDTFILE` | `imx95-15x15-frdm.dtb` | Base device tree |
| `EPP_EXTRA_PACKAGES` | empty | Extra packages installed in the image |
| `EPP_XEN_ZEPHYR_IMAGE` | empty | Zephyr image (raw `zephyr.bin` for `xenvm/xenvm/gicv3`, path inside this repo, e.g. `xen/zephyr.bin`) started as second DomU with `--xen=dom0less`. `EPP_XEN_ZEPHYR_VCPUS` / `EPP_XEN_ZEPHYR_MEM` (MB) set its resources |
| `EPP_XEN_ZEPHYR_CPU` | `5` | Physical A55 core (1-5) reserved for the Zephyr DomU: own Xen cpupool with the `null` scheduler, so its vCPU always runs on that core and no other domain does. Empty: shared cores (credit2). Core 0 is Xen's boot CPU |
| `EPP_M7_IMAGE` | empty | Cortex-M7 firmware (raw `.bin`, path inside this repo, e.g. `m7/zephyr.bin`) packed into the boot container and started by the System Manager at power-on. Empty: NXP's M7 demo. `none`: no M7 image, M7 off. Update only the M7 on a flashed card: `./flash-sd.sh --m7 <file\|none>`. See [zephyr/README.md](zephyr/README.md) |

`scripts/yocto-build.sh` turns these into `conf/auto.conf` in the build
directory on every build. Edit `board.conf`, not `auto.conf`.

### Zephyr images (M7 and Xen DomU)

The Zephyr binaries are not in git (`m7/*.bin` and `xen/*.bin` are in
`.gitignore`), so `EPP_M7_IMAGE` and `EPP_XEN_ZEPHYR_IMAGE` are empty in the
repository. After a fresh clone, build both images (see
[zephyr/README.md](zephyr/README.md)), copy them into this directory, the only
one the build container sees, and set the paths in `board.conf`:

```bash
cp <zephyr build for frdm_imx95/mimx9596/m7>/zephyr/zephyr.bin   m7/zephyr.bin
cp <zephyr build for xenvm/xenvm/gicv3>/zephyr/zephyr.bin          xen/zephyr.bin
```

```bash
# board.conf
EPP_M7_IMAGE="m7/zephyr.bin"            # Zephyr on the Cortex-M7
EPP_XEN_ZEPHYR_IMAGE="xen/zephyr.bin"   # Zephyr DomU, with ./docker-run.sh --xen
```

The build stops with an error if a configured file is missing. Keep these
local edits of `board.conf` out of commits, or leave the settings empty.

**Change the default root password before deploying a device.**

## Adding an external module

1. **Device tree overlay**: create
   `meta-epp-v2/recipes-bsp/epp-v2-overlays/files/<name>.dtso` (see
   `epp-example.dtso`). Board nodes can be referenced by label (`&lpi2c3`,
   ...) because the FRDM DTB is built with symbols. Add it to `SRC_URI` in
   `epp-v2-overlays.bb`.
2. **Enable it**: add `<name>.dtbo` to `EPP_OVERLAYS` in `board.conf`.
3. **Kernel driver**: if the driver is not in `imx_v8_defconfig`, add it to
   `meta-epp-v2/recipes-kernel/linux/files/epp-v2.cfg` (for example
   `CONFIG_SENSORS_TMP102=m`).
4. Rebuild: `SKIP_SYNC=1 ./docker-run.sh`.

### How overlays are applied

The overlays are copied to `overlays/` on the FAT boot partition (mounted at
`/boot` on the target), together with `boot.scr`. U-Boot runs `boot.scr`,
which loads `Image` and the base DTB, applies each overlay from `epp_overlays`
and boots. If the script fails, U-Boot falls back to the default NXP boot.

You can change the overlays without rebuilding, from the U-Boot prompt:

```
setenv epp_overlays "epp-example.dtbo my-sensor.dtbo"
saveenv
boot
```

or by copying a new `.dtbo` to `/boot/overlays/` on the running board.
