# Kotl EV9 Platform Specification

Draft 0.1 · 24 November 2026 · Status: working draft, nothing here is frozen

Consistency revision · 28 November 2026: separate pre-exit PAL selection from
post-exit transfer, require legacy hardware walking on current parts, distinguish
EFI allocation units from translation granules, and define canonical PAL hashing.

## 1. Scope and conventions

This document defines the **Kotl EV9 platform**: the architecture baseline, memory map, interrupt system, boot flow, and peripheral requirements that every EV9-class chip, board, and emulator must follow so that one OS image and one firmware image can run on all of them.

- **Kotl EV9** is the architecture and platform generation. It continues the Alpha AXP line past the cancelled EV8.
- **Kotl Z1** is the middle-range part, built from the base 21564 (section 1.1).
- **kotl-ev9** is the QEMU machine type, the golden reference model. When this document and QEMU disagree, that is a bug to be resolved explicitly, not silently.
- Keywords MUST, SHOULD and MAY follow RFC 2119. Numbers in hex are written `0x...`. "Hart" means one hardware thread.
- Items marked **\[OPEN\]** are undecided and collected in section 17.

Values in this document are those of the 21564, the base part, unless another part is named. Section 1.1 lists how Z1 and Z1-lite differ.

### 1.1 Product family

The platform covers one base processor and the parts built from it.

| Part | Role | Cores | Core | Socket | Scope |
| --- | --- | --- | --- | --- | --- |
| Z1-lite | Test only: FPGA bring-up of firmware, OS and tools before silicon. Never a product | 1 to 4 | In-order, 2-wide | None (FPGA) | In scope |
| 21564 | Base processor. Every other part is built from it | 4 | Out-of-order, 4-wide | Socket 21580 | In scope |
| Z1 | Middle range: the 21564 core with more cores and a larger uncore | 8 to 16 **\[TENTATIVE\]** | Same as the 21564 | Socket 21580 | In scope |
| Desktop and workstation parts above Z1 | More cores on Socket 21580 | Up to 32 | Same as the 21564 | Socket 21580 | Out of scope for now |
| Server parts | Parts for Socket 21581 | Up to 128 | Same as the 21564 | Socket 21581 | Out of scope for now |

Rules for the family:

- The 21564 core, its private L2 and one L3 slice form a **tile**. Larger parts add tiles, memory channels and PCIe lanes. They do not change the core microarchitecture (21564 document, section 2.13).
- Every part implements the same EV9 architecture and the same platform. Software tells them apart from the devicetree and the FPARTID register (Z1-lite = 1, 21564 = 2, Z1 = 3).
- A function that sits on the SoC in one part and in the chipset in another (USB, SATA, Ethernet, display, low-speed I/O) is found through the devicetree. The on-SoC register ranges in section 5 are used only when the function is on the SoC.
- Z1-lite exists to test. It need not meet any performance, power or physical target, and it may include test-only features such as 2 harts per core.

### 1.2 Sockets

Two LGA sockets are defined. Each is a superset: a part uses the pins it needs and leaves the rest unconnected. Everything in the table is a target. **\[TENTATIVE\]**

|  | Socket 21580 | Socket 21581 |
| --- | --- | --- |
| Market | Desktop and workstation | Server |
| Cores | Up to 32 | Up to 128 |
| Memory | Up to 4 DDR5 channels (each 2 x 32 bit) | Up to 12 DDR5 channels |
| PCIe | Up to 64 Gen 5 lanes plus a Gen 4 x4 chipset link | Up to 128 Gen 5 lanes plus the chipset link |
| Multi-socket | One socket per system | Up to 2 sockets with coherent links between them **\[OPEN\]** |
| Power envelope | Up to 250 W | Up to 500 W |
| Parts today | 21564, Z1 | None (out of scope for now) |

- The 21564 uses 2 memory channels and 24 PCIe lanes in total: 20 Gen 5 device lanes plus the Gen 4 x4 chipset link. Z1 targets 4 channels and 48 Gen 5 device lanes plus its Gen 4 x4 chipset link (52 lanes total). Both fit Socket 21580's device-lane and separate chipset-link budgets. **\[TENTATIVE\]**
- The chipset link, boot flash, debug (JTAG), clock, reset and power-management signal groups are defined identically on both sockets.
- The sockets are keyed differently, so a part cannot be installed in the wrong one.
- Contact counts, the pin-out, the package and the thermal and power-delivery requirements are **\[OPEN\]** until the first part is floorplanned.

## 2. Platform overview

