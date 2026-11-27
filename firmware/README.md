# Kotl Firmware

Standalone Alpha AXP EFI firmware for the native Kotl EV9 QEMU machine, with a
separate legacy Clipper build. It implements an EFI service subset and a small
boot manager. The companion Kotl EDK II project provides the upstream DXE-based
firmware image.

The native firmware boots from flash, scans NVMe GPT/FAT32 EFI partitions,
loads Alpha PE32+ applications, and supports BootOrder/BootNext. It provides
serial output, Bochs GOP graphics, a centered Kotl boot logo, antialiased text,
and dark BIOS-style menus. Press **F10** for settings or **F12** for a one-time
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
```

The restored workspace passed 17 native firmware scenarios and 16 BIOS settings
scenarios before this repository split. Graphics tests use local QMP/display
sockets. Tests are integration checks, not UEFI certification. Read
[the native firmware guide](kotl/README.md) for storage discovery,
settings, KSI, memory layout and runtime-service details.

## Scope

Storage is polled and read-only, FAT32 ESPs use 512-byte sectors, and several
UEFI services remain unsupported. Secure Boot, networking, general USB drivers,
virtio-gpu and native EV9 Linux boot are not implemented. The native machine
uses direct CSRs and hardware paging; the later PAL-centric specification is
not the firmware ABI in this snapshot.

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
The Alpha division helper retains its BSD-2-Clause-Patent notice. Noto Sans Mono
is distributed under the [SIL Open Font License](kotl/font/OFL.txt).
