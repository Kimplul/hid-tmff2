# DOX: src/tmt300rs/

## Purpose

T300RS backend and the force feedback protocol code shared by the T300 family.

## Ownership

- `hid-tmt300rs.c`: encoding of all effects into T300 packets, effect upload,
  update, play and stop, open/close, gain, range and autocenter, and the send
  helpers `t300rs_send_buf()` and `t300rs_send_int()`.
- `hid-tmt300rs.h`: T300RS-local definitions. The `t300rs_*` functions and
  `struct t300rs_device_entry` used by other backends are declared in
  `../hid-tmff2.h`.

## Local Contracts

- The T248, TX, TS-XW and TS-PC backends call the `t300rs_*` functions and use
  `struct t300rs_device_entry`. A change here changes all of these wheels.
- The packet layout must match `docs/FFBEFFECTS.md`. Update the documentation
  in the same commit when the encoding changes.
- The `t300rs` names are used for code shared by the whole family. Renaming or
  splitting this code needs an issue with the maintainer first.

## Work Guidance

## Verification

- Test changes on a T300RS and, if available, on another wheel of the family,
  and state in the pull request which wheels were not tested.

## Child DOX Index

No child guides are currently needed.