```
  +---------------------------------------------------------------+
  |                         Kotl EV9 SoC                           |
  |  +--------+ +--------+ +--------+ +--------+                  |
  |  | Core 0 | | Core 1 | | Core 2 | | Core 3 |   1 hart each    |
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
2. **Firmware you can replace.** UEFI at the bottom, PAL images that the OS can replace above it, and a thin service layer (KSI) between them. PALcode is the privileged layer of the platform.
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
- **Vector/SIMD:** reserved encoding space. Implementations MAY omit it (none implements it yet). Software MUST NOT assume it exists without feature discovery.
- **FP:** IEEE 754-2008, with precise exceptions (no imprecise-trap quirks of early Alpha). VAX FP formats are **not** supported.

### 3.2 Memory ordering

EV9 is a **weakly ordered** architecture, formally specified (an axiomatic model published as an appendix, in the style of RVWMO).

- **Address-dependency ordering is guaranteed in hardware.** This removes the one property that made original Alpha unique among architectures and forced `read_barrier_depends` into Linux. Software MUST still use explicit barriers for all other orderings.
- Barriers: `MB` (full), `WMB` (store-store), and a new `RMB` (load-load). Acquire/release variants are encoded in the AMO instructions.
- Atomicity: naturally aligned accesses up to 64 bits are single-copy atomic. Misaligned accesses within a cache line MUST work, and MAY be slow.

### 3.3 Privilege model

EV9 has two privileged models, chosen once per boot: PAL mode (a PAL image chosen by the OS runs at F level and owns the privileged layer) and full-PAL mode (the same, with classic Alpha behavior and virtualization off). There is no hardware-only native model. Firmware itself (stage 1 and UEFI) runs at F level in firmware mode (FMODEL = 0) before an OS model is selected.

| Level | Name | Purpose |
| --- | --- | --- |
| 0 | U | User |
| 1 | K | Kernel / supervisor |
| 2 | V | Hypervisor (encoding reserved; not yet implemented) |
| 3 | F | PAL image and firmware (the most privileged level; owns reset, traps, interrupts, secure boot, power) |

- **PAL images own the privileged layer.** Every exception, interrupt, reserved opcode and CALL\_PAL other than enabled IMB/RDUNIQUE/WRUNIQUE fast-path calls enters a PAL image at F level through the vector table (Architecture Specification, 6.8). The OS runs at K and U and sees only the interface its image defines. The OS chooses the image, as described in section 7.3.
- Privileged registers (status, ASN, page-table roots, TLB control, counters) exist in hardware, but only F level reaches them, with instructions in the Alpha PAL-reserved opcode space (PR\_READ, PR\_WRITE, XRET, PLD, PST).
- Firmware services (hart start and stop, reset, timers, remote fences, console, UEFI runtime services) are reached through KSI. PAL images call KSI directly and give the OS whatever PAL calls it needs for them.
- Alpha-style kernels (Linux/Alpha, NetBSD/Alpha) are supported by an OSF/1 PAL image. New operating systems can use the Kotl reference PAL (personality 0).

* **One model per boot.** MODEL\_SELECT chooses and locks the pending model and verified PAL image before ExitBootServices (the setup switch picks full-PAL mode for SRM boots). Selection does not change FMODEL: UEFI and its loader continue at F in firmware mode. A retained handoff entry commits the selection after successful ExitBootServices, as specified in section 7.3. All released harts use the same active model; it changes only at reset.
* **Hardware available in both models.** The PAL image runs at F level on top of the privileged registers, so the hardware page-table walker, KAIA interrupts, IOMMU, AMO instructions and ordering guarantees are available to it. Full-PAL mode hides the EV9 extensions from the OS (section 3.3.1).
* **PALcode image.** The image is not fixed: it can be a firmware-resident image, an image supplied by the OS, or one the user installed. It is selected, verified, measured and loaded as described in section 7.3. UEFI itself runs at F level in firmware mode (FMODEL = 0), before the model is selected.
* **Unprivileged PAL calls** (callsys, rduniq, wruniq, imb, bpt and gentrap) are handled by the image. The hardware fast path (section 3.3.1) executes IMB, RDUNIQUE and WRUNIQUE without entering PAL.
* **Translation.** The image configures it: native 4, 8 or 16 KB formats, or the legacy 8 KB format, in PAL mode; the legacy 8 KB format only in full-PAL mode. Z1-lite, the 21564, Z1 and the QEMU reference MUST implement the legacy hardware walker. PAL MAY disable it and use software refill only when FCAP reports FTLBW support. **\[FIXED\]**
* **Not covered:** the SRM console, VAX floating point, and the legacy chipsets. Tru64 and OpenVMS would also need their own PAL images, so they are not goals.

#### 3.3.1 Firmware mode, the two OS models and the SRM switch

|  | PAL mode | Full-PAL mode |
| --- | --- | --- |
| FMODEL | 1 | 2 |
| Who handles traps, interrupts and CALL\_PAL | PAL image at F level | PAL image at F level |
| What the OS sees | The image's PAL calls | The image's PAL calls |
| EV9 instructions at K/U | Visible | Hidden (reserved opcodes); still available at F |
| IMPLVER at K/U | 9 | 3 (EV7); F-level discovery still reports 9 |
| Translation granules | 4, 8 and 16 KB, chosen by the image | 8 KB, legacy format |
| SMT | Depends on the part | Off |
| Virtualization | Reserved for a future part, usable by the image | Disabled |
| Typical use | New operating systems (Kotl reference PAL) and Alpha-style systems that can use EV9 features | Unmodified classic Alpha operating systems, SRM boots |

- **Setup switch.** Firmware setup has a boot-mode setting: UEFI (PAL mode, with the image chosen by the OS loader) or SRM (full-PAL mode with the SRM console). Stage 1 reads it before it starts any hart beyond the first, because full-PAL mode fixes the hart count and the visible features.
- **OpenSRM comes later.** Until it exists, full-PAL mode is exercised with a test loader (a small UEFI application that builds a minimal HWRPB, stages a PAL image and calls MODEL\_SELECT with full-pal) and with the QEMU option model=full-pal.
- **Hardware kept in every model:** the page-table walker (including the legacy 8 KB format), the configurable hardware fast path for IMB, RDUNIQUE and WRUNIQUE, and cheap PAL entry. EV9 instruction hiding in full-PAL mode applies to K and U, not to F-level PAL and firmware. The V-level encoding remains reserved and is not implemented on any current part; full-PAL mode also forbids its use on future parts. PAL entry shares the hart's register files and targets ordinary-trap cost; actual latency is implementation-dependent.
- The hardware rules for full-PAL mode are in the Architecture Specification, section 6.9.

### 3.4 MMU

- 48-bit virtual addresses, 48-bit physical addresses (every part implements at least 44 physical bits).
- Translation granule configurable per address-space half: 4 KB, 8 KB (Alpha compatibility) or 16 KB (default). Table depth and page sizes follow the granule (Architecture Specification, section 7). Contiguous-hint groups (a naturally aligned group of 16 entries, flagged by the C bit) MAY be supported.
- **Hardware page-table walker.** Original Alpha used software TLB fill in PALcode, which the hardware walker makes optional. Full-PAL mode uses the legacy translation format (section 3.3.1).
- Address-space IDs (16-bit) and a global bit. Dirty/accessed bits are updated by hardware.
- Per-page attributes: read, write, execute, user, cacheable/write-back, device (strongly ordered), write-combining.
- Two-stage translation is reserved for the hypervisor level.

* **Configurable granule.** Native formats use 4 levels at 4 KB, 4 levels at 8 KB and 3 levels at 16 KB, with separate roots for the two address-space halves. LEGACY selects a distinct 3-level, 43-bit-VA, 8 KB Alpha-compatible format. The PAL image chooses 16 KB by default for new software, 4 KB for software that assumes 4 KB pages, and legacy 8 KB for existing Alpha kernels and full-PAL mode. TLB entries record the granule and format.
* **Why 16 KB is the default.** Fewer TLB misses and shallower tables than 4 KB. The 32 KB L1 caches are 8-way, so each way is 4 KB and a virtually indexed cache never aliases under any granule.
* **Mixed granules inside one address-space half** (for example, sub-page permissions for old binaries) are not supported on any current part. The two halves may use different granules. **\[OPEN\]**

### 3.5 Kotl Service Interface (KSI)

A small call interface between PAL images and the firmware, covering hart control, reset, timers, console and PAL image management. The OS reaches it through the PAL calls its image provides. Minimum function set:

- Hart control: `HART_START`, `HART_STOP`, `HART_SUSPEND`, `HART_STATUS`.
- System: `SYSTEM_RESET`, `SYSTEM_OFF`.
- Timers and IPI: `SET_TIMER`, `SEND_IPI`, `REMOTE_FENCE` (TLB shootdown assist), `REMOTE_ICACHE_SYNC`.
- Debug console: `CONSOLE_PUTC`, `CONSOLE_GETC` (for early boot only).
- Discovery: `KSI_VERSION`, `KSI_PROBE(function)`.

Calls pass arguments in integer registers following the standard calling convention, with an error code and value returned. MODEL\_SELECT(pal or full-pal, image ID), called before ExitBootServices on a UEFI boot, locks a pending model and image without entering it (sections 3.3 and 7.3). The PAL service group (PAL\_QUERY, PAL\_STAGE, PAL\_INSTALL, PAL\_CONFIRM) manages PAL images. KSI is used by PAL images and by firmware at F level. Selection and successful ExitBootServices are prerequisites for the separate, one-shot UEFI handoff entry; that entry is not a post-exit Boot Services protocol call. A standalone SRM boot without Boot Services follows the ownership-release equivalent in section 7.3.

### 3.6 Multithreading (SMT)

- The architecture allows 2 harts per core. Z1-lite implements both to test the SMT rules, and the 21564 and Z1 ship with one hart per core (SMT reserved). Each hart has its own architectural registers, program counter, and trap state. Caches, TLBs and execution units are shared.
- Every hart is a full processor from software's point of view. The OS discovers topology from the devicetree (`cpu-map`), not from CPUID-like instructions.
- Milestone: single-thread cores first, SMT enabled as a later bring-up phase.

## 4. Cache and memory system

| Level | 21564 configuration |
| --- | --- |
| L1 I | 32 KB, 8-way, per core, virtually indexed physically tagged |
| L1 D | 32 KB, 8-way, per core, write-back |
| L2 | 512 KB per core, 8-way, private, inclusive of L1 |
| L3 | 16 MB shared, 16-way, non-inclusive (larger parts scale with core count) |
| Line size | 64 bytes (architectural). Each L1 way is 4 KB, so the L1 caches never alias at any page granule |

- **Coherence:** MESI (MOESI MAY be used) across all cores and coherent DMA agents.
- **I/O coherence:** PCIe and on-SoC DMA are cache-coherent by default. Non-coherent mode can be selected per-device through IOMMU attributes.
- **Instruction cache:** not coherent with data stores. Software uses an explicit instruction-sync sequence after code modification (and KSI `REMOTE_ICACHE_SYNC` for other harts).

### 4.1 DRAM

- 21564: 2 DDR5 channels (each 2 x 32-bit sub-channels), up to DDR5-5600. Z1 and the sockets allow more channels (section 1.2).
- UDIMM or SODIMM, 2 slots per channel maximum; up to 128 GB total.
- **ECC:** side-band ECC is an optional SKU feature. On-die ECC alone is not counted as ECC for the platform's reliability claims.
- Memory training is performed by the F-level firmware before UEFI starts.

## 5. Physical address map

The map below is normative for every EV9 part and for `kotl-ev9` in QEMU. Firmware MUST describe it in the devicetree and not assume it.

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

DRAM starts at 4 GB so that the entire low 4 GB is available for devices and 32-bit-BAR PCIe hardware, with no DRAM hole to manage. The 16 MB boot-flash range is an execute-in-place window, not the total backing flash capacity. Firmware accesses other flash banks/offsets through the flash controller; changing the visible window must not invalidate executing firmware.

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
2. **Stage 1 (F level).** Trains DRAM, initialises PCIe root complexes, sets up the IOMMU in default-deny mode, installs KSI.
3. **UEFI (EDK II, F level, translation off).** Provides the standard boot environment. Loads the OS loader from an EFI System Partition.
4. **OS.** Receives the devicetree (and ACPI tables if present) via the EFI configuration table.

### 7.1 UEFI requirements

- UEFI 2.10 or later, built from EDK II with a new `KotlPkg` architecture package.
- **PE/COFF machine type:** `0x0284` (`IMAGE_FILE_MACHINE_ALPHA64`, as used by Windows NT on Alpha) is the proposed value. A formal registration with the UEFI Forum is needed so loaders and tools agree. **\[OPEN\]**
- Mandatory protocols and drivers: Block I/O, Simple File System, Network (SNP/MNP/TCP/UDP/HTTP boot), Graphics Output (GOP), Console, USB (xHCI, HID, mass storage), NVMe pass-through, PCIe root bridge I/O, RNG, and Runtime Services including `GetTime`, variable services and `ResetSystem`. Boot Services protocols cease to be callable after successful ExitBootServices. A retained framebuffer may still be used under its handoff contract. Runtime Services and the retained KSI functions are reached through the active PAL image, as described below; they do not make Boot Services available again.
- **Secure Boot:** db/dbx/KEK/PK variables, signature verification of all loaded images.
- **Measured boot:** TCG TPM 2.0 event log (EFI TCG2 protocol).
- **Capsule updates** for firmware, with rollback protection.
- Boot flash holds A/B firmware slots so a failed update never bricks the board. EFI page allocation and memory-map accounting use 4 KB units. This is not a translation-granule requirement: UEFI and its applications execute at F with translation off. Retained runtime code and data are aligned and sized in 16 KB multiples so an OS using any supported granule can describe or map them.

Runtime calls after handoff enter through the selected PAL personality's bridge
to KSI; K/U code MUST NOT call F-level firmware addresses directly. A personality
that exposes the UEFI runtime ABI must provide the required call and address
conversion stubs, including SetVirtualAddressMap/ConvertPointer behavior. F-level
firmware remains physically addressed. A classic SRM personality may expose only
its traditional callbacks instead; that does not remove the underlying retained
firmware services. The binary layout of the runtime bridge remains **\[OPEN\]**.

### 7.2 Firmware description

- **Devicetree (mandatory):** the primary description. Bindings are published with the spec.
- **ACPI 6.5 (optional, Tier 2):** MADT-equivalent, MCFG, DSDT for power and thermal. Required only if the target OS needs it.
- **SMBIOS 3.x:** mandatory, for inventory and `dmidecode`-style tools.

### 7.3 PAL images: selection, loading and upgrade

The PAL image belongs to the operating system (or the user), not to the firmware. Firmware ships default images, but it is never the only possible source, and an image can be replaced without changing the firmware. Four rules:

1. **Changeable.** Any image that passes verification can be the active PAL image. An OS vendor can supply its own.
2. **Upgradable.** An image is updated with the operating system, with a firmware capsule, or from setup, without touching the rest of the firmware.
3. **Verified.** PALcode runs at F level, the highest privilege, so no unverified image runs under the default policy.
4. **Stable contract.** The hardware-facing interface of every image is the PAL Platform Interface (PPI), defined in the Architecture Specification, section 6.8. The OS-facing interface (the CALL\_PAL functions the OS sees) is entirely up to the image.

#### Personalities and sources

| ID | Personality | Typical user |
| --- | --- | --- |
| 0 | Kotl reference PAL (generic) | New operating systems that use the reference PAL |
| 1 | OSF/1 | Linux, the BSDs and Tru64-style systems |
| 2 | OpenVMS | OpenVMS-style systems |
| 3 | Windows NT | Windows NT-style systems |
| 4 to 0x7FFFFFFF | Reserved |  |
| 0x80000000 and up | Vendor-defined | Any OS vendor |

The ID is a label that selects a policy and an image. The hardware does not care which one is used. An image comes from one of three sources:

- **Firmware-resident:** inside the firmware volume, replaced when the firmware is updated.
- **OS-supplied:** a file shipped with the OS (usually on the EFI System Partition) and staged by the OS loader on every boot.
- **User-installed:** kept in a flash PAL area that firmware updates leave alone.

Selection order: the OS loader's explicit request, then the setup default for that personality, then none (the boot fails with an error, and never falls back silently to an unverified image).

#### Image format

Little-endian. The header is a multiple of 64 bytes and the payload follows it. **\[TENTATIVE\]**

| Offset | Size | Field | Meaning |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | The ASCII bytes KPAL |
| 0x04 | 2 | header\_version | 1 |
| 0x06 | 2 | header\_size | Bytes, a multiple of 64 |
| 0x08 | 4 | personality | See the table above |
| 0x0C | 4 | ppi\_version | PPI version the image targets: major in the high 16 bits, minor in the low 16 |
| 0x10 | 8 | image\_version | Four 16-bit fields: major, minor, patch, build |
| 0x18 | 4 | svn | Security version number, used for anti-rollback |
| 0x1C | 4 | flags | Bit 0 requires SMT off. Bit 1 uses the legacy translation format. Bit 3 needs software TLB refill (FTLBW). Bit 4 requests the initial hardware fast path in PAL mode; full-PAL mode starts with it on. Bits 5 and 6 declare support for PAL mode and full-PAL mode; at least one must be set. Bit 2 and bits 31:7 are reserved and zero |
| 0x20 | 8 | required\_features | Hardware feature bits the image needs (same layout as FCAP) |
| 0x28 | 8 | load\_size | Bytes the image occupies once loaded, a multiple of 64 KB |
| 0x30 | 8 | data\_size\_per\_hart | Bytes of F-only scratch memory needed per hart |
| 0x38 | 8 | load\_alignment | Power of two, at least 64 KB |
| 0x40 | 4 | payload\_offset | From the start of the file |
| 0x44 | 4 | payload\_size | Bytes |
| 0x48 | 4 | hash\_algorithm | 1 = SHA-384 |
| 0x4C | 48 | image\_hash | SHA-384 of the canonical header followed by the payload; see the digest rules below |
| 0x7C | 4 | signature\_offset | From the start of the file |
| 0x80 | 4 | signature\_size | Size of a detached PKCS#7 signature whose signed content is the 48-byte image\_hash |
| 0x84 |  | reserved | Zero, up to header\_size |

The payload is loaded at the PAL base, and its first bytes are the vector table described in the Architecture Specification, section 6.8. The maximum load\_size is 4 MB. **\[TENTATIVE\]**

#### Canonical digest and bounds

1. `header_size` MUST be at least 0xC0 and a multiple of 64. Reserved header
   bytes are zero. The complete header and payload must be present in the file.
2. `payload_offset` is at least `header_size`; the payload is nonempty and fits
   within `load_size`. The signature, when present, follows the payload. Header,
   payload and signature ranges MUST NOT overlap, and every offset, addition
   and size calculation is checked for overflow before use. Gaps are zero-filled;
   no bytes may follow the payload/signature's final declared range.
3. Copy exactly `header_size` bytes to a digest buffer. Zero `image_hash`
   (0x4C..0x7B), `signature_offset` (0x7C..0x7F) and `signature_size`
   (0x80..0x83) in that copy. All other header bytes retain their file values.
4. Compute SHA-384 over that canonical header followed immediately by exactly
   `payload_size` bytes beginning at `payload_offset`. Padding, gaps and the
   signature bytes are not digest inputs. Compare the result with `image_hash`.
5. Verify the detached PKCS#7 signature with the computed 48-byte digest as its
   content. The digest field is zeroed during hashing to avoid self-reference;
   signature location and size are zeroed so the signature can be generated
   after the digest without a circular size dependency. Bounds are validated
   independently before signature processing.

An unsigned development image has both signature fields zero and ends at the
declared payload end. It is accepted only under Permissive policy. That policy
skips signature trust checks, never bounds, feature, SVN or digest checks.

#### Verification

Done entirely at F level, in this order. Any failure rejects the image with a distinct status.

1. Copy the image into F-only staging memory. Every later check uses the copy, so the caller cannot change it after verification.
2. Bounds-check every header field and offset, and reject anything inconsistent.
3. Check the magic, the header version, the personality, and that the PPI major version is supported.
4. Check that required\_features is a subset of the hardware's features, and that the flags and per-hart data size can be satisfied.
5. Check that svn is not below the platform's minimum for that personality.
6. Check the SHA-384 hash, then verify the signature against KotlPalDb, rejecting any signer or hash in KotlPalDbx (the signature checks are skipped only under the Permissive policy below).
7. Return a staged image ID.

#### Trust and measurement

- **Keys:** two authenticated UEFI variables under the Kotl vendor GUID. KotlPalDb lists allowed signing certificates, and KotlPalDbx lists revoked certificates and hashes. They are managed through the same Platform Key and Key Exchange Key hierarchy, with the same setup mode and user mode, as UEFI Secure Boot. The Kotl reference PAL signer is in KotlPalDb by default. An OS vendor is added by a signed update of KotlPalDb, or by the user.
- **Policy:** the variable KotlPalPolicy is Enforce (default: only signed images run) or Permissive (development: unsigned images run, setup shows a warning, and the image is still measured).
- **Measurement:** before activation, firmware extends a TPM PCR with the image hash, personality, version and SVN, and logs an event. Use PCR 0 for firmware-resident images and PCR 4 for OS-supplied ones. **\[VERIFY\]** the PCR numbers and event types against the TCG PC Client specification.
- **Protection:** the PAL region is marked F-only in the physical memory attributes (FPMA registers), so K and U levels can neither read nor write it. An active image cannot be modified.
- **Anti-rollback:** firmware keeps a minimum SVN per personality in protected flash. It is raised only by an authenticated update (a signed capsule or an authenticated variable write), never lowered.

#### Interfaces

KSI PAL service group (function numbers **\[OPEN\]**):

| Call | What it does |
| --- | --- |
| PAL\_QUERY(index) | With index -1: platform information (supported PPI versions, policy, minimum SVNs, maximum image size, PAL region base). Otherwise: personality, version, SVN, source and flags of resident image number index |
| PAL\_STAGE(address, size) | Copies an image from the caller's buffer (the OS loader, running as a UEFI application) and verifies it as above. Returns a status and a staged image ID |
| MODEL\_SELECT(pal or full-pal, image ID) | Before ExitBootServices, validates and locks the pending model and a verified staged/resident image. An unsuccessful call changes nothing. A successful call is allowed once per boot and leaves FMODEL = 0 until final handoff. Full-PAL selection requires image flags declaring both full-PAL support and the legacy format |
| PAL\_INSTALL(image ID, slot) | Writes a staged image into the user PAL area in flash. Accepted only from firmware setup or a verified capsule |
| PAL\_CONFIRM(image ID) | Tells firmware that a newly installed flash image has booted successfully, so it is not rolled back |

UEFI wraps these in EFI\_KOTL\_PAL\_PROTOCOL (GUID **\[OPEN\]**) with GetInfo,
Stage and Activate. All three are Boot Services operations and MUST be called
before ExitBootServices. Activate reserves all transfer resources and validates
and copies the personality-specific OS handoff descriptor before calling
MODEL\_SELECT. All fallible preparation completes before selection is locked;
failure releases temporary resources without changing the selection. If a
selection was already locked, another Activate is rejected and preserves it.
It then returns a stable physical `Enter` entry and opaque context. The copied descriptor
and context remain valid across failed ExitBootServices attempts; a retry uses
the new memory-map key without reselecting the model.

After successful ExitBootServices, the loader may call only the retained `Enter`
entry to finish this transfer, not the protocol or any other Boot Services.
Its code, context, stack, PAL payload and per-hart data MUST already be allocated
and retained; Enter performs no allocation and returns no success result. It
checks that ExitBootServices succeeded and the prepared context is valid before
changing hardware state. A call before successful exit is rejected without a
model change. Firmware quiesces DMA and interrupt sources, parks excluded harts,
installs the verified image and Info Block, sets FPALBASE, FWALK and FFASTPAL,
commits FMODEL to 1 or 2, and enters the PAL reset vector at F. Any failure before
that commit leaves FMODEL = 0. Once ExitBootServices has succeeded, an invalid
context or any transfer failure is fail-stop/reset, even if the model has not
yet been committed; firmware MUST NOT return to Boot Services. The PAL
initializes OS state and enters the kernel with XRET.
This is a boot handoff entry, not a general UEFI Runtime Service.

The devicetree node /firmware/kotl-pal distinguishes resident, pending and active
images. Firmware finalizes its handoff view before the loader's final memory-map
query, so final transfer does not resize the published devicetree or allocate
memory. Retained F-only firmware/PAL regions remain reserved in the OS memory map.

#### Boot sequences

**OS-supplied PAL (the Windows NT case):**

1. Firmware boots (UEFI runs at F level in firmware mode), and stage 1 measures the firmware.
2. The OS loader (for example the Windows boot manager) finds the PAL image next to its other boot files.
3. It calls GetInfo to learn the supported PPI versions and the policy.
4. It calls Stage with the image. Firmware verifies and measures it. On failure the loader gets a status and decides what to do: boot with the setup default image, or stop with an error. Firmware never runs a failed image.
5. The loader prepares the kernel and calls Activate before ExitBootServices. Firmware locks the verified image/model and returns the retained Enter entry and context. The loader then obtains the final memory map and calls ExitBootServices; stale-map-key failures are retried normally.
6. After successful ExitBootServices, the loader calls Enter with the prepared context. Firmware commits the selected model and enters the PAL reset vector. PAL consumes the prepared OS handoff descriptor and enters the kernel; its entry is not bypassed by a direct firmware-to-kernel jump.

**Firmware-resident image (SRM mode):** the F-level SRM console chooses a verified resident image and prepares MODEL\_SELECT(full-pal, image ID) before relinquishing firmware ownership. A UEFI-hosted SRM/test loader follows Activate, ExitBootServices, Enter. A standalone SRM boot with no live Boot Services uses the same prepared handoff checks and DMA quiescence without pretending to call ExitBootServices. SRM mode is full-PAL mode.

**No image from the loader:** Activate selects the verified setup default image for the requested personality and follows the same preparation and transfer sequence. If no usable image exists, it fails before ExitBootServices. A loader that never prepares a PAL handoff cannot enter a K/U operating system on this platform.

#### Upgrade paths

- **With the OS:** the OS vendor replaces the image file, and the next boot stages the new one. No firmware change. If a resident image exists for the same personality, a verified OS-supplied image with an SVN not lower than the resident one takes priority.
- **Firmware capsule:** a signed capsule carries firmware-resident images. It follows the same A/B bank and rollback rules as the rest of the firmware (section 7.1).
- **Setup:** an Install PAL from file command writes a user-installed image into the flash PAL area. It follows the policy above.
- **Minimum SVN** rises only with an authenticated update, so old images can be retired.
- **Live update without a reboot** is not supported in this version. **\[OPEN\]**

#### Flash PAL store

- Two areas: the firmware volume (firmware-resident images) and the **user PAL area** (4 slots of up to 4 MB each), which firmware updates do not touch.
- Updates are safe against power loss: write the new image into an unused slot, verify it, then flip a commit marker. The old image stays valid until the commit.
- Each slot has a state (empty, valid or pending confirmation) and a boot-attempt counter. A pending image that fails to boot 3 times without PAL\_CONFIRM is marked bad and the previous slot is used.
- The exact layout is **\[OPEN\]**.

#### Developer kit and reuse

- **PAL developer kit:** the PPI document, assembler and binutils support for the PAL opcodes (PR\_READ, PR\_WRITE, XRET, PLD, PST), an image builder and signing tool, a QEMU option (-machine kotl-ev9,pal-image=FILE) that runs the same staging and verification path under the Permissive policy, and a reference OSF/1-style PAL (clean-room, permissively licensed) as a worked example.
- **Conformance suite** for PAL images: entry state, vector layout, interrupts and machine checks, hart start and stop, TLB maintenance, and no use of F-only resources outside the PPI. **\[OPEN\]**
- **Reusing an existing PAL:** an original NT, OSF/1 or VMS PAL binary cannot run unchanged, because it is hardware-specific and belongs to its owner. An OS vendor with the PAL source can port it to the PPI. The CALL\_PAL numbering and entry layout follow the original architecture, so the OS-facing code ports with little change. **\[VERIFY\]**

## 8. PCI Express

### 8.1 Root complexes

- Spec revision: **PCIe Gen 4** on Z1-lite at most (the FPGA decides; Gen 2 or 3 is typical), and PCIe Gen 5 (32 GT/s) on the 21564 and Z1.
- **21564 lane budget (24 lanes):**
  - 1 x 16 lanes for the graphics/accelerator slot, bifurcatable as x8/x8 or x8/x4/x4.
  - 1 x 4 lanes dedicated to an M.2 NVMe slot (CPU-attached, not through a chipset). Plus 1 x 4 lanes (Gen 4) to the chipset (21564 document, section 3).
- Additional lanes (x1/x2 Gen 3) come from the chipset (the 21564 PCH, 21564 document, section 3).
- **Configuration:** ECAM, 256 buses per segment, as in section 5. Enhanced allocation (EA) is optional.
- **Address translation:** all inbound DMA goes through the IOMMU (section 11). Outbound windows are fixed by the memory map.
- **Interrupts:** MSI/MSI-X, mandatory. Legacy INTx via `interrupt-map`.
- **Error reporting:** Advanced Error Reporting (AER), Downstream Port Containment (DPC) on the 21564 and later.
- **Power management:** ASPM L0s/L1 and L1 substates, D-states, PME. Link power states MUST be controllable by the OS.
- **Hot-plug:** native PCIe hot-plug (surprise removal) for the M.2 and slot ports is a 21564 SHOULD, a Z1 MUST.
- **Peer-to-peer DMA:** permitted between devices behind the same root port. Cross-port P2P is controlled by IOMMU/ACS policy.
- **ATS / PRI / PASID:** reserved for the IOMMU 2.0 generation, not required in the 21564.
- **Resizable BAR:** MUST be supported so that GPUs can map their full VRAM.
- **Cache coherency:** PCIe No-Snoop is honoured. Default is coherent.

### 8.2 Compatibility notes

Kotl EV9 supports PCIe devices that use standard architectural driver models. Option ROMs for other architectures (x86 BIOS, UEFI EBC) are ignored. GPUs need either a native driver in firmware for the display path or must be driven by the OS after boot.

## 9. Storage

### 9.1 NVMe

NVMe is the primary boot and data storage.

- **Interface:** NVMe 2.0 or later, over PCIe, controllers as ordinary PCIe endpoints. There is no NVMe controller on the SoC; the SoC provides the root port, IOMMU and interrupts.
- **M.2:** M-key 2280 slot, CPU-attached x4 PCIe Gen 5 on the 21564 and Z1, with 3.3 V power and sideband `PERST#`, `CLKREQ#`, `PEWAKE#`. Lower-generation devices negotiate down normally; FPGA links depend on the board. A PCH-attached M.2 slot follows the PCH's Gen 3 capability.
- **Platform requirements for NVMe devices:**
  - 64-bit BARs are mapped above 4 GB when needed (section 5).
  - MSI-X with at least 1 vector per hart.
  - PRP and SGL descriptors both supported by the platform's IOMMU path.
  - Controller Memory Buffer (CMB) and Host Memory Buffer (HMB) are supported but optional.
