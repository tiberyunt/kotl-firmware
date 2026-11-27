> The native EV9 paths described here are the current prototype. The revised platform target uses PALcode; these instructions do not imply that target is implemented.

# EFI boot selection and OS handoff

Native EV9 now interprets `BootNext`, `BootOrder` and `Boot####` in the EFI global
variable namespace. `BootNext` is deleted durably before its loader runs. A failed
deletion stops automatic boot, so a failed host commit cannot repeat a one-time
option indefinitely. An explicit BootNext may select an inactive option; normal
BootOrder processing skips inactive and non-boot-category entries. Duplicate IDs
in BootOrder are attempted once. A returning loader is unloaded and processing
continues; exhausted options fall back to automatic ESP discovery.

The manager parses bounded EFI_LOAD_OPTION records, validates description and
FilePathListLength, and passes opaque optional data through LoadedImage.LoadOptions.
It sets volatile BS/RT `BootCurrent` before signaling ReadyToBoot and starting the
image. It removes BootCurrent when that image returns. The variable backend still
has 32 slots and a 1024-byte value limit; these bounds also apply to boot variables.
Malformed entries are skipped without rewriting the user's order.

F12 Enter overrides automatic variables for that selection only. F10 changing the
default loader creates or reuses a Boot#### option and promotes it in BootOrder,
retaining other IDs. Timeout and console preferences remain in the separate settings
record. These two backing files are not an atomic transaction: a failed standard
variable update triggers a settings rollback, whose failure is explicitly reported.
Use `variables-file=` for persistence across QEMU processes; the standard run targets
already set it. F10 with no variable backing file has volatile EFI variables and
retains the older private disk preference as its restart fallback.

## Device paths

Each mounted ESP publishes DevicePath alongside BlockIO and SimpleFileSystem. GPT
hard-drive short paths carry the partition number, start, size, and partition GUID.
Resolution matches the GUID and partition number, so PCI slot movement and changed
partition geometry do not change identity. Hardware/PCI/NVMe prefixes, MBR paths,
USB paths and multiple device-path instances are not implemented.

`LoadImage` accepts a GPT HD node followed by one or more UTF-16 FilePath nodes and
EndEntire, or a FilePath-only path relative to the mounted filesystem. An HD-only
path uses `\\EFI\\BOOT\\BOOTAXP64.EFI`. Before application execution, native firmware
can resolve GPT paths across NVMe controllers/namespaces/ESPs. Boot-manager FilePath
short options enumerate fixed-media ESPs, including partitions without a fallback
loader. The current FAT driver owns one mounted volume: an application may load
children from that volume, but switching to a different volume while an application
is running returns EFI_UNSUPPORTED rather than invalidating its file handles.

The loader owns a copy of the device path until UnloadImage. It publishes
LoadedImageDevicePath and sets LoadedImage.DeviceHandle and the relative FilePath.
LocateDevicePath advances past the resolved partition prefix; LocateHandle supports
the usual buffer-sizing pattern. Device paths are limited to 1024 bytes and file
names to 479 UTF-16 code units. Image transport remains read-only with a 4 MiB PE limit.

EFI `Exit()` unwinds the current application's integer-only Alpha stack to StartImage.
It rejects another image's handle, accepts at most 4096 bytes of terminated UTF-16
exit data, and returns a copied buffer to the caller, which must FreePool it. Images
remain available for explicit UnloadImage after return or Exit; automatic application
unload, driver lifecycle, LoadFile transport and the boot watchdog are still pending.
This implements the documented subset, not full UEFI boot-manager conformance.

## GNU GRUB and Linux

GNU GRUB 2.12's real kernel, normal menu, script parser, FAT reader and commands are
compiled statically for Alpha EFI. The platform adapter supplies EFI block access,
console, clock, compiler division helpers, setjmp and the Alpha OS loader. GRUB now
derives its prefix directory from LoadedImage.FilePath, so a Boot#### option can
point outside the fallback directory. Dynamic `.mod` loading is not supported.

```sh
make run-grub-native-display   # native EV9 EFI + graphical GRUB; no native Linux
make run-grub-linux            # Clipper/PAL EFI -> GRUB -> Linux 6.12 -> /init
make run-grub-linux-display    # Cocoa VGA window + serial console
```

The Linux commands build a pinned upstream 6.12 Alpha kernel and a small initramfs.
This is a userspace smoke test, not a desktop distribution. The kernel uses both
`console=tty0` and `console=ttyS0,115200`. Clipper firmware and GRUB use the terminal;
Linux's VGA text console appears in the window. `/init` prints a success marker and
powers off. `DISPLAY_BACKEND=none` can be used on hosts without Cocoa.

The compatibility loader validates an uncompressed Alpha ELF kernel, stages it and
the initramfs in EFI allocations above every kernel destination, and prepares the
Linux/Alpha command line and INITRD parameters. After ExitBootServices it runs a
copied stackless trampoline, masks PAL interrupts, copies/zeros segments, installs
the parameter page, synchronizes caches, and jumps to the kernel. This is a legacy
OSF PAL handoff: Linux does not retain or use EFI runtime services afterward.

