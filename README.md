# Kotl Firmware

Standalone Alpha AXP EFI firmware for the revised Kotl EV9 QEMU machine, with a
separate legacy Clipper build. It implements an EFI service subset and a small
boot manager. The companion Kotl EDK II project provides the upstream DXE-based
firmware image and now uses the same F-mode/PAL model.

The native firmware boots from flash, scans NVMe GPT/FAT32 EFI partitions,
loads Alpha PE32+ applications, and supports BootOrder/BootNext. It provides
serial output, Bochs GOP graphics, a centered Kotl boot logo, antialiased text,
and dark BIOS-style menus. UEFI executes physically at F level with FMODEL=0. Press **F10** for settings or **F12** for a one-time
boot choice. Display modes include 1024×768, 1080p, 1440p and 4K.

## Build and run

The current scripts target Apple Silicon macOS with Xcode command line tools
and Python 3. First build the emulator and Alpha cross-toolchain in the sibling
`kotl-qemu` checkout, then share that toolchain:

```sh
ln -s ../kotl-qemu/toolchain toolchain
make kotl-efi
make run-kotl-efi
make run-kotl-efi-display
```

The default emulator path is
`../kotl-qemu/build/qemu-kotl/qemu-system-alpha`. Override it with
`KOTL_QEMU=/absolute/path/to/qemu-system-alpha` on run/test commands.
An existing Alpha compiler can be selected with `CROSS=/path/to/alpha-linux-gnu-`.

```sh
make run-kotl-efi-display KOTL_DISPLAY_WIDTH=1920 KOTL_DISPLAY_HEIGHT=1080
make run-kotl-efi-display KOTL_DISPLAY_WIDTH=2560 KOTL_DISPLAY_HEIGHT=1440
make run-kotl-efi-display KOTL_DISPLAY_WIDTH=3840 KOTL_DISPLAY_HEIGHT=2160
make shell-kotl-efi
```

The flash image is `build/kotl-efi/kotl-efi-flash.bin`; the example EFI disk is
`build/kotl-efi/esp.img`. Run targets use snapshot disks. Settings and variables
persist separately in `build/kotl-efi/settings.bin` and `variables.bin`; give
each VM its own files. Serial remains available alongside the graphical console.
Exit a serial QEMU session with Ctrl-a x.

## Test

```sh
make test-kotl-efi
make test-kotl-ui
make test-kotl-setup
make test-events
make test-variables
make test-relocations
make test-kotl-pal
make run-kotl-pal KOTL_PAL_MODEL=pal-4k
make run-kotl-pal KOTL_PAL_MODEL=full-pal
```

The regression suites cover EFI/NVMe boot, graphics and keyboard input,
settings, events, variables, relocations, and retained PAL handoff. Graphics and
input tests use local QMP sockets. These are integration tests, not UEFI
certification. See [the firmware guide](firmware/kotl/README.md).

## Resident Kotl PAL

The resident personality is adapted from
[QEMU PALcode](https://github.com/qemu/qemu-palcode), pinned to commit
`99d9b4dcf27d7fbcbadab71bdc88ef6531baf6bf`. It replaces QEMU-private registers,
shadow registers and HW_REI with EV9 privileged registers, private F stacks,
XRET and retained firmware services. Original sources, copyright notices and
GPL license are included under `firmware/kotl/pal/upstream/`.

The provisional EFI_KOTL_PAL_PROTOCOL selects the compiled-in resident image
before ExitBootServices and returns a retained physical Enter function/context.
Enter commits PAL or full-PAL only after successful exit, invokes PAL reset,
and lets PAL establish page tables and enter K. Tests boot an actual small
kernel through that complete path. It is not yet a complete OSF/1 PAL suitable
for Linux. External-image Stage and Enforce policy remain unsupported.
See [PAL ABI and limits](docs/kotl-firmware-pal.md).

## Scope

Storage is polled and read-only, FAT32 ESPs use 512-byte sectors, and several
UEFI services remain unsupported. Secure Boot, networking, general USB drivers,
virtio-gpu and native EV9 Linux boot are not implemented. The firmware now follows the revised physical F-mode/PAL handoff model.
Firmware-resident PAL selection is supported; complete OSF/1 trap/context
support, external PAL image validation and production trust policy remain work.

`firmware/`, `include/` and `apps/` contain the implementation and test programs.
`scripts/` supplies image/font tools, and `kotl-logo/` contains source artwork.
The small Alpha division helper is included locally so standalone builds do not
need an EDK II checkout. Build outputs, local VM state and dependencies are ignored.

## Development with Codex

OpenAI Codex was used extensively for the Kotl-specific implementation and
porting work, debugging, integration tests and documentation. Upstream QEMU,
EDK II and other dependencies retain their original authorship and licenses.

## License

Original firmware code is MIT licensed; individual files retain their notices.
The QEMU-derived PAL sources are GPL-2.0-or-later. Combined firmware images include GPL code; preserve its notices and meet the
applicable corresponding-source requirements. Original MIT source notices remain intact. See
[PAL license/provenance](firmware/kotl/pal/README.md).
The Alpha division helper retains its BSD-2-Clause-Patent notice. Noto Sans Mono
is distributed under the [SIL Open Font License](firmware/kotl/font/OFL.txt).