- **Boot:** UEFI's NVMe driver (NvmExpressDxe) reads the ESP. `kotl-ev9` in QEMU uses `-device nvme` to prove the same path.
- **Reliability:** SMART/health data is exposed via NVMe admin commands. Secure erase and sanitize supported by the driver stack.
- **Encryption:** TCG Opal, and inline encryption by the SoC are reserved (section 14).

### 9.2 SATA / AHCI

- AHCI 1.3.1 controller (in the chipset on the 21564, optional on the SoC on Z1-lite), 4 ports (SATA 6 Gb/s), to cover legacy disks and optical drives.
- Each port supports hot-plug, NCQ and port multipliers MAY be unsupported.

### 9.3 SD / eMMC

- 1 SD host controller (SDHCI-compatible, UHS-I) for removable boot media and 1 eMMC 5.1 controller for embedded boards.

### 9.4 Boot flash

- Quad-SPI NOR provides at least 64 MiB total backing storage, normally two 32 MiB banks as on KEB-1. It must hold both firmware slots, the persistent variable/rollback metadata, and the four user PAL slots of up to 4 MiB each; the earlier 16 MiB minimum cannot hold that store plus firmware. The active 16 MiB execute-in-place window is mapped as in section 5. Firmware updates preserve the user PAL area. Exact partition offsets remain **\[OPEN\]** and must be budgeted before the flash layout is frozen.

