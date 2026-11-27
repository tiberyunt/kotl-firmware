# Kotl EV9 Platform Specification

Draft 0.1 · 24 November 2026 · Status: working draft, nothing here is frozen

## 1. Scope and conventions

This document defines the **Kotl EV9 platform**: the architecture baseline, memory map, interrupt system, boot flow, and peripheral requirements that every EV9-class chip, board, and emulator must follow so that one OS image and one firmware image can run on all of them.

- **Kotl EV9** is the architecture and platform generation. It continues the Alpha AXP line past the cancelled EV8.
- **Kotl Z1** is the first implementation: 4 cores, 2-way SMT (8 hardware threads).
- **kotl-ev9** is the QEMU machine type, the golden reference model. When this document and QEMU disagree, that is a bug to be resolved explicitly, not silently.
- Keywords MUST, SHOULD and MAY follow RFC 2119. Numbers in hex are written `0x...`. "Hart" means one hardware thread.
- Items marked **\[OPEN\]** are undecided and collected in section 17.

This spec supersedes the earlier informal Z1 sketch where they differ (notably the cache hierarchy in section 4).

## 2. Platform overview

```
  +---------------------------------------------------------------+
  |                         Kotl Z1 SoC                           |
  |  +--------+ +--------+ +--------+ +--------+                  |
  |  | Core 0 | | Core 1 | | Core 2 | | Core 3 |   2 harts each   |
  |  | L1 I/D | | L1 I/D | | L1 I/D | | L1 I/D |                  |
  |  |  L2    | |  L2    | |  L2    | |  L2    |   private L2     |
  |  +---+----+ +---+----+ +---+----+ +---+----+                  |
  |      +----------+-----+------+----------+                     |
  |                 |  Coherent interconnect (ring/xbar)          |
  |      +----------+-----------+------------+                    |
  |   Shared L3        DDR5 controller     I/O fabric + IOMMU     |
  |                          |                  |                 |
  |                       2x DDR5      PCIe RC / xHCI / GbE /     |
  |                                    SATA / SD / UART / SPI ... |
  +---------------------------------------------------------------+
```

Design principles:

1. **Standard buses, no legacy chipset.** PCIe, xHCI, NVMe and virtio-style models only. No ISA bridge, no PS/2 requirement, no Tsunami/Typhoon-style custom chipset.
2. **Firmware you can replace.** UEFI and a protected, replaceable PALcode layer provide the privileged software environment. Hardware retains the page walker and the fast paths specified below.
3. **Everything discoverable.** Devicetree is mandatory, ACPI is optional. The OS must never hardcode addresses beyond what firmware hands it.
4. **Emulator first.** Every feature appears in `kotl-ev9` before it appears in RTL.

## 3. CPU architecture baseline

### 3.1 ISA

- 64-bit Alpha AXP, little-endian, 32 integer and 32 floating-point registers, fixed 32-bit instruction encoding.
- **Mandatory extensions:** BWX (byte/word load/store), MVI (multimedia), FIX (sqrt, int/FP moves), CIX (count, bit operations).
- **EV9 additions:**
  - **AMO**: single-instruction atomic read-modify-write and compare-and-swap (32/64-bit, plus 128-bit CAS) so scalability does not depend on load-locked/store-conditional retry loops alone. LL/SC (LDL\_L/LDQ\_L, STL\_C/STQ\_C) remains.
  - **Zero-extension and bit-manipulation** helpers, to be specified in the ISA volume.
  - **Speculation barrier** instruction for software mitigations.
  - **Cache management operations**: clean, invalidate, clean+invalidate by address, and a zero-line operation.
- **Vector/SIMD:** reserved encoding space. Z1 MAY omit it. Software MUST NOT assume it exists without feature discovery.
- **FP:** IEEE 754-2008, with precise exceptions (no imprecise-trap quirks of early Alpha). VAX FP formats are **not** supported.

### 3.2 Memory ordering

EV9 is a **weakly ordered** architecture, formally specified (an axiomatic model published as an appendix, in the style of RVWMO).

- **Address-dependency ordering is guaranteed in hardware.** This removes the one property that made original Alpha unique among architectures and forced `read_barrier_depends` into Linux. Software MUST still use explicit barriers for all other orderings.
- Barriers: `MB` (full), `WMB` (store-store), and a new `RMB` (load-load). Acquire/release variants are encoded in the AMO instructions.
- Atomicity: naturally aligned accesses up to 64 bits are single-copy atomic. Misaligned accesses within a cache line MUST work, and MAY be slow.

### 3.3 Privilege model

EV9 uses PALcode as its privileged software layer. In the normal EV9 environment, except for the hardware fast calls specified below,
CALL_PAL and architectural exceptions enter protected PALcode, which implements the OS-facing privileged
interface and dispatches exceptions to the operating system. UEFI runs before OS
handoff and installs the PAL image. The PAL interface and the underlying
PAL-only hardware interface are distinct: the latter is not an OS ABI.