Stock Linux/Alpha requires OSF PAL and its 8 KiB, three-level translation model.
Native EV9 uses a different privileged architecture with selectable 4 KiB or 16 KiB translation;
its GRUB `linux` command returns an explicit unsupported-kernel-port error. Native
Linux, or an EV9 PAL compatibility implementation that can run stock Linux, remains
separate work. The successful Clipper boot does not establish either of those.

```sh
make test-boot-options-ui  # F10 persistence and F12 override through QMP
make test-boot-options  # persistence, identity, optional data, path loading, Exit
make test-grub-linux    # GRUB scripts/FAT/runtime + Linux userspace + invalid ELF
make test-edk2          # EDK II AutoGen application build and native EV9 execution
```

Boot-variable behavior and GPT identity matching follow the relevant portions of the
[UEFI boot-manager specification](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/03_Boot_Manager.rst).
The Linux parameter layout comes from the pinned kernel's `arch/alpha/include/asm/setup.h`.

## EDK II firmware image

`make run-edk2` and `make run-edk2-display` now boot the actual upstream DXE Core
through the Kotl SEC/HOB entry, then run native GRUB using EDK II's image loader.
The platform BDS driver integrates the same BootNext/BootOrder/F10/F12 manager.
`make test-edk2-firmware` checks those variables across QEMU processes, EFI app and
GRUB execution, IRQ timer waits, and the ExitBootServices/virtual-runtime boundary.
The native Linux limitation above still applies. See [the EDK II port status](../edk2/README.md).

The native EV9 GRUB build also provides `ev9handoff FILE`. It stages a
self-contained EFI application from the GRUB filesystem and starts it when
`boot` runs. The test ESP uses this command to launch `HANDOFF.EFI`, which
reserves the probe's fixed-address memory, retrieves the device tree from the EFI
configuration table, and calls ExitBootServices. The probe then installs its own
page tables and handles a KAIA timer interrupt.

```sh
make test-linux73-ev9-grub
```

This test covers the firmware-to-GRUB-to-probe path and recovery after a rejected
EFI image. The probe is the entry and platform test for the Linux port. The Linux
7.3.6 kernel still needs native exception, memory management, and platform code.

The EV9 probe now saves an integer exception frame containing the general registers,
EPC, saved status, cause, and fault address. Its dispatcher handles the KAIA timer,
repairs a missing mapping at the selected granule and a write protection fault, and resumes through
ERET. The probe also verifies that TLB invalidation exposes a remapped page and
that a non-executable alias faults. Its own text is mapped read/execute and its
data and stack are read/write. Kernel exceptions use the interrupted kernel stack. User exceptions switch to
a dedicated stack for the boot hart before storing registers. The probe maps a
separate user text page and stack, tests a `callsys` round trip, and checks that
user code cannot read a kernel text address. The syscall behavior belongs to the
probe. Linux syscall dispatch, per-task stacks, and process address spaces still
need integration.

The probe also switches between two page-table roots. Both share the kernel
mappings and user text, while the same user stack address maps to a different
physical page under each root. It runs the first context, the second context,
and then the first again to check that private memory and the Alpha UNIQUE
register value used for TLS survive each switch. Native `rduniq` and `wruniq`
trap to the probe's dispatcher. The switch uses the EV9 PTBR write's local TLB
flush on a single CPU.

`make test-linux73-ev9-kernel` boots the real Linux 7.3.6 Alpha image as far as
its legacy platform setup. QEMU loads the Linux segment into EV9 RAM, and the
native entry code maps it at its linked virtual address before calling
`__start`. The diagnostic trap handler keeps hardware interrupts masked and
provides the early PAL IPL bookkeeping that `start_kernel` and printk expect.
The host test resolves the saved PCs against the kernel symbols and verifies
that Linux reaches `setup_arch` and stops in legacy SRM callback setup. This
diagnostic uses direct QEMU boot. The native kernel still needs an FDT based
`setup_arch` path and ownership of the boot mappings and interrupt controller.

`make test-linux73-ev9-fdt` builds the Linux 7.3.6 kernel with
`CONFIG_ALPHA_KOTL_EV9_BRINGUP` and boots it on the Kotl EV9 machine.
The bootstrap passes the FDT pointer in Alpha argument register `a0`.
The kernel's native `setup_arch` branch validates the Kotl device tree,
adds its RAM range to memblock, reserves the kernel and bootstrap, and
limits early allocations to the window covered by the bootstrap page tables.
The checkpoint ends before generic memory initialization. The existing
early interrupt-mask shim still runs in the diagnostic bootstrap.

The EV9 diagnostic builds default to 4 KiB pages. Use `EV9_PAGE_SHIFT=14`
for 16 KiB pages, for example `make EV9_PAGE_SHIFT=14 test-linux73-ev9-fdt`.
The two choices use separate kernel output directories. These remain early
boot diagnostics; generic Linux paging and userspace have not been ported.