## 10. USB

- **Host controller:** xHCI 1.2, on the SoC (Z1-lite) or in the chipset (21564, Z1), standard register layout so the stock `xhci` driver works on every OS and on EDK II (`XhciDxe`).
- **Ports (21564 chipset):** 4 x USB 3.2 Gen 2 (10 Gb/s), 2 x USB 3.2 Gen 1 and 6 x USB 2.0 from the chipset; boards add more through hubs.
- **Type-C:** Power Delivery and alternate-mode negotiation use an external PD controller over I2C. The SoC provides orientation and role-swap signals. USB4/Thunderbolt is out of scope for the 21564 and left to PCIe-attached controllers.
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

- 2.5 GbE MAC (in the chipset on the 21564, on the SoC on Z1-lite) with RGMII/SGMII PHY interface, descriptor-ring DMA, checksum offload, RSS, and WoL. Alternatively, boards use any PCIe NIC.
- UEFI network stack with PXE and HTTP boot.

### 12.2 Graphics and display

- Primary: PCIe GPU with a standard driver model. UEFI uses the GPU's own GOP when available, and otherwise a simple linear framebuffer described in the devicetree (`simple-framebuffer`).
- A basic display controller (single output, DisplayPort/HDMI via external PHY) is in the chipset on the 21564 and optional on Z1-lite.
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
- **Sleep:** S3-like suspend-to-RAM (DRAM self-refresh) is a 21564 SHOULD; S4/hibernation is handled entirely by the OS.
- **Thermal:** on-die sensors, programmable trip points, hardware throttling, and a critical trip that triggers `SYSTEM_OFF`.
- **Board power:** standard ATX or barrel-jack input; the SoC supplies power-good/reset sequencing signals.

