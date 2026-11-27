# Native EV9 EFI experiment

This ports the standalone Alpha EFI subset to the draft Kotl EV9 QEMU platform.
It remains a standalone EFI subset rather than a complete EDK II firmware image.
Native GRUB runs here; Linux currently boots on the separate Clipper/PAL path.
The supplied platform specification and Alpha architecture PDF are unchanged.

From the repository root, with the local cross-toolchain available:

```sh
make kotl-efi                 # firmware, EFI apps and sparse GPT/FAT32 disk
make run-kotl-efi             # also builds modified QEMU; NVMe boot on eight harts
make shell-kotl-efi           # project serial monitor
make run-kotl-efi-display     # Cocoa window on macOS; F10 setup, F12 boot menu
make test-kotl-ui             # 8 graphics/keyboard/discovery scenarios
make test-kotl-setup          # 16 setup/persistence/failure scenarios
make test-events              # 9 event/input/unload scenarios on both platforms
make test-variables           # 13 nonvolatile/runtime variable scenarios
make test-relocations         # 14 emulator cases + 5 converter checks
make test-kotl-efi            # 17 integration scenarios; logs build/kotl-efi/test-*.log
```

Exit QEMU with Ctrl-a x. The default flash boot scans PCI bus zero for NVMe controllers and up to 1024
active namespace IDs per controller, then checks every GPT ESP (up to 128 table
entries). Boot candidates require 512-byte sectors and `\EFI\BOOT\BOOTAXP64.EFI` in a GPT FAT32 ESP. Up to 32 validated boot candidates are retained;
invalid images and corrupt volumes are skipped so later valid ESPs can boot.
There is no fallback to the embedded image on disk failure. Select the embedded
self-test with `-M kotl-ev9,firmware-args=selftest`, or the monitor with
`firmware-args=shell`. Monitor commands are `help`, `memmap`, `boot` (embedded),
`disk` (NVMe), and `shutdown`.

## BIOS-style setup, display and boot selection

`make run-kotl-efi-display` attaches `bochs-display` and opens the native macOS
Cocoa window. Click the window and press **F10** during startup for firmware
setup, or **F12** for the one-time boot menu. Both use the antialiased font with
black backgrounds, highlighted selections and keyboard help.

F10 setup has Main, Boot, Console and Exit tabs. Left/Right or Tab switches tabs,
Up/Down selects a field, and Enter changes a value or activates an action.
F9 loads defaults into the pending configuration; F10 saves and boots; Esc
discards changes and boots. The Exit tab also offers the serial monitor and
shutdown. Main reports actual processor, hart count, memory and boot discovery.

Saved options are the default NVMe/namespace/ESP boot entry, startup delay
(2, 5 or 10 seconds), and graphical text console enablement. Serial stays
available and disabling graphical text does not remove EFI GOP. F12 selection
overrides the saved entry for this boot only; it never writes settings. R rescans
storage and S enters the monitor. Number keys 1–9 also select F12 entries.

The run targets attach `build/kotl-efi/settings.bin`. Override its location with
`KOTL_SETTINGS=/path/to/settings.bin`. QEMU's optional
`-M kotl-ev9,settings-file=...` property loads this 64-byte store; Save creates or
replaces the file. Without the property, changes survive guest reset only and
Main reports "Until QEMU exits". Pending edits never write the store. Save
failure keeps setup open, leaves applied settings unchanged, and permits retry
or discard. Invalid sizes, CRCs, versions and field ranges fall back to defaults
without overwriting the file at boot.

The default entry is matched by PCI requester ID, namespace ID and ESP ordinal.
If topology or partitions change and the entry disappears, the first valid
candidate is used. Rescan to select and save a new default. This is a small
firmware configuration store, not the general EFI variable service or BootOrder
protocol. General EFI variables now have a separate store and service, described
below. Each VM should have its own backing files; there is no multi-process
configuration merge or flash journal.

Serial terminals can send F10 as `ESC [ 21 ~`, F9 as `ESC [ 20 ~`, F12 as
`ESC [ 24 ~`, and conventional arrow escape sequences. A standalone Esc is
decoded after a bounded 50 ms wait.