| Level | Name | Purpose |
| --- | --- | --- |
| 0 | U | User |
| 1 | K | Kernel / supervisor |
| 2 | V | Reserved for virtualization; Z1 MAY omit its implementation |
| 3 | F | Firmware and PAL execution; owns reset, secure boot and power |

- PAL entry is a controlled hardware transition to F, with enough protected
  per-hart state to identify the cause and resume the interrupted context.
  PALcode handles privileged services, exceptions and interrupt dispatch. PAL
  return restores the interrupted privilege and control state. Entry and return
  MUST preserve architectural state except for the effects and clobbers defined by
  the relevant PAL interface. Privileged PAL calls from U MUST fault; entry
  points and PAL-only controls MUST be inaccessible to U and K directly.
- In the normal EV9 environment, the three hot unprivileged calls `rduniq`,
  `wruniq` and `imb` MUST execute in hardware without entering PALcode. Their externally visible Alpha semantics,
  including instruction-stream synchronization for `imb`, are preserved. The
  UNIQUE value is per execution context and PAL/OS context switching MUST
  preserve it. Other calls, including `callsys`, `bpt` and `gentrap`, enter
  PALcode and are dispatched according to the selected PAL ABI.
- The **21564** implementation MUST make PAL entry cheap by sharing the main
  integer register file between PAL and interrupted code where possible, rather
  than requiring a full bank switch or automatic register spill. PALcode saves
  any registers it uses as required by the ABI; protected entry/return state and
  any scratch registers needed for safe entry remain separate. Sharing MUST NOT
  expose protected state or corrupt interrupted state, including across nested
  exceptions. The precise scratch and nesting convention remains **[OPEN]**.
- In the normal EV9 environment, the hardware walker supports all three
  granules, including the legacy 8 KiB three-level format. PALcode configures
  translation and handles faults; it is not responsible for ordinary TLB refills. V is reserved solely for a future
  virtualization extension, not reused as PAL privilege. Two-stage translation
  and guest PAL behavior require a future virtualization specification.
- Firmware bootstrap PAL is stored in boot flash. OS-specific PAL images MAY
  be supplied on the EFI System Partition (ESP) and installed through the dynamic
  PAL loading contract (section 7.3). Images reside in protected memory while
  active and follow boot-chain authentication and measurement policy. The platform
  defines PAL interfaces; an OS or firmware package supplies a compatible image.
  Full-PAL compatibility mode below defines the legacy compatibility target;
  its exact hardware interface and platform handoff remain **[OPEN]**.
  Compatibility requires the appropriate OS and platform interfaces as well as
  PAL calls; it is not guaranteed merely by installing an image.
- SRM, VAX floating point and legacy chipsets are outside the platform scope.
  Tru64 and OpenVMS would need their own PAL images and are not goals.

### 3.3.1 Full-PAL compatibility mode

Full-PAL compatibility mode is a boot-selected execution environment for existing
Alpha kernels and a complete PAL implementation of the selected legacy ABI. The
normal EV9 PAL environment remains the default. Firmware selects the environment
and PAL image before OS handoff, applies it consistently to all participating
cores, and keeps it fixed for the OS session until reset. Firmware MAY transition from
its bootstrap environment to the selected OS environment once during the
post-ExitBootServices handoff defined in section 7.3. Runtime OS requests to
replace PAL or switch execution profiles are not supported by this contract.

- In this mode PALcode implements the complete selected legacy PAL interface,
  including privileged calls, exception and interrupt entry, return, context
  switching and translation-management behavior. It may use emulation or
  slower compatibility paths for legacy behavior. The required legacy PAL
  execution interface, privileged registers, entry vectors and return semantics
  MUST be specified for the selected PAL image and CPU profile. Support for an
  arbitrary historical PAL binary or processor-specific internal register set
  is not implied; a particular image must be ported or explicitly qualified.
- Translation and PAL calls follow the selected historical CPU and PAL profile,
  including its TLB miss and refill behavior. The EV9 hardware walker MUST be
  disabled for compatibility execution unless the selected original CPU profile
  provided a hardware walker with the required behavior. When that profile uses
  PALcode TLB refill, misses enter PALcode and PALcode controls the refill. The
  normal EV9 4, 8 and 16 KiB walker is not a compatibility-mode requirement;
  legacy page sizes and formats are those of the selected profile.
- The normal EV9 `rduniq`, `wruniq` and `imb` hardware fast paths MUST be
  disabled in this mode unless the selected original CPU profile executed the
  corresponding call in hardware with the required behavior. Otherwise each
  call enters the full PAL implementation. The selected profile MUST specify
  the call dispatch and observable behavior; compatibility cannot rely on a
  fast path merely because its result appears equivalent.
