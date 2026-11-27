# Experimental Alpha EFI binding

## Relationship to Kotl EV9

This file describes the existing Clipper EFI prototype. The supplied Kotl EV9
specification defines a different native platform, now modeled separately in
`qemu/`. Its implementation and provisional hardware ABI are documented in
[qemu/README.md](../qemu/README.md) and
[kotl-ev9-qemu-bindings.md](kotl-ev9-qemu-bindings.md). The existing PAL-based
firmware is not presented as an implementation of that native platform.
A separate [native firmware port](../firmware/kotl/README.md) now reuses its EFI
core with identity mappings, resident KSI, NVMe, graphical GOP, F12 settings and
automatic bootable-ESP discovery. It remains independent of EDK II.

## Scope

The implementation provides a native Alpha EFI service subset for QEMU Clipper.
It is an executable starting point for an Alpha UEFI effort, not a conforming
UEFI firmware, EDK II port, SRM replacement for physical machines, or native EV9 OS boot path. The Clipper compatibility path now boots Linux through GRUB.

The installed emulator is QEMU 10.1.3. Its existing `-kernel` loader accepts an
Alpha ELF executable, translates KSEG load addresses to physical RAM, and passes
the entry point to its bundled `palcode-clipper` firmware. PALcode initializes
the chipset, page tables and HWRPB, switches to OSF PALcode, and jumps to the
payload. No QEMU source changes are necessary for this path.

## Boot sequence

1. QEMU loads PALcode and `build/alpha-efi.elf`.
2. PALcode performs platform initialization and transfers control to `_start`.
3. `_start` establishes GP and a private 64 KiB stack, clears BSS, and calls C.
4. Firmware initializes serial I/O and discovers RAM from HWRPB memory clusters.
5. Firmware builds EFI tables and selects the embedded image or an external
   buffer loaded by QEMU's `-initrd` facility. With `-append disk`, it instead
   discovers the CMD646 controller, reads a GPT ESP and opens the FAT32 boot file.
6. LoadImage validates and copies the PE sections into an allocated RAM range,
   zeros the image including BSS, and performs an instruction-memory barrier.
7. StartImage calls the application's entry wrapper with its handle and the
   EFI system table. The wrapper establishes the application's GP.
8. A returning application is unloaded; an OS-style application can obtain a
   final memory map and call ExitBootServices with the current key.
9. Runtime ResetSystem currently shuts down QEMU through OSF CALL_PAL halt.

The project does not replace PALcode: `alpha-efi.elf` is a firmware payload.
Booting it with `-bios alpha-efi.elf` is incorrect because that interface expects
PAL-mode firmware implementing the processor's exception entry points.

## Local processor binding

| Property | Convention |
| --- | --- |
| ISA | Alpha EV67, little endian, integer instructions only |
| C ABI | GNU Alpha LP64; 64-bit pointers, UINTN and EFI_STATUS |
| Calls | Arguments in r16-r21, return in r0, procedure value in r27, GP in r29 |
| Stack | r30, 16-byte aligned |
| Character data | Explicit uint16_t CHAR16; no dependency on host wchar_t |
| Status errors | Bit 63 set |
| PE machine | IMAGE_FILE_MACHINE_ALPHA64 / AXP64, 0x0284 |
| PE optional header | PE32+, EFI application subsystem 10 |
| Image loading | Preferred address with HIGHLOW/DIR64 fallback relocation; 16 images; no imports or TLS |
| Example preferred image base | 0xfffffc0001000000; LoadedImage reports the actual address |
| EFI allocation units | 4096 bytes; Alpha hardware translation pages remain 8192 bytes |
| Application filename | BOOTAXP64.EFI is a project convention only |

The Microsoft PE format assigns 0x0284 to Alpha64, but that assignment does not
standardize this project's EFI ABI. Existing Alpha Windows binaries do not
therefore become compatible with this environment.

EFI structures retain the 64-bit UEFI table layouts. Compile-time assertions
check important offsets. Firmware calculates CRC32 on system, boot-services
and runtime-services headers; the guest test application independently checks
them. The advertised revision describes the chosen table layout, not a claim
of UEFI conformance.

## Memory

| Physical range | Use |
| --- | --- |
| 0-2 MiB | PALcode, tables and bootstrap area, reserved |
| 2-4 MiB | Firmware code, data, stack, allocator metadata; retained at runtime |
| 4 MiB to RAM end | Conventional RAM except allocated images and external image buffer |
| 16 MiB by default | Preferred address for the example EFI application |

