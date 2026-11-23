# DOX: docs/

## Purpose

User and contributor documentation, protocol documentation, and the hardware
evidence that driver changes depend on.

## Ownership

- `CONTRIBUTING.md`: rules for submitting changes, and how to capture what
  games and the driver send.
- `FFBEFFECTS.md`: packet format of the T300RS and compatible wheels.
- `STRUCTURE.md`: the Thrustmaster driver stack in the kernel and here.
- `INTEGRATION.md`: installation on distributions that need extra steps.
- `DRIVER.md`: the Windows Thrustmaster driver under Wine.
- `TODO.md`: maintainer TODO list.
- `wheels/<model>/`: hardware evidence per wheel, created when the first
  evidence for a wheel is added.

## Local Contracts

- New wheels get their own directory `wheels/<model>/`, for example
  `wheels/t500rs/`, with:
  - `README.md`: USB IDs, `bcdDevice`, firmware versions seen, values returned
    by the attachment query, how the wheel is told apart from other wheels
    with the same ID, and which effects are supported and tested.
  - `rdesc.md`: report descriptor dumps, with source (Linux or Windows, kernel
    or driver version, firmware) and length.
  - `effects.md`: the packet format of each effect, format first, then
    dissected example packets, in the style of `FFBEFFECTS.md`.
- The existing `FFBEFFECTS.md` for the T300RS stays where it is.
- Preserve raw data exactly: descriptor dumps and packet bytes are copied, not
  retyped or reconstructed.
- Record how the data was obtained: tool (`usbmon`/Wireshark, `fedit`,
  `ffbsdl`, `ffbwrap`), which parameter was changed, wheel, firmware, kernel or
  Windows driver version.
- Separate measured facts from assumptions. Name unknown fields as unknown.
- Don't generalize from one wheel or firmware to all Thrustmaster wheels
  without evidence from more devices.
- Raw capture files don't go into the repository, only annotated excerpts.
- Remove personal data (serial numbers, user names, paths) from dumps.

## Work Guidance

- ASCII only, and one consistent example format throughout a document.

## Verification

- Re-check every stated ID, length, firmware version and packet byte against
  the source material.
- Check that the driver code and the documentation agree (constants, field
  names, packet order).

## Child DOX Index

No child guides are currently needed. Add one for `wheels/` when the evidence
there needs rules beyond this guide.