## 14. Security

- **Root of trust:** immutable boot ROM verifies first-stage firmware with a public key hash held in one-time-programmable fuses.
- **Secure/measured boot:** see 7.1. TPM 2.0 is attached over SPI or I2C (discrete TPM), with a firmware TPM as an option.
- **Hardware RNG:** a true random number generator (TRNG) with health tests, exposed as a memory-mapped device and as the UEFI RNG protocol.
- **Crypto acceleration:** AES, SHA-2/SHA-3 and carry-less multiply instructions are reserved for an optional extension. Software fallbacks MUST work.
- **Memory protection:** per-page execute-never, supervisor-mode access prevention (SMAP-like), and supervisor-mode execution prevention (SMEP-like) are mandatory in the MMU.
- **Speculation:** Z1-lite is in-order, but the 21564 and Z1 are out-of-order. The speculation barrier instruction and a documented speculation contract MUST exist, and every out-of-order part MUST ship with documented mitigations for speculative-execution side channels.
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
| Z1-lite FPGA board | First hardware: a large FPGA board for the SoC prototype, DDR4 or DDR5, one PCIe slot, one M.2, USB through a bridge |
| 21564 silicon | First silicon, built only after Z1-lite boots Linux on an FPGA (21564 document, section 6) |

### 16.2 QEMU machine notes