- SMT is disabled: at most one hardware thread per physical core is enabled or
  exposed to the OS. Other physical cores MAY participate in SMP. Firmware MUST
  park sibling threads before activating this mode, exclude them from processor
  discovery and prevent them from being started until reset.
- V remains reserved for virtualization and is unavailable in this mode. Guest
  execution and two-stage translation MUST NOT be exposed or enabled; the PAL
  environment runs without the future virtualization extension. This does not
  repurpose V as PAL privilege.
- Compatibility takes precedence over throughput and may add traps, emulation
  and serialization. The shared-register PAL entry requirement still applies
  to the 21564, but no compatibility-mode performance target is implied.
- The initial target is an OSF PAL profile sufficient for supported unmodified
  Linux/Alpha and NetBSD/Alpha kernels. The profile MUST identify the kernel
  versions, complete required PAL ABI and processor assumptions, and supply a
  matching boot handoff, memory map, interrupt and device interfaces. A PAL ABI
  alone does not provide legacy chipset, SRM, VAX floating-point or device
  compatibility. The profile, image qualification and boot protocol remain
  **[OPEN]**; until implemented and tested, these are targets, not a claim that
  existing kernels already boot on EV9.

### 3.4 MMU

- 48-bit virtual addresses, 48-bit physical addresses (Z1 implements at least 44 physical bits).
- Translation granule configurable per address-space half: 4 KB, 8 KB (Alpha compatibility) or 16 KB (default). Table depth and page sizes follow the granule (Architecture Specification, section 7). 64 KB "contiguous hint" pages MAY be supported.
- **Hardware page-table walker.** In the normal EV9 environment, the walker handles all three granules and the legacy 8 KB translation format. PALcode configures it and handles translation faults. Full-PAL compatibility mode follows the selected historical CPU profile instead (section 3.3.1).
- Address-space IDs (16-bit) and a global bit. Dirty/accessed bits are updated by hardware.
- Per-page attributes: read, write, execute, user, cacheable/write-back, device (strongly ordered), write-combining.
- Two-stage translation is reserved for the hypervisor level.

* **Configurable granule.** In the normal EV9 environment, the walker supports 4 KB, 8 KB and 16 KB granules, chosen separately for each address-space half by a field in its page-table base register. EV9 software uses 16 KB by default, software that assumes 4 KB pages can use 4 KB, and legacy kernels use 8 KB with the original 3-level format (43-bit virtual addresses). TLB entries are tagged with their granule.
* **Why 16 KB is the default.** Fewer TLB misses and shallower tables than 4 KB. The 32 KB L1 caches are 8-way, so each way is 4 KB and a virtually indexed cache never aliases under any granule.
* **Mixed granules inside one address-space half** (for example, sub-page permissions for old binaries) are not supported on Z1. The two halves may use different granules. **\[OPEN\]**

### 3.5 Kotl Service Interface (KSI)

KSI is the EV9 PAL service ABI from K to PALcode in F. It provides a documented interface for EV9-aware software; legacy software uses its selected legacy PAL ABI. Minimum function set:

- Hart control: `HART_START`, `HART_STOP`, `HART_SUSPEND`, `HART_STATUS`.
- System: `SYSTEM_RESET`, `SYSTEM_OFF`.
- Timers and IPI: `SET_TIMER`, `SEND_IPI`, `REMOTE_FENCE` (TLB shootdown assist), `REMOTE_ICACHE_SYNC`.
- Debug console: `CONSOLE_PUTC`, `CONSOLE_GETC` (for early boot only).
- Discovery: `KSI_VERSION`, `KSI_PROBE(function)`.

Calls pass arguments in integer registers following the standard calling convention, with an error code and value returned. Firmware selects and installs the appropriate PAL image and ABI before OS handoff. KSI is not exposed as a requirement to legacy kernels.

### 3.6 Multithreading (SMT)

- Z1 provides 2 harts per core in the normal EV9 environment; full-PAL compatibility mode enables only one (section 3.3.1). Each hart has its own architectural registers, program counter, and trap state. Caches, TLBs and execution units are shared.
- Every hart is a full processor from software's point of view. The OS discovers topology from the devicetree (`cpu-map`), not from CPUID-like instructions.
- Milestone: single-thread cores first, SMT enabled as a later bring-up phase.

## 4. Cache and memory system

| Level | Z1 configuration |
| --- | --- |
| L1 I | 32 KB, 8-way, per core, virtually indexed physically tagged |
| L1 D | 32 KB, 8-way, per core, write-back |
| L2 | 512 KB per core, 8-way, private, inclusive of L1 |
| L3 | 8 MB shared, 16-way, non-inclusive |
| Line size | 64 bytes (architectural). Each L1 way is 4 KB, so the L1 caches never alias at any page granule |

