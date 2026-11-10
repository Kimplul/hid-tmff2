# Contributing

## Overview This file contains general contributing information.

This project is seeking for help from people who can contribute. If you have a
wheel that's not on the list, but suspect it might fit into the driver, please
feel free to open up an issue about it.

Other documents available are linked here:

+ [FFBEEFFECTS](./FFBEFFECTS.md):
  Force feedback effects example for T300RS and compatible wheels

+ [STRUCTURE](./STRUCTURE.md):
  Structure of Thrustmaster device stack

+ [DRIVER.md](./DRIVER.md):
  Info on installing Thrustmaster drivers under Wine

+ [TODO](./TODO.md):
  TODO list for maintainers

## Submitting changes

Reviews here are done by one person in their spare time. Small, focused pull
requests with clear evidence get reviewed and merged much faster than large
ones.

### Scope

+ One concern per pull request, one concern per commit. If a commit message
  needs the word 'and', the commit is probably too big.
+ A pull request with more than about 350 to 400 lines of diff is probably too
  big. This is a guideline, not a hard limit: sometimes it can't be avoided,
  e.g. a new wheel needs some boilerplate. Then say why it can't be split.
  Bigger features are split into a series: first the generic changes, then the
  wheel with its most important effects (usually constant force), then further
  effects.
+ Keep generic changes (core driver, Makefile, udev rules, README, helper
  scripts) separate from wheel-specific changes.
+ Don't change defaults as a side effect. If you think a default is wrong, open
  a separate pull request and explain why.
+ Don't include unrelated changes: whitespace, reformatting, removed final
  newlines, documentation rewrites.
+ Every commit must build and work on its own.

### Working with a queue branch

Larger work is easiest to get merged if you keep it on a local queue branch and
submit it in small pieces:

1. Keep all your commits on a private queue branch on top of `master`. It may
   contain work in progress, experiments and commits in any order.
2. Pick the next commit or short series that is ready and covers a single
   concern. Cherry-pick it onto a new branch from the current `master`, build
   and test every commit, and open a pull request for it.
3. Address review comments in the same pull request: create
   `git commit --fixup=<commit>` commits, fold them with
   `git rebase -i --autosquash master` and force-push the branch. Don't close
   the pull request and open a new one for each round, that loses the review
   history. Don't squash a series into one big commit, and don't add commits
   that fix earlier commits of the same series.
4. When the pull request is merged, rebase your queue onto `master`. The merged
   commits drop out. Adjust the remaining commits if needed, then continue with
   step 2.

If a pull request depends on another one that is not merged yet, say so and
keep it as a draft.

### Commits

Write commit messages so they could be sent to the Linux kernel as well:

+ A short lowercase subject that says what the commit does, prefixed with the
  affected driver or area, e.g. `hid-tmt300rs: fix ...`.
+ A body that explains why the change is needed, what the problem was and how
  it was found. The message must describe what the commit actually changes.
+ Reference related issues and pull requests with their full URL in a `Link:`
  or `Fixes:` trailer. Only reference what is actually related.