- PCIe root complex with ECAM at the address in section 5; virtio-pci devices are acceptable for disk, net, GPU, and RNG.
- `-device nvme`, `-device qemu-xhci`, and `-device usb-kbd` / `usb-storage` model the same devices real hardware will use.
- Devicetree is generated by the machine at startup and passed to firmware.
- No dependency on legacy Alpha chipset devices.

### 16.3 Operating systems

| OS | Plan |
| --- | --- |
| Linux | Primary. Rework the existing `alpha` port in two steps: first boot it on the OSF/1 PAL image through UEFI, then add support for the Kotl reference PAL (personality 0), adopt the address-dependency-ordering guarantee, and add KAIA drivers. Page size is a kernel build option: 4 KB, 8 KB (legacy) or 16 KB (default) |
| NetBSD | Secondary, builds on its existing Alpha port |
| Others | Out of scope until the platform is stable |

### 16.4 Toolchain

- GCC and binutils: Alpha support exists upstream. EV9 needs new ISA extension patches (AMO, speculation barrier, cache ops) and a UEFI PE/COFF target.
- LLVM: no maintained Alpha backend, a new backend is a longer-term option.
- Standard C library: glibc has an Alpha port that needs updating for the EV9 ABI. musl is a cheaper second target.
- ABI: the standard Alpha calling convention, preserved so existing toolchains keep working.