The firmware initializes Bochs PCI BARs and a BGR framebuffer with modes for
1024×768, 1920×1080, 2560×1440 and 3840×2160. The 4K mode requires 32 MiB
of video memory. It
installs EFI Graphics Output Protocol with QueryMode, SetMode and all four BLT
operations, including overlapping video copies. The 80×25 console fills the framebuffer and uses
[antialiased Noto Sans Mono](font/README.md) with detailed glyph atlases selected
for the active resolution. Framebuffer
access remains available to the loaded application; firmware text rendering
stops after ExitBootServices. The fixed mode is not an EDID-driven mode list.

The emulator now builds Cocoa on macOS and VNC support. For a remote viewer,
use `-display none -vnc 127.0.0.1:1` instead of `-display cocoa` with the same
QEMU device arguments. `-display none` also permits headless GPU tests. This
implements Bochs virtual display support; it does not implement virtio-gpu.
The draft polled keyboard mailbox receives QEMU window/QMP key events and
supports menu navigation, US letters/digits, basic punctuation and shift.
It is not a USB HID driver or a general keyboard layout implementation.

## Execution and memory

Stage 1 executes in F privilege from flash at zero. It parses QEMU's DTB in SRAM,
checks required controller descriptions, copies the EFI core into DRAM, installs
16 KiB native page tables with 32 MiB identity leaves, and enters K privilege with
translation enabled. The EFI core uses the shared protocol database, allocator,
PE loader, Block I/O, GPT and FAT32 implementation. It publishes the retained DTB
under the EFI device-tree configuration-table GUID.

| Physical range | Use |
| --- | --- |
| 0–16 MiB | Flash, retained stage 1 and embedded image |
| 0x01000000–0x011fffff | SRAM, stage-1 state, page tables, stack and DTB |
| RAM + 0–2 MiB | Reserved IOMMU context/page tables |
| RAM + 2–4 MiB | Retained EFI core and stack |
| RAM + 4–8 MiB | Boot data reservation; first six 16 KiB pages are DMA buffers |
| RAM + 8 MiB onward | Allocatable EFI memory |
| RAM + 16 MiB | Fixed PE32+ application base (reserved when loaded) |

RAM begins at 0x100000000. Firmware accepts 64 MiB through 1 GiB, although QEMU
supports larger configurations. AllocatePages addresses and C pointers refer to
the same identity-mapped memory. EFI allocation units remain 4 KiB; native
hardware translation units are 16 KiB. The PE loader uses the project's preferred native image base (0x101000000)
when available and relocates eligible images elsewhere in RAM when necessary.
Images still use Alpha64 machine code 0x284 and the experimental local ABI. The local
`BOOTAXP64.EFI` filename is not an assigned UEFI architecture filename.

## Storage and handoff

The NVMe driver scans PCI bus zero, assigns the selected controller's BAR,
uses polling with bounded waits, identifies the selected active namespace, and
creates admin and I/O queue pairs of depth 16. Reads use a private bounce buffer
in chunks up to 4 KiB. Only six 16 KiB pages are mapped for that requester's DMA;
firmware code, application RAM and stacks have no device mappings. The driver
supports 512-byte sectors without metadata and does not write storage. QEMU's
snapshot backend preserves the source disk. Discovery switches controllers with
old decode and DMA disabled, removes the previous volume's protocol handles,
and refuses to switch while a file or application is open. EFI filesystem
protocols expose the selected volume; the menu inventories the other candidates.
GPU BARs occupy the top 32 MiB of the PCI32 window; the active NVMe controller
uses its bottom aperture, avoiding overlap.

ExitBootServices validates the application and map key, selects the native
model, disables NVMe, disables PCI bus mastering and restores IOMMU default deny
before ending boot services. Retained reset/shutdown/time calls use stage 1's
KSI handler. The implemented service numbers are documented in
[the draft bindings](../../docs/kotl-ev9-qemu-bindings.md).