- **Coherence:** MESI (MOESI MAY be used) across all cores and coherent DMA agents.
- **I/O coherence:** PCIe and on-SoC DMA are cache-coherent by default. Non-coherent mode can be selected per-device through IOMMU attributes.
- **Instruction cache:** not coherent with data stores. Software uses an explicit instruction-sync sequence after code modification (and KSI `REMOTE_ICACHE_SYNC` for other harts).

### 4.1 DRAM

- 2 DDR5 channels (each 2 x 32-bit sub-channels), up to DDR5-5600.
- UDIMM or SODIMM, 2 slots per channel maximum; up to 128 GB total.
- **ECC:** side-band ECC is an optional SKU feature. On-die ECC alone is not counted as ECC for the platform's reliability claims.
- Memory training is performed by the F-level firmware before UEFI starts.

## 5. Physical address map

The map below is normative for Z1 and for `kotl-ev9` in QEMU. Firmware MUST describe it in the devicetree and not assume it.

| Range | Size | Region |
| --- | --- | --- |
| `0x0000_0000 – 0x00FF_FFFF` | 16 MB | Boot flash, execute-in-place window |
| `0x0100_0000 – 0x011F_FFFF` | 2 MB | On-chip SRAM (early boot, mailbox) |
| `0x0200_0000 – 0x02FF_FFFF` | 16 MB | System controller: clocks, reset, power, thermal, fuses |
| `0x0300_0000 – 0x03FF_FFFF` | 16 MB | Interrupt controller (KAIA, section 6) |
| `0x0400_0000 – 0x04FF_FFFF` | 16 MB | Timer and IPI block (KTIMER) |
| `0x1000_0000 – 0x1FFF_FFFF` | 256 MB | Low-speed peripherals: UART, I2C, SPI, GPIO, RTC, watchdog (64 KB each) |
| `0x2000_0000 – 0x2FFF_FFFF` | 256 MB | On-SoC controllers: xHCI, SATA/AHCI, GbE MAC, SD/eMMC |
| `0x3000_0000 – 0x3FFF_FFFF` | 256 MB | IOMMU, DMA engine, debug module |
| `0x4000_0000 – 0x4FFF_FFFF` | 256 MB | PCIe ECAM (256 buses) |
| `0x5000_0000 – 0x5FFF_FFFF` | 256 MB | PCIe I/O-space window (memory-mapped) |
| `0x6000_0000 – 0x7FFF_FFFF` | 512 MB | Reserved |
| `0x8000_0000 – 0xFFFF_FFFF` | 2 GB | PCIe 32-bit MMIO window |
| `0x1_0000_0000 – 0xFF_FFFF_FFFF` | up to \~1 TB | DRAM |
| `0x100_0000_0000 – 0x1FF_FFFF_FFFF` | 1 TB | PCIe 64-bit MMIO window |

DRAM starts at 4 GB so that the entire low 4 GB is available for devices and 32-bit-BAR PCIe hardware, with no DRAM hole to manage.

## 6. Interrupts and timers

### 6.1 Kotl Advanced Interrupt Architecture (KAIA)

A message-signalled design in the spirit of RISC-V AIA and ARM GICv3.

- **Wired interrupt controller:** up to 1023 wired sources, programmable priority, level or edge, routed as messages.
- **Per-hart interrupt files:** each hart has its own memory-mapped interrupt file that receives MSIs. This scales to many harts without a shared bottleneck.
- **PCIe:** MSI and MSI-X MUST be supported. Legacy INTx is mapped to wired sources through the devicetree `interrupt-map`.
- **IPIs:** delivered as writes to a target hart's interrupt file (or through KSI `SEND_IPI`).
- **Virtualization:** guest interrupt files are reserved for the hypervisor extension.

### 6.2 Timers

- **KTIMER:** a global 64-bit counter at a fixed, discoverable frequency (default 24 MHz) with per-hart compare registers and interrupts. It is the system time source.
- Per-hart cycle and instruction-retired counters, with privilege-controlled access from user mode.
- **RTC:** battery-backed real-time clock on I2C, exposed to UEFI runtime services `GetTime`/`SetTime`.

## 7. Boot flow and firmware

1. **Reset (F level).** Hart 0 starts at the boot flash window; other harts wait in a low-power state. Boot ROM in the SoC verifies the first-stage firmware against a hardware root of trust.
2. **Stage 1 (F level).** Trains DRAM, initialises PCIe root complexes, sets up the IOMMU in default-deny mode, and installs the protected firmware bootstrap PAL and its services.
3. **UEFI (EDK II, K level).** Provides the standard boot environment and loads the OS loader from an EFI System Partition. Before handoff, firmware selects the PAL environment and image, and applies the compatibility-mode CPU and virtualization restrictions when selected.
4. **PAL handoff.** If the boot entry requests an OS PAL image, the loader stages it through firmware before ExitBootServices. After successful ExitBootServices, firmware commits the PAL/profile transition and enters the loader continuation (section 7.3).
5. **OS.** Receives the devicetree (and ACPI tables if present) via the EFI configuration table and the selected PAL handoff description.