## 17. Open decisions

1. **PALcode and no native model.** Decided: PALcode owns the privileged layer in both OS models. All current parts implement the legacy 8 KB hardware walker; software refill is an optional PAL choice when FTLBW is present. The exact OS-facing legacy PAL function set remains personality-specific.
2. **PE/COFF machine type.** Use `0x0284` or register a new one. **\[OPEN\]**
3. **Vector extension.** Whether the 21564 and Z1 ship any SIMD beyond MVI.
4. **Cache hierarchy.** Private L2 plus shared L3 here, versus the earlier shared 1 MB L2. Depends on FPGA resource budget.
5. **ACPI tier.** Whether ACPI ships at all, or whether the platform is devicetree-only.
6. **PCIe Gen.** Gen 4 on FPGA is hard (transceivers); Gen 3 may be the real FPGA target.
7. **Microarchitecture.** Decided: Z1-lite is in-order (the test core), and the 21564 and Z1 are out-of-order (21564 document, section 2).
8. **DDR5 vs DDR4 for the FPGA prototype.**
9. **Hypervisor extension timing.** Reserved now, specified when needed.
10. **Licensing of the spec.** Open (CC BY, with a patent grant) is recommended if the aim is adoption.

## 18. Suggested milestones

1. `kotl-ev9` boots a bare-metal test with the section 5 memory map.
2. KAIA, KTIMER and PCIe ECAM modelled; Linux boots to a shell via UEFI on QEMU.
3. NVMe and xHCI boot paths verified on QEMU.
4. Freeze spec v0.5 (platform), then v1.0 once FPGA boots.
5. Single-core RTL, co-simulated against QEMU.
6. Four cores, then the out-of-order 21564 core.
7. FPGA board bring-up with real PCIe, NVMe, USB.
8. Decision on tapeout.