Tests cover embedded and NVMe boot at three RAM sizes, eight-hart topology,
a relocated PCI slot with xHCI present, namespace ID 7, missing devices and
namespaces, unsupported 4 KiB sectors, fragmented reads through queue wrap,
repeated image load/unload, malformed GPT/PE, runtime reset and source-disk
preservation. The separate UI suite verifies GOP reads/writes and bounds,
real QMP F12/arrow/Enter input, serial F12, an empty menu, a second ESP,
a second controller and a second namespace. It captures the actual framebuffer
to `build/kotl-efi/boot-settings.ppm`. The setup suite separately tests F10 and
serial F10, Main/Boot/Console/Exit navigation, save/discard/default actions,
new-process persistence, default-entry selection, F12 override, graphical text
disablement, failed writes, corrupt stores, invalid ranges and rescanning. It
captures `build/kotl-efi/firmware-setup.ppm`. The guest independently verifies the EFI DTB, native execution,
identity allocation addresses, configuration table lifecycle, unsupported KSI
calls and IOMMU shutdown after ExitBootServices.

## Remaining work

Only the primary hart executes firmware. CPU identity mappings use coarse RWX
leaves; page permissions and F/K stack isolation are incomplete. Resident KSI
runs on the caller's stack and is not a security boundary. No PAL model, hart
services, remote fences, interrupt-driven NVMe or MSI remapping are implemented.
QEMU can warn that MSI-X is unsupported; this driver uses polling. USB is not a
firmware input or storage path. Time is virtual elapsed time, not a clock/calendar.

Interrupt-driven event delivery, authenticated variables, complete runtime/boot
services, full PCI driver binding, device-path LoadImage, ACPI,
Secure Boot and an OS loader
remain pending. Required EDK II architecture/toolchain/platform integration is
also pending. Retaining native firmware memory and passing this test application
does not establish a general OS runtime ABI or full platform conformance.

## EFI events, timers and input waits

The shared EFI core now implements typed RaiseTPL/RestoreTPL, CreateEvent and
CreateEventEx, SignalEvent, CheckEvent, CloseEvent, WaitForEvent and SetTimer.
There are 64 event slots with generation-tagged opaque handles; stale handles do
not resolve to a reused slot. Supported notification levels are TPL_CALLBACK and
TPL_NOTIFY. Notifications dispatch in priority order and FIFO order within a
priority, coalesce while masked, and may close their own event safely. Runtime
and virtual-address-change events remain unsupported.

Timers accept 100 ns units, relative or periodic operation and cancellation.
Zero-delay timers fire at the next 1 ms software tick. Periodic catch-up preserves
phase and coalesces missed notifications. Dispatch occurs from WaitForEvent,
CheckEvent, RestoreTPL, ReadKeyStroke and Stall, not from hardware interrupts.
An application which never yields to these entry points cannot receive timer
callbacks yet. WaitForEvent requires TPL_APPLICATION.

The console publishes a real WaitForKey event. Readiness checks retain the
unread key; consecutive queued keys and native EFI scan codes are preserved.
BeforeExitBootServices notifications can change the map, requiring a fresh key.
Exit notifications run with timers stopped and memory allocation blocked. Event
groups also notify memory-map and configuration-table changes. Unloading an
application retires its owned events before executable pages are released.

`make test-events` builds separate applications for timeout, serial input,
consecutive input, native F10 scan delivery and forty repeated image unloads.
The main self-test also checks callback ordering, priority masking, grouped
signaling, capacity, stale handles and exit notifications on both platforms.

## EFI variables and retained runtime support

GetVariable, SetVariable, GetNextVariableName and QueryVariableInfo are available.
Variables are identified by UTF-16 name plus vendor GUID, with replacement,
append and deletion support. The bounded store has 32 shared slots, up to 63
UTF-16 name units plus a terminator, and up to 1024 data bytes per value.
QueryVariableInfo reports the fixed-slot capacity. Unsupported authenticated,
hardware-error and enhanced-authentication attributes return EFI_UNSUPPORTED.

Volatile values work on both Clipper and EV9. Native nonvolatile writes require
`-M kotl-ev9,variables-file=PATH`; without a file backend they return
EFI_UNSUPPORTED. The run targets use `build/kotl-efi/variables.bin`, overridden
with `KOTL_VARIABLES=PATH`. This file is separate from F10's 64-byte configuration
record. Only nonvolatile variables are serialized; volatile values disappear on
reset. Firmware validates the entire 64 KiB store, including CRC, record bounds,
names and duplicates, before exposing it. Invalid stores load as empty without
an implicit write.