RAM must be 64 MiB through 1 GiB. The HWRPB at virtual address 0x10000000 is
provided by QEMU PALcode, not assumed to be identity mapped RAM. The allocator
respects reserved PALcode clusters and records allocated pages separately so
reserved firmware cannot be freed through FreePages. Pool metadata lives in
allocated data pages. Map keys change whenever page ownership changes.

Code, data and function pointers currently use the Alpha KSEG alias
`0xfffffc0000000000 + physical_address`. EFI physical addresses and memory-map
descriptors report the underlying physical address. An application currently
converts an AllocatePages result through KSEG before accessing it. This is an
explicit deviation from standard UEFI pre-boot identity mapping. Resolving the
mapping model is required before OS interoperability can be claimed.

KSEG does not enforce PE section permissions. The prototype uses combined
read/write/execute payload segments. Firmware memory is retained as a single
runtime code range covering its code and data. Runtime virtual remapping is
not implemented; runtime shutdown works with the existing KSEG mappings only.

Up to 16 applications can be loaded simultaneously, with nested StartImage,
integer-only EFI Exit unwinding and explicit UnloadImage. GPT/FilePath loading
is implemented within the bounds in [boot-and-handoff.md](boot-and-handoff.md).
Driver binding and automatic application unload are not implemented.

## External applications

```sh
qemu-system-alpha -M clipper -cpu ev67 -m 128M -smp 1 \
  -nographic -nic none -kernel build/alpha-efi.elf \
  -initrd build/HELLOAXP64.EFI
```

`-initrd` is a QEMU transport for an application buffer, not Linux initrd
semantics inside this firmware. QEMU stores the buffer address and length in
its Alpha boot parameter block. Firmware validates the range, reserves those
pages, and passes the buffer to EFI LoadImage. This transport does not use the disk filesystem path. Disk boot below provides
a separate GPT/FAT32 ESP and EFI filesystem protocols.

`apps/hello.c`, `apps/entry.S` and `apps/link.ld` are the minimal application
example. `scripts/elf-to-pe.py` packages the linked Alpha ELF segments into a
PE32+ image with deterministic headers and optional relocation metadata.
The make targets link with `--emit-relocs` and request relocatable output. The cross-compiler itself
emits ELF; it does not contain an Alpha PE backend.

## Disk boot and protocols

`make run-disk` builds a sparse GPT/FAT32 disk and boots
`\EFI\BOOT\BOOTAXP64.EFI` from the primary IDE master. The name remains the
project's experimental Alpha convention. The firmware discovers CMD646 on PCI
bus zero and reads its BARs after PALcode assignment. ATA uses polled PIO,
512-byte sectors, LBA28 and bounded status waits. No DMA or disk writes occur.
QEMU's `snapshot=on` gives the IDE backend a temporary writable layer.

Block I/O is published on two handles: the disk and its selected GPT ESP.
Media IDs, buffer sizes and LBA bounds are checked. GPT primary header and entry
array CRC32 values, table bounds and the ESP range are checked before mounting.
The implementation reads up to 128 GPT entries of 128 bytes each and does not
recover from the backup header when the primary is damaged.

The FAT32 reader validates BPB geometry, FAT capacity, active FAT selection and
cluster ranges. Files are checked for the declared chain length and EOC before
opening; cycles and truncated chains are rejected. Reads traverse fragmented
chains. Directory traversal is bounded to 4096 raw entries per scan. Names use
8.3 entries or checksummed VFAT long-name sequences, with ASCII case folding.
The current filesystem supports 512-byte sectors and power-of-two clusters.
There are 16 simultaneous file handles; timestamps are returned as zero.

The Simple File System protocol provides OpenVolume. File protocol revision 1
provides read-only Open, Close, Read, Get/SetPosition, FileInfo GetInfo, directory
enumeration and Flush. Writes, creation and SetInfo are rejected; Delete closes
the handle and returns EFI_WARN_DELETE_FAILURE. Directory SetPosition supports
rewinding to zero. Other filesystem info GUIDs and asynchronous file I/O are
not implemented. LoadedImage.DeviceHandle identifies the ESP; FilePath and
LoadedImageDevicePath describe the copied path used by path-based LoadImage.

A static protocol database supports 48 bindings and 24 concurrent dynamically
created handles, plus internal device handles. Install/reinstall/uninstall,
HandleProtocol, LocateProtocol, LocateHandleBuffer, LocateHandle and the supported
GPT/FilePath subset of LocateDevicePath are implemented. Marker
protocols with null interfaces and handle slot reuse are supported. OpenProtocol
supports GET_PROTOCOL and TEST_PROTOCOL without driver ownership tracking.
Notification, driver binding and ownership modes are not implemented. Console,
loaded-image, Block I/O and filesystem interfaces are registered in the database.

