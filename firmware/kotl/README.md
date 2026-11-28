# Kotl EV9 firmware

A standalone Alpha EFI service subset for the revised Kotl EV9 emulator.
UEFI, applications and runtime services run physically at F level, FMODEL=0.
The resident Kotl PAL is prepared before ExitBootServices and activated only
by the retained Enter function afterward. The separate EDK II checkout also uses this F-mode/PAL model. This is not UEFI certification or Linux boot.

## Build and use

From this repository with a local Alpha toolchain and the sibling QEMU built:

```sh
make kotl-efi
make run-kotl-efi
make shell-kotl-efi
make run-kotl-efi-display
make run-kotl-efi-display KOTL_DISPLAY_WIDTH=1920 KOTL_DISPLAY_HEIGHT=1080
make run-kotl-efi-display KOTL_DISPLAY_WIDTH=2560 KOTL_DISPLAY_HEIGHT=1440
make run-kotl-efi-display KOTL_DISPLAY_WIDTH=3840 KOTL_DISPLAY_HEIGHT=2160
```

`KOTL_QEMU=/absolute/path/to/qemu-system-alpha` overrides the companion emulator.
Run targets use the Z1-lite test part for their four-core/two-thread topology.
Ordinary 21564 defaults to four single-thread cores. Unchanged outputs are
reused. Flash is `build/kotl-efi/kotl-efi-flash.bin`; NVMe ESP is
`build/kotl-efi/esp.img`. Disks use snapshot mode. Serial QEMU exits with Ctrl-a x.

Press F10 during startup for Main/Boot/Console/Exit settings, or F12 for a
one-time boot choice. Menus use a full-black background, centered Kotl splash
and detailed antialiased fonts. Arrow keys/Tab navigate, Enter selects, F9 loads
pending defaults, F10 saves, Esc discards. Serial F10/F12 escape sequences also
work. The display path uses Bochs GOP, supporting 1024x768, 1080p, 1440p and 4K.
Text rendering uses the active framebuffer dimensions. Serial remains available.

Settings and EFI variables persist in separate files under `build/kotl-efi/`.
Use separate `KOTL_SETTINGS`/`KOTL_VARIABLES` paths for independent VMs.
Writes stage separately; failed host-file replacement preserves the old record.
Authenticated variable writes and physical flash journals remain unsupported.

## Boot and storage

Stage one validates the DTB, installs the F exception vector and direct KSI
entry, copies the EFI image to RAM+2 MiB and calls it at F. It creates no CPU
identity page tables and enters no kernel privilege while boot services run.
EFI allocation units remain 4 KiB; physical addresses equal C pointers.
Supported firmware RAM is 64 MiB through 1 GiB, although QEMU supports more.

The boot manager scans PCI bus zero, NVMe namespaces and GPT FAT32 ESPs for
`\EFI\BOOT\BOOTAXP64.EFI`. That filename and the Alpha PE machine/ABI are
experimental. It supports BootOrder/BootNext, device paths, relocation and
nested image lifecycle. Invalid candidates are skipped. Filesystems are
read-only, with 512-byte sectors. NVMe uses polling and six private 16 KiB
DMA pages. Requester contexts now use the 32-byte v2 format and native
three-level 16 KiB page tables. No arbitrary device DMA reaches firmware RAM.
GPU BARs are kept apart from NVMe BAR allocation.

`-M kotl-ev9,firmware-args=selftest` selects the embedded test; `shell` opens
the monitor (`help`, `memmap`, `boot`, `disk`, `shutdown`). The normal boot path
uses NVMe. ExitBootServices validates the map key, quiesces storage/DMA and
releases boot services without changing FMODEL. A prepared PAL transfer then
uses Enter. A loader that never prepares it remains an F-level EFI test app.

## PAL and memory

The [resident personality](pal/README.md) derives from QEMU PALcode; it is GPL
licensed, with upstream source and provenance retained. UEFI exposes a
provisional GetInfo/Stage/Activate protocol; only the trusted compiled-in
resident image is currently available. Stage explicitly returns unsupported.
The developer policy is Permissive; Enforce, Db/Dbx, SVN persistence and TPM
measurement are not implemented.

Memory relative to RAM base 0x100000000:

| Range | Use |
| --- | --- |
| 0..2 MiB | Reserved metadata and private IOMMU tables |
| 2..4 MiB | Retained EFI code/data/stack |
| 4..6 MiB | Boot/DMA reservation |
| 6 MiB..6 MiB+64 KiB | Resident PAL code (F-only RX after transfer) |
| 6 MiB+960 KiB..7 MiB | Retained read-only F PAL information/context |
| 7..8 MiB | Sixteen private 64 KiB F scratch/stack areas |
| 8 MiB onward | EFI-managed memory, application images and OS allocations |

The entire PAL range stays reserved in the EFI map. The physical DTB is in the
reserved upper MiB of SRAM; `/firmware/kotl-pal` publishes resident, pending
and active state. Its structure is finalized before selection; fixed fields
change without allocation during transfer. See [the PAL binding](../../docs/kotl-firmware-pal.md).

## Validate

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

The PAL test uses real EFI applications, both hardware models, a K-mode kernel,
4 KiB/16 KiB native tables, stale map keys, early Enter, descriptor copying,
one-shot selection, F-only code protection and a corrupt
retained context. Full-PAL also tests exclusion of SMT siblings. Kernel tests
exercise adapted TLS, IMB, system value, stack pointer, TLB and console calls.

The graphical suites drive QMP keyboard input, F10/F12 menus, persisted settings,
ESP/namespace/controller discovery and real GOP writes. Captures remain under
`build/kotl-efi/`. Storage tests check reset and source-disk preservation.
Legacy Clipper tests retain their separate PAL/firmware path.

## Remaining

A complete OSF/1 personality (PCB/context switching, exception frames, user
return, interrupt delivery and HWRPB/SRM) is needed before Linux can boot here.
Secondary OS scheduling, the full KSI lifecycle, external-image staging,
authentication/measurement, network boot, USB firmware drivers and virtio-gpu
remain unsupported. No existing Linux/GRUB OS handoff is claimed by the small
kernel test. EDK II integration has its own tests and runtime limits in the sibling checkout.

OpenAI Codex assisted the implementation, porting, debugging, tests and docs.
Original source/file-level notices and upstream licenses are preserved.
