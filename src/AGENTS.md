# DOX: src/

## Purpose

Kernel module sources of `hid-tmff-new`: the core driver and one backend
directory per wheel family.

## Ownership

- `hid-tmff2.c`: probe and removal, the HID device table, wheel selection,
  the Linux force feedback API, effect state, the work handler, module
  parameters and sysfs attributes for shared settings.
- `hid-tmff2.h`: device IDs, `struct tmff2_device_entry` with the backend
  callbacks, the `<wheel>_populate_api()` declarations and the shared
  `t300rs_*` declarations.
- `tmt248/`, `tmtx/`, `tmtsxw/`, `tmtspc/`: thin backends with the wheel's
  report descriptor, init and open/close specifics. They reuse the T300RS code
  in `tmt300rs/` and are owned by this guide.

## Local Contracts

- Shared settings (gain, range, autocenter, spring/damper/friction levels,
  alternate mode) are module parameters and sysfs attributes in
  `hid-tmff2.c`. Backends get them through the callbacks and don't keep their
  own copies.
- A backend fills the callbacks of `struct tmff2_device_entry` in its
  `<wheel>_populate_api()`, declared in `hid-tmff2.h` and called from
  `tmff2_probe()`.
- `tmff2_probe()` selects the backend by USB product ID. Don't add module
  parameters to select a wheel model. Telling apart wheels that share an ID, or
  restructuring backends, needs an issue with the maintainer first.
- The work handler exists to limit how often packets are sent to the wheel
  (`timer_msecs`). Giving it other responsibilities needs a discussion first.
- New source files start with `// SPDX-License-Identifier: GPL-2.0-or-later`
  and a copyright line, and are added to `Kbuild`.

## Work Guidance

- Follow the Linux kernel coding style and the patterns of the existing
  backends.
- Use the kernel log levels (`hid_err()`, `hid_warn()`, `hid_info()`,
  `hid_dbg()`), no driver-specific debug parameters.
- Headers only expose what other files need.

## Verification

- `make` builds `hid-tmff-new.ko` against the running kernel, or against
  `KDIR=<kernel build dir>`.
- `checkpatch.pl --no-tree` on the series, see `docs/CONTRIBUTING.md`.

## Child DOX Index

- `tmt300rs/` - T300RS backend and the effect encoding and send helpers that
  the T248, TX, TS-XW and TS-PC backends reuse.