The disk generator writes standard primary/backup GPT structures and a FAT32
ESP, including FSInfo and backup boot sectors. Applications and PATTERN.BIN use
fragmented clusters so the test cannot succeed by assuming contiguous files.
The native macOS FAT checker was also run against the generated ESP.

## Work required for a complete implementation

1. Specify an Alpha UEFI binding, including physical/virtual addressing,
   exception handling, alignment, floating point and runtime calling rules.
2. Implement the pre-boot identity map and retained runtime mappings.
3. Complete driver-image lifecycle and automatic application unload.
   Relocation, nested images, GPT/FilePath loading and EFI Exit are implemented.
4. Complete required boot services, interrupt-driven events, protocol notifications and driver ownership.
   The shared core now supplies cooperative events, timers, TPL dispatch and WaitForKey.
5. Extend storage, hardware device-path forms and concurrent volume support.
6. Complete boot recovery/driver/watchdog policy and required runtime services.
   Native EV9 now supplies persistent BootOrder/BootNext/BootCurrent processing.
7. Complete native EV9 OS integration. GRUB boots stock Linux on the Clipper/PAL
   compatibility machine through the implemented ELF/initramfs handoff.
8. Validate against relevant UEFI tests after adapting those tests to Alpha.

The separate native EV9 EDK II image now implements ALPHA64 build metadata,
BaseLib/compiler helpers, cache/exception primitives, SEC/HOB entry, actual DXE
Core and runtime drivers, CPU/timer architectural protocols, BDS and hardware
library integration. Its core allocator, events, protocol database and PE loader
come from upstream EDK II. See [../edk2/README.md](../edk2/README.md) for commands,
validation and remaining platform work. The sections below describe the standalone
firmware's shared event and variable implementations; EDK II uses its own event core.

## Shared event and variable service additions

`firmware/events.c` implements a bounded cooperative event scheduler with typed
EFI table slots, notification priorities/FIFO, timer catch-up, event groups and
exit-phase handling. `firmware/services.c` connects the console WaitForKey,
map/configuration notifications and timer polling in Stall. Image unload retires
owned callbacks before releasing code. Events are not yet hardware-preemptive.

`firmware/variables.c` supplies bounded name/GUID variables, UTF-16 enumeration,
attributes, append/delete, storage queries and boot/runtime visibility. Clipper
supports volatile variables; native EV9 optionally commits nonvolatile records
through a separate QEMU syscon backing-file service. The runtime-properties table
reports the available services. F10 configuration remains a separate record;
F10 default changes also promote a standard BootOrder entry. BootNext is consumed
before execution, and BootCurrent/optional load data are provided to the loader.

The implemented API behavior follows the [UEFI event services](https://uefi.org/specs/UEFI/2.10_A/07_Services_Boot_Services.html)
and [variable service definitions](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/08_Services_Runtime_Services.rst)
within the explicit bounds and execution limits documented in the native
firmware README. These tests do not establish full UEFI conformance.

## PE relocation binding

The shared loader now accepts PE base relocations ABSOLUTE (0), HIGHLOW (3)
and DIR64 (10). The 32/64-bit fields receive the unsigned load-address delta,
including negative deltas modulo their field width. The full directory is
validated before application; a temporary per-byte bitmap rejects duplicate or
overlapping fields. Targets must be contained in a mapped section and cannot
alter headers or relocation metadata. The directory itself must be file-backed.

Up to 16 generation-tagged image handles can coexist. StartImage preserves the
calling image's identity across nested execution. Unload rejects active images,
removes protocol bindings and callbacks, and releases only that image's pages.
Application code/data still use coarse shared mappings; isolation and W^X remain
future work. A returning application is explicitly unloaded by the caller in this
prototype. EFI Exit unwinds the active image; automatic unload and full driver
lifecycle handling remain outstanding.

The ELF converter handles statically resolved Alpha absolute fields and GOT
slots using retained relocation provenance. GNU Alpha BFD's coalesced-section
RELA addends may omit input-section contributions, so linked field values remain
authoritative for STT_SECTION references. GPDISP pairs establish the actual GP;
multiple GP domains and nonstandard initializers are rejected. No binary address
heuristic or executable instruction rebasing is used. These choices extend the
local experimental Alpha binding; they do not register an official UEFI ABI.

The directory structure and generic fixup values follow the
[Microsoft PE/COFF format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
Alpha ELF relocation kinds are taken from the pinned binutils sources and
[GNU assembler relocation documentation](https://sourceware.org/binutils/docs/as/Alpha_002dRelocs.html).