### 7.1 UEFI requirements

- UEFI 2.10 or later, built from EDK II with a new `KotlPkg` architecture package.
- **PE/COFF machine type:** `0x0284` (`IMAGE_FILE_MACHINE_ALPHA64`, as used by Windows NT on Alpha) is the proposed value. A formal registration with the UEFI Forum is needed so loaders and tools agree. **\[OPEN\]**
- Mandatory protocols and drivers: Block I/O, Simple File System, Network (SNP/MNP/TCP/UDP/HTTP boot), Graphics Output (GOP), Console, USB (xHCI, HID, mass storage), NVMe pass-through, PCIe root bridge I/O, RNG, and Runtime Services including `GetTime`, variable services and `ResetSystem`.
- **Secure Boot:** db/dbx/KEK/PK variables, signature verification of all loaded images.
- **Measured boot:** TCG TPM 2.0 event log (EFI TCG2 protocol).
- **Capsule updates** for firmware, with rollback protection.
- Boot flash holds A/B firmware slots so a failed update never bricks the board. UEFI itself uses 4 KB EFI pages and the 4 KB granule, and every region marked for runtime services is aligned and sized in 16 KB multiples, so an OS using any granule can map it.

### 7.2 Firmware description

- **Devicetree (mandatory):** the primary description. Bindings are published with the spec.
- **ACPI 6.5 (optional, Tier 2):** MADT-equivalent, MCFG, DSDT for power and thermal. Required only if the target OS needs it.
- **SMBIOS 3.x:** mandatory, for inventory and `dmidecode`-style tools.


### 7.3 Dynamic OS PAL loading and UEFI handoff

UEFI remains the default boot firmware. Its bootstrap PAL supports firmware
execution and drivers without embedding the privileged OS implementation for
every operating system. An OS boot entry MAY select a separate PAL image on the
ESP, supplied by the OS, its bootloader, or a firmware package. Loading an image
does not imply compatibility with arbitrary historical processor internals.

**Image and preparation contract.** Firmware MUST expose discovery of supported
PAL loading versions, target CPU profiles and runtime-service compatibility.
The loader MUST explicitly identify its PAL image; firmware MUST NOT infer one
by scanning for an arbitrary filename. A versioned image header or authenticated
manifest MUST describe the PAL ABI and version, target processor/profile,
required hardware features, image size, entry offsets, alignment, protected
memory and per-hart state requirements, relocation rules and integrity metadata.
Firmware MUST reject malformed, incompatible or overlapping images and entries
outside the loaded executable image. A checksum alone is not authentication.
PAL image authentication MUST follow the platform Secure Boot policy, and its
identity and target profile MUST be included in measured boot when enabled.

Firmware MUST copy validated image contents and metadata into firmware-owned
staging storage, reserve all resident and transition memory in the EFI memory
map, and protect the committed image from OS and device writes. Images and PAL
state MUST remain resident throughout their use. Validation MUST apply to the
exact bytes eventually executed. Firmware MUST return preparation failures
before ExitBootServices while the loader can still report them or choose another
boot entry. Preparation MUST NOT change the active firmware PAL or CPU profile.
PAL activation MUST NOT be implicit in ExitBootServices for an unprepared loader.

**Commit and CPU transition.** A prepared loader obtains a firmware-controlled
handoff entry and descriptor before its final GetMemoryMap/ExitBootServices
sequence. Only after ExitBootServices succeeds may it invoke that entry. The
handoff is a one-way transfer to an explicitly supplied loader continuation;
it MUST NOT return through the old loader stack or call Boot Services. Firmware
MUST establish execution, stack and descriptor storage that remain valid while
PAL state, privilege controls and translation are replaced. This transition
mechanism is a required EV9 hardware/firmware contract, not an assumed existing
instruction or register encoding.

Firmware MUST quiesce other harts and relevant DMA, mask interrupts, preserve
handoff inputs, synchronize instruction visibility, invalidate incompatible
TLB/walker state, and install the target PAL vectors, per-hart state and CPU
profile. No hart may execute OS code with a partially installed or mismatched
PAL environment. Secondary processors remain parked until initialized through
the selected PAL's startup interface. Full-PAL targets MUST disable SMT siblings,
virtualization and the normal EV9 walker/fast calls as required by section 3.3.1.
Normal EV9 targets retain the hardware requirements of section 3.3. The descriptor
MUST identify the committed image/ABI/profile, resident memory, enabled processor
set, continuation entry and its address/translation state, initial register
contract and applicable platform description. The exact descriptor layout and
transition transport remain **[OPEN]**. A commit failure MUST fail closed with a
reset or diagnostic stop; it MUST NOT resume the old UEFI environment after a
partial transition. Reset restores the firmware bootstrap environment.

