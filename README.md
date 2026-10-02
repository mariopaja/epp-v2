# epp-v2 Yocto custom config

Custom layer + config for the epp-v2 FRDM-IMX95 build.

- hostname: `epp-v2`
- root password: `root1234`
- SSH enabled with password root login

## Usage

After `repo sync` + `imx-setup-release.sh` (or `oe-init-build-env` on an
existing build dir), with the environment sourced:

```bash
~/epp-v2-yocto-config/apply-config.sh <path-to-build-dir>
bitbake core-image-minimal
```