A persistent update publishes its in-memory value only after the host commit
succeeds; failed creates/updates/deletions preserve the old value. QEMU requests
consistent replacement and durable synchronization through GLib before reporting
commit success. This is an emulator backing-file service, not NOR flash or a
wear-leveling/journal implementation. Concurrent VMs must use separate files.

After ExitBootServices, boot-only variables are hidden, volatile runtime values
are read-only, and nonvolatile runtime values remain writable through the
separate syscon backend with NVMe disabled. EFI_RT_PROPERTIES_TABLE advertises
implemented runtime calls and whether persistent writes are available. This
still requires the retained experimental memory mappings: SetVirtualAddressMap
and a general OS runtime handoff remain pending.

`make test-variables` checks persistence across processes, runtime writes,
volatile exclusion, deletion, failed-create/update rollback and malformed stores,
including mutations with valid CRCs. The core self-test checks the API's sizing,
attributes, UTF-16 names, enumeration, capacity and runtime visibility rules.
These variables are not yet interpreted as BootOrder/BootNext/Boot####; F10/F12
continue to use the current firmware boot manager.

## Relocatable images and nested execution

`firmware/pe.c` supports up to 16 simultaneous images, including a parent running
its child. LoadImage tries the preferred address first. If that allocation is
unavailable and IMAGE_FILE_RELOCS_STRIPPED is clear, it allocates other RAM and
applies the supported relocation records. Fixed images still require their
preferred address. LoadedImage.ImageBase reports the actual address, with the
requested ParentHandle retained. A completed handle cannot be started again;
unload invalidates it, releases pages and retires its owned callbacks. Running
images and active ancestors cannot be unloaded from their own call stack.

The PE relocation directory supports ABSOLUTE padding, HIGHLOW (32-bit) and
DIR64 (64-bit). Blocks may be out of page order. Unaligned and page-crossing
fields are updated bytewise. All records are validated before any fixup is
applied, including directory backing, block sizes, mapped targets, overlaps and
attempts to modify relocation metadata or headers. Failures release the reserved
image slot, pages and temporary validation bitmap. Other PE directories and
relocation kinds remain unsupported; this is not a general Windows loader.

Application links now use `--emit-relocs`, and make targets pass `--relocatable`
to `scripts/elf-to-pe.py`. The converter translates retained ELF REFLONG/REFQUAD
records into PE fixups and resolves linker-created GOT entries from LITERAL
relocations. It derives the actual GP from verified GPDISP instruction pairs;
the linker's `_gp` symbol can differ due to GOT alignment. It uses relocation
provenance, not scanning for address-shaped integers. PC/GP-relative references
remain relative. Supported inputs are static ELF64 Alpha with one GP/GOT domain;
TLS, dynamic linking, unknown relocations and unclassified GOT entries are
rejected. Omitting `--relocatable` still produces an explicitly fixed image.

`make test-relocations` executes parent/child tests on Clipper and EV9 at three
RAM sizes, plus zero/below-RAM/high preferred bases, eight harts and the graphical
display. The guest tests 32/64-bit fixups, packed/cross-page pointers, initialized
function/data pointers, callbacks, BSS reset, fixed-base collisions, 40 repeated
child loads, image capacity and malformed tables without leaks. Five independent
converter mutations test metadata/target/symbol/TLS rejection. The fixture also
contains an address-shaped integer that must not be rebased.

## Boot variables, OS handoff and EDK II

Standard BootOrder/BootNext and GPT/FilePath image loading are now implemented.
F10 default selection promotes a Boot#### option; F12 Enter overrides once.
EFI Exit unwinds nested applications, returning copied exit data to StartImage.
See [../../docs/boot-and-handoff.md](../../docs/boot-and-handoff.md) for limits,
tests and the verified GRUB/Linux Clipper compatibility boot.

The [EDK II foundation](../../edk2/README.md) builds and boots an ALPHA64
AutoGen application. A full native EDK II firmware image and native Linux kernel
port are still required. Other major remaining work includes driver lifecycle,
concurrent mounted volumes, interrupt-driven events, executable/data permissions,
a private F-level stack, full KSI services, runtime mappings and the required
network, USB, security and firmware-update protocols.