**UEFI runtime services.** A PAL replacement MUST NOT invalidate advertised EFI
runtime services. Each supported target MUST specify how runtime entry, mappings,
privilege and firmware PAL dependencies are preserved or isolated, including
SetVirtualAddressMap behavior and serialization across processors. Runtime calls
MUST NOT globally replace the OS PAL or expose an OS-controlled firmware PAL
entry. Firmware MUST reject preparation for targets whose runtime requirements
it cannot satisfy; silently breaking advertised services is forbidden. The
runtime bridge implementation and ABI remain **[OPEN]**.

**Implementation and validation.** The present experimental QEMU boot-fixed
pal-environment property does not implement this transition. QEMU and physical
EV9 implementations MUST supply the specified transition mechanism before
claiming dynamic PAL support. Validation MUST cover successful ESP loading and
handoff, incompatible/corrupt image rejection, failed ExitBootServices retries,
secondary startup, protected memory, profile-specific translation and PAL calls,
and EFI runtime calls after the switch. Booting with an ESP file alone is not
proof of a working PAL handoff or Linux compatibility.

## 8. PCI Express

### 8.1 Root complexes

- Spec revision: **PCIe Gen 4** on Z1 (16 GT/s), architecture ready for Gen 5.
- **Z1 lane budget (20 lanes):**
  - 1 x 16 lanes for the graphics/accelerator slot, bifurcatable as x8/x8 or x8/x4/x4.
  - 1 x 4 lanes dedicated to an M.2 NVMe slot (CPU-attached, not through a chipset).
- Additional lanes (x1/x2 Gen 3) MAY come from a platform controller hub on a board.
- **Configuration:** ECAM, 256 buses per segment, as in section 5. Enhanced allocation (EA) is optional.
- **Address translation:** all inbound DMA goes through the IOMMU (section 11). Outbound windows are fixed by the memory map.
- **Interrupts:** MSI/MSI-X, mandatory. Legacy INTx via `interrupt-map`.
- **Error reporting:** Advanced Error Reporting (AER), Downstream Port Containment (DPC) on Z1 and later.
- **Power management:** ASPM L0s/L1 and L1 substates, D-states, PME. Link power states MUST be controllable by the OS.
- **Hot-plug:** native PCIe hot-plug (surprise removal) for the M.2 and slot ports is a Z1 SHOULD, a Z2 MUST.
- **Peer-to-peer DMA:** permitted between devices behind the same root port. Cross-port P2P is controlled by IOMMU/ACS policy.
- **ATS / PRI / PASID:** reserved for the IOMMU 2.0 generation, not required in Z1.
- **Resizable BAR:** MUST be supported so that GPUs can map their full VRAM.
- **Cache coherency:** PCIe No-Snoop is honoured. Default is coherent.

### 8.2 Compatibility notes

Kotl EV9 supports PCIe devices that use standard architectural driver models. Option ROMs for other architectures (x86 BIOS, UEFI EBC) are ignored. GPUs need either a native driver in firmware for the display path or must be driven by the OS after boot.

## 9. Storage

### 9.1 NVMe

NVMe is the primary boot and data storage.

- **Interface:** NVMe 2.0 or later, over PCIe, controllers as ordinary PCIe endpoints. There is no NVMe controller on the SoC; the SoC provides the root port, IOMMU and interrupts.
- **M.2:** M-key 2280 slot, x4 PCIe Gen 4, with 3.3 V power and a sideband `PERST#`, `CLKREQ#`, `PEWAKE#`.
- **Platform requirements for NVMe devices:**
  - 64-bit BARs are mapped above 4 GB when needed (section 5).
  - MSI-X with at least 1 vector per hart.
  - PRP and SGL descriptors both supported by the platform's IOMMU path.
  - Controller Memory Buffer (CMB) and Host Memory Buffer (HMB) are supported but optional.
- **Boot:** UEFI's NVMe driver (NvmExpressDxe) reads the ESP. `kotl-ev9` in QEMU uses `-device nvme` to prove the same path.
- **Reliability:** SMART/health data is exposed via NVMe admin commands. Secure erase and sanitize supported by the driver stack.
- **Encryption:** TCG Opal, and inline encryption by the SoC are reserved (section 14).

### 9.2 SATA / AHCI

- Optional on-SoC AHCI 1.3.1 controller, 2 ports (SATA 6 Gb/s), to cover legacy disks and optical drives.
- Each port supports hot-plug, NCQ and port multipliers MAY be unsupported.

### 9.3 SD / eMMC

- 1 SD host controller (SDHCI-compatible, UHS-I) for removable boot media and 1 eMMC 5.1 controller for embedded boards.

### 9.4 Boot flash

- SPI NOR (quad SPI, 128 Mb minimum, 256 Mb recommended) holds firmware, the UEFI variable store, and the A/B slots. An execute-in-place window is mapped as in section 5.