+ Add a `Signed-off-by` line to certify the
  [Developer's Certificate of Origin](https://developercertificate.org/). It is
  required for AI-assisted contributions, see below.
+ Run `checkpatch.pl` from the kernel tree on your commits, e.g.
  `git format-patch --stdout master | ./scripts/checkpatch.pl --no-tree -`.

### Evidence

The maintainer can't test every wheel. Each change that depends on hardware
behavior needs evidence that can be checked later, stored in `docs/`. The
layout and rules are described in [AGENTS.md](./AGENTS.md) in this directory:

+ For a new wheel: USB IDs, `bcdDevice`, firmware version, the report
  descriptor (from Linux, and from Windows if the Windows driver replaces it),
  and how the wheel is told apart from other wheels with the same ID.
+ For force feedback: the packet format of each effect, with dissected example
  packets, like [FFBEFFECTS](./FFBEFFECTS.md). Work out each field by changing
  one effect parameter at a time, e.g. with `fedit` on Windows or
  [ffbsdl](https://github.com/Kimplul/ffbsdl), and note which tool and which
  parameters were used.
+ Mark what was measured and what is assumed. Unknown fields stay named as
  unknown.
+ Annotated excerpts of captures go into the documentation. Raw capture files
  don't go into the repository.
+ In the pull request, link the evidence and list which wheels, kernels and
  games you tested with.

### Driver and protocol

+ Settings shared by all wheels (gain, range, autocenter, spring/damper/friction
  levels) are handled by `hid-tmff2.c`. Wheel drivers use them, they don't keep
  their own copies.
+ Implement the Linux force feedback API as specified: effect direction, replay
  length and delay, envelopes, and updates of already uploaded effects. If the
  wheel has hardware timers or delay fields, use them.
+ Don't add workarounds without understanding what they work around, and don't
  work around bugs in individual games in the driver.
+ Don't add module parameters to select a wheel model. Wheels with the same USB
  ID have to be told apart by the driver itself, ask in an issue first.
+ Larger restructuring, like shared backends or wheel detection, is discussed
  in an issue before any code is submitted.
+ Switching the wheel out of its init mode is done by
  [hid-tminit](https://github.com/Kimplul/hid-tminit), changes for that go
  there. Permissions for sysfs files belong to the udev rules of
  [Oversteer](https://github.com/berarma/oversteer).
+ No magic numbers: give packet fields names, preferably with structs, and
  refer to the documentation of the packet format.

### Style

+ Follow the
  [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html):
  tabs with a width of 8. `clang-format` with the kernel's configuration helps,
  see the
  [kernel docs](https://www.kernel.org/doc/html/latest/dev-tools/clang-format.html).
+ ASCII only in code and documentation. Check with
  `grep -P -n "[\x80-\xFF]" <file>`.
+ Lowercase hex, keep the final newline of files.
+ Keep comments short and to the point, and skip checks that can't fail.
+ Headers only expose what other files need.
+ New source files start with an SPDX license identifier
  (`GPL-2.0-or-later`, like the existing ones) and a copyright line.

### Testing

+ Test basic effects with fixed parameters first (`fftest`, `ffbsdl`), then
  games. `ffbwrap` shows what a game sends to the driver, see below.
+ Test that the other wheels you have still work.

### Reviews

+ Answer review questions yourself, in your own words. The goal of a review is
  that both sides understand the code that gets merged.
+ Only mark a review comment as resolved if you changed the code accordingly or
  replied why not.
+ If a pull request is not ready yet, open it as a draft and say what is
  missing.

### AI-assisted contributions

AI tools can help, but you are responsible for everything you submit.

+ Declare in the pull request if and how you used AI tools. This helps choosing
  how to review it.
+ Only submit code you understand, have tested and can explain, and that won't
  change in arbitrary ways after each review round.
+ Add your own `Signed-off-by` to confirm this. Don't add `Co-authored-by` or
  similar trailers for AI tools.
+ Don't pass review questions to an AI and post its answers. If you don't know
  the answer, say so.
+ Check that commit messages, pull request descriptions and comments are
  correct before posting them.
+ AI agents must not open pull requests or post comments themselves. They
  prepare the branch and the text, the human contributor submits it.

## How to capture what effects a game sends to the driver?

Use ffbwrap from ffbtools:
[github:berarma/ffbtools](https://github.com/berarma/ffbtools)

The documentation gives good examples, but tl;dr; For steam, insert the following
into a game's launch options:

```shell
ffbwrap --logger=/home/$USER/game.log /dev/input/by-id/usb-Thrustmaster_Thrustmaster_T300RS_Racing_wheel-event-joystick -- %command%
```

This will create a file called `game.log` (with additional timestamp) in your
home directory. Preferably change the name to suit the game, but you do you.

> **NOTE:** Most fixes presented in the documentation are more or less obsolete
> by now, but the tool is still very useful for logging purposes.

## How to capture what USB packets the driver sends to the device?

I'd recommend
[wireshark](https://www.wireshark.org/)

Usb capture setup is fairly straightforward:
[wireshark/CaptureSetup](https://wiki.wireshark.org/CaptureSetup/USB#linux)

Here's what I typically do when starting a capture:

+ Run `sudo modprobe usbmon`. This will load a kernel module that allows
  Wireshark to read the USB packets.

+ Open wireshark with root privileges. There are some ways to allow wireshark to
  access the packets with regular user privileges, I just haven't bothered with
  it.

+ Select `usbmon0` from the view that opens up by default.

+ The screen will quickly fill up with noise from other devices connected to the
  computer, so you will have to filter out the noise.

+ Run `lsusb` in a terminal. Look for the T300 in the list, you should see
  something like `Bus 001 Device 006: ID 044f:b66e ThrustMaster, Inc.
  Thrustmaster T300RS Racing wheel`

+ From the previous command, the Bus and Device fields can be used to filter
  out only packets from/to the device. To see all packets coming from the
  wheel, add in a filter `usb.src ~ "1\.6\..*"`, where `1` is in this case
  from the `Bus` field and `6` from the `Device` field. To see all packets
  being sent to the device, use `usb.dst`.

+ This data is probably also filled with a lot of cruft. Data to/from
  endpoint 2 is button state info, which is probably unnecessary. To see FFB
  data being sent to the device, use endpoint 1, i.e. `usb.dst == "1.6.1"`.
  `~` is a Perl-compliant regex operator, whereas `==` just matches the string
  directly. To see both data coming from the device and going to it, use
  `usb.dst == "..." || usb.src == "..."`.

+ There is also endpoint 0, `usb.src == "1.6.0"` but it doesn't seem to be
  used for much.

+ Use the three buttons in the top left of the screen to start, stop and restart
  captures.

+ Do whatever you want with the device, packets should automatically be
  captured. When you want to save your capture to a file, stop the recording and
  go to `File > Export Specified Packets` and make sure `Displayed` is selected.
  This will apply the filter you've been using, and will only include the packets
  that are visible in Wireshark, i.e. it applies the filter you've specified.

> **NOTE:** Every time you unplug and replug your wheel, its `Device` field will
> probably change.

## How to add in support for a new T-series wheel?

Should probably not be too often that you need this info, but essentially use
wireshark like in the previous example, but spin up a Windows virtual machine
and install the Thrustmaster drivers on to it, and pass the device to the
virtual machine. I prefer to use `qemu` with `virt-manager` as a frontend.

With the wheel working under Windows, install
[fedit.exe](https://gimx.fr/download/b882e209a0ac023d03abbf560dfc3f25fe6367ca/fedit.zip)
and methodically go through all effects the device supports and compare the USB
packets the driver sends out. You should be able to build up a table of what
each value in the USB packet means, see [FFBEFFECTS.md](./FFBEFFECTS.md)
for an example of what I found out about the T300.