## 10. USB

- **Host controller:** xHCI 1.2 on-SoC, standard register layout so the stock `xhci` driver works on every OS and on EDK II (`XhciDxe`).
- **Ports (Z1):** 2 x USB 3.2 Gen 2 (10 Gb/s) and 4 x USB 2.0 on the SoC; boards add more through hubs.
- **Type-C:** Power Delivery and alternate-mode negotiation use an external PD controller over I2C. The SoC provides orientation and role-swap signals. USB4/Thunderbolt is out of scope for Z1 and left to PCIe-attached controllers.
- **Firmware:** UEFI MUST enumerate USB keyboards (HID boot protocol), mass storage, and USB Ethernet adapters (CDC-ECM/NCM) at minimum.
- **DMA:** all xHCI DMA goes through the IOMMU.
- **Device mode:** a USB 2.0 device controller (for debug and firmware recovery) is a SHOULD.
- **Power:** per-port overcurrent detection, and USB suspend/resume with wake-up.

## 11. IOMMU

- Every DMA-capable agent (PCIe, xHCI, SATA, GbE, SD, DMA engine) is identified by a **device ID** (PCIe requester ID for PCIe).
- **Default-deny:** at reset, no device can DMA. Firmware and the OS open mappings explicitly.
- Multi-level I/O page tables using the same format as the CPU MMU, with a granule of 4 KB, 8 KB or 16 KB chosen per domain.
- Faults are reported through a queue and an interrupt, and never silently dropped.
- Domain/device assignment is software-controlled, supporting safe device passthrough in virtual machines.
- Invalidation queue for cache and IOTLB shootdowns.
- An interrupt remapping table MUST be present, to isolate MSIs between domains.

## 12. Networking, graphics, and other I/O

### 12.1 Networking

- On-SoC 2.5 GbE MAC with RGMII/SGMII PHY interface, descriptor-ring DMA, checksum offload, RSS, and WoL. Alternatively, boards use any PCIe NIC.
- UEFI network stack with PXE and HTTP boot.

### 12.2 Graphics and display

- Primary: PCIe GPU with a standard driver model. UEFI uses the GPU's own GOP when available, and otherwise a simple linear framebuffer described in the devicetree (`simple-framebuffer`).
- A basic on-SoC display controller (single output, DisplayPort/HDMI via external PHY) is optional on Z1.
- No VGA text mode, VGA legacy ranges, or VESA BIOS calls.

### 12.3 Audio

- Delivered via USB Audio Class and PCIe HD Audio controllers. An on-SoC I2S block for embedded boards is optional.

### 12.4 Low-speed I/O

| Block | Count | Notes |
| --- | --- | --- |
| UART | 4 | 16550-compatible, one for firmware/console, 115200 8N1 default |
| I2C | 4 | 400 kHz fast mode, 1 MHz fast-mode plus |
| SPI | 2 | Plus the dedicated flash controller |
| GPIO | 64 | Interrupt-capable, pin muxing via system controller |
| RTC | 1 | Battery-backed, I2C-attached or on-chip |
| Watchdog | 1 per core | Reset and NMI-style recovery |
| PWM/fan | 2 | Tachometer inputs for fan control |

## 13. Power, clocks, and thermal

- **Operating points:** per-core DVFS with a small set of voltage/frequency pairs, controlled through the system controller and a devicetree `operating-points` table.
- **Idle states:** `WFI` (wait for interrupt) at minimum. Deeper per-core and per-cluster states are exposed through KSI `HART_SUSPEND`.
- **Sleep:** S3-like suspend-to-RAM (DRAM self-refresh) is a Z1 SHOULD; S4/hibernation is handled entirely by the OS.
- **Thermal:** on-die sensors, programmable trip points, hardware throttling, and a critical trip that triggers `SYSTEM_OFF`.
- **Board power:** standard ATX or barrel-jack input; the SoC supplies power-good/reset sequencing signals.

## 14. Security

- **Root of trust:** immutable boot ROM verifies first-stage firmware with a public key hash held in one-time-programmable fuses.
- **Secure/measured boot:** see 7.1. TPM 2.0 is attached over SPI or I2C (discrete TPM), with a firmware TPM as an option.
- **Hardware RNG:** a true random number generator (TRNG) with health tests, exposed as a memory-mapped device and as the UEFI RNG protocol.
- **Crypto acceleration:** AES, SHA-2/SHA-3 and carry-less multiply instructions are reserved for an optional extension. Software fallbacks MUST work.
- **Memory protection:** per-page execute-never, supervisor-mode access prevention (SMAP-like), and supervisor-mode execution prevention (SMEP-like) are mandatory in the MMU.
- **Speculation:** Z1 is an in-order design, but the speculation barrier instruction and a documented speculation contract MUST exist so that later out-of-order parts can be mitigated in software.
- **Memory encryption:** optional on later SKUs, reserved.
- **IOMMU:** the DMA isolation layer, see section 11.

## 15. Debug, trace and test

- **JTAG** (IEEE 1149.1) with a debug module that supports halt, resume, single-step, and register and memory access, per hart.
- **Hardware breakpoints/watchpoints:** at least 4 per hart.
- **Performance counters:** at least 4 programmable counters per hart plus cycle and instruction counts.
- **Trace (optional):** instruction trace over a dedicated trace port or on-chip buffer.
- **Co-simulation:** RTL simulation under Verilator MUST compare retired-instruction traces with `kotl-ev9` in QEMU. This is the main correctness gate.
- **Conformance suite:** a public set of architectural tests (ISA, memory-model litmus tests, MMU, interrupts, PCIe, NVMe boot) that every implementation must pass.

## 16. Reference platforms and software

### 16.1 Reference implementations

| Name | Description |
| --- | --- |
| `kotl-ev9` (QEMU) | Reference machine: the memory map from section 5, virtio devices plus emulated xHCI and NVMe on a PCIe root complex |
| Z1 FPGA board | First hardware: a large FPGA board for the SoC prototype, DDR4 or DDR5, one PCIe slot, one M.2, USB through a bridge |
| Z1 silicon | Target for shuttle/MPW tapeout, only once the FPGA board boots Linux |

### 16.2 QEMU machine notes

- PCIe root complex with ECAM at the address in section 5; virtio-pci devices are acceptable for disk, net, GPU, and RNG.
- `-device nvme`, `-device qemu-xhci`, and `-device usb-kbd` / `usb-storage` model the same devices real hardware will use.
- Devicetree is generated by the machine at startup and passed to firmware.
- No dependency on legacy Alpha chipset devices.

### 16.3 Operating systems

| OS | Plan |
| --- | --- |
| Linux | Primary. Rework the existing `alpha` port in two steps: first boot supported unmodified kernels using full-PAL compatibility mode and its specified legacy handoff, then add EV9 support that adopts the address-dependency-ordering guarantee and adds KSI and KAIA drivers. Page size is a kernel build option: 4 KB, 8 KB (legacy) or 16 KB (default) |
| NetBSD | Secondary, builds on its existing Alpha port |
| Others | Out of scope until the platform is stable |

### 16.4 Toolchain

- GCC and binutils: Alpha support exists upstream. EV9 needs new ISA extension patches (AMO, speculation barrier, cache ops) and a UEFI PE/COFF target.
- LLVM: no maintained Alpha backend, a new backend is a longer-term option.
- Standard C library: glibc has an Alpha port that needs updating for the EV9 ABI. musl is a cheaper second target.
- ABI: the standard Alpha calling convention, preserved so existing toolchains keep working.

## 17. Open decisions

1. **PAL interfaces and entry.** PALcode is the privileged software layer; the normal EV9 walker handles all three granules, while full-PAL compatibility follows the selected historical CPU profile. The EV9 ABI, the full OSF compatibility profile and PAL execution interface, supported kernel versions, PAL entry scratch/nesting convention, and legacy OS handoff remain to be specified and validated (sections 3.3–3.3.1).
2. **PE/COFF machine type.** Use `0x0284` or register a new one. **\[OPEN\]**
3. **Vector extension.** Whether Z1 ships any SIMD beyond MVI.
4. **Cache hierarchy.** Private L2 plus shared L3 here, versus the earlier shared 1 MB L2. Depends on FPGA resource budget.
5. **ACPI tier.** Whether ACPI ships at all, or whether the platform is devicetree-only.
6. **PCIe Gen.** Gen 4 on FPGA is hard (transceivers); Gen 3 may be the real FPGA target.
7. **Microarchitecture.** Z1 in-order dual-issue is the baseline. Whether Z2 moves to out-of-order.
8. **DDR5 vs DDR4 for the FPGA prototype.**
9. **Hypervisor extension timing.** Reserved now, specified when needed.
10. **Licensing of the spec.** Open (CC BY, with a patent grant) is recommended if the aim is adoption.
11. **Dynamic PAL loading ABI.** Image format, discovery/staging API, transition descriptor and hardware transport, protected-memory enforcement and runtime-service bridge require precise bindings and validation (section 7.3).

## 18. Suggested milestones

1. `kotl-ev9` boots a bare-metal test with the section 5 memory map.
2. KAIA, KTIMER and PCIe ECAM modelled; Linux boots to a shell via UEFI on QEMU.
3. NVMe and xHCI boot paths verified on QEMU.
4. Freeze spec v0.5 (platform), then v1.0 once FPGA boots.
5. Single-core RTL, co-simulated against QEMU.
6. Four cores, then SMT.
7. FPGA board bring-up with real PCIe, NVMe, USB.
8. Decision on tapeout.
