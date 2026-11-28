# Kotl EV9 QEMU bindings, revision 2026-11-28

The three supplied Markdown specifications define the intended architecture.
This document records executable bindings where those specifications still
leave binary encodings open. Such fields are provisional, not new normative
architecture. This revision replaces the earlier direct-CSR binding and is
incompatible with its firmware. Standard Clipper/Alpha behavior is unchanged.

## Reset, parts and discovery

Hardware reset: PC=0, F privilege, translation off, FMODEL=0, interrupts off.
All integer registers are cleared, then R16=hart ID and R17=physical DTB address
are installed for the provisional firmware reset ABI. Secondary harts stop.
The DTB occupies a reserved 1 MiB SRAM window at 0x01100000. RAM begins at
0x100000000; supported size is 64 MiB through 128 GiB.

`part=21564` (ID 2, default) permits four single-thread cores; `part=z1` (ID 3)
permits sixteen single-thread cores. `part=z1-lite` (ID 1, test part) permits
four cores, two threads each. Smaller configurations are allowed, with one
socket. `model=pal|full-pal` is a boot request reported at syscon+0x60; it does
not change the architectural reset state. Part, topology and model are locked
when the machine is created. FMODEL is committed by F-level software once.

The root DTB compatible is `kotl,ev9-qemu-draft02`. CPU properties describe
48-bit native VA, 44-bit PA, all three page granularities and legacy 43-bit VA.
Local CPU interrupt-controller nodes identify IPI=0, timer=1 and external=2;
KAIA is the external parent, not the source of timer/IPI causes. PCI properties
discover requester contexts, MSI remapping and the private MSI doorbell.

## Instructions and traps

Use the Architecture Specification encodings: PR_READ opcode 0x19 (Ra result),
PR_WRITE 0x1d (Rb source), index 15:0; XRET is exactly 0x78000000. All three
instructions are F-only. The retired 0x01 ERET/WFI/service encodings trap.
PLD/PST are opcodes 0x1b/0x1f and F-only. Until their complete binary format is
frozen, the emulator supports displacement bits 11:0 and mode bits 15:12:
0/1 physical signed-longword/quadword; 8/9 access through the interrupted
level's translation. Other modes trap. Stores validate translation for every
byte before writing. Physical MMIO bus errors may still have device effects.

AMO opcode 0x04 uses operation bits 11:8, size bit 7 and ordering bits 6:5.
Ra is operand/expected, Rb address, Rc old result and CAS replacement on entry.
CAS128 uses even pairs (including R30/R31). BITM is opcode 0x05. RMB, SPECB,
ICSYNC and cache operations follow the MISC encodings in the specification.
TCG currently supplies stronger barriers than the minimum requested AMO
ordering. Cache operations check permissions; CBZERO writes a 64-byte line.
The functional emulator does not simulate cache replacement or speculation.

Before PAL activation, traps enter FVEC. After activation they enter FPALBASE
plus reset 0x0000, machine check 0x0100, interrupt 0x0200, synchronous exception
0x0300, reserved opcode 0x0400, privileged PAL 0x2000+64*function or unprivileged
PAL 0x3000+64*(function-0x80). FEPC is the faulting instruction; the PAL image
advances it when completing a CALL_PAL. Every entry clears the LL/SC reservation.

Full-PAL hides EV9 instructions at K/U, with IMPLVER=3 and legacy AMASK there;
F retains all implemented hardware assistance and IMPLVER=9. PAL mode exposes
EV9 instructions to K/U. Only enabled IMB/RDUNIQUE/WRUNIQUE bypass trap entry.

## Privileged register implementation

The register indices follow the supplied architecture. Every PR access is
F-only, including registers that hold the kernel's virtualized state.

| Indices | Registers |
| --- | --- |
| 000..009 | PHARTID, PSTATUS, PEPC, PCAUSE, PTVAL, PVEC, PSCRATCH0/1, PIE, PIP |
| 00A..00E | PPTBRU, PPTBRK, PASN, PUNIQUE, PCOUNTEREN |
| 010..012 | PTIME, PCYCLE, PINSTRET (read only) |
| 018..01A | PTLBIVA, PTLBIASN, PTLBIALL (write commands; read zero) |
| 100..107 | FSTATUS, FEPC, FCAUSE, FTVAL, FVEC, FSCRATCH, FEDELEG, FIDELEG |
| 108..10F | FMODEL, FPALBASE, FCAP, FPARTID, FPALID, FPALSIZE, FPALINFO, FFASTPAL |
| 110..11F | FPMA0..15 |
| 122 | FWALK (hardware walking only, reads/writes 1) |
| 140..147 | FSCRATCH0..7; FSCRATCH aliases FSCRATCH0 |

Undefined indices, read-only writes, nonzero delegation and unavailable refill
commands trap. Performance/debug registers are not yet implemented.
PSTATUS allows IE bit 0, previous IE bit 1, previous K/U level bit 2,
FS bits 4:3, SUM 5 and MXR 6;
no MMU enable bit exists. K/U always translate; F never does. FSTATUS's
**provisional** saved-state encoding is IE bit 0, level bits 9:8 (U=0,K=1,F=3).
Nested F handlers must save their outer trap state themselves.

PIE/PIP use software IPI bit 0, timer 1, external 2 (bits 3/4 reserved for
additional interrupt classes). PIP bit 0 is writable; wired bits follow inputs.
F masks ordinary interrupts. K requires PSTATUS.IE; U accepts enabled sources.

FCAP is a positive presence bitmap: native features 0x003f0307 plus legacy
hardware walker bit 33. Optional software refill bit 32 and virtualization bit
34 are clear. AMASK is inverted. PTIME uses 24 MHz virtual time; PCYCLE/RPCC use
100 MHz for Z1-lite and 2.5 GHz for the other parts. PINSTRET counts translated
completed instructions. QEMU does not model performance timing. PASN changes
and ASN invalidations conservatively flush local TLB state. PTLBIVA flushes
all 4 KiB pieces of the containing 16 KiB region; this can over-invalidate.

FPALBASE and FPALINFO are F-writable only before activation. FPALSIZE is read
only, derived when FPALBASE is written from an explicit matching F-only RX PMA
of 64 KiB through 4 MiB. FMODEL accepts 1/2 once and requires retained code to
be F-only, readable/executable and nonwritable under all intersecting PMAs.
Subsequent PMA writes cannot remove that protection. Full-PAL initializes
FFASTPAL=1. FPALID is provisionally the physical pointer FPALINFO+0x180 to an
identification record, rather than packing an entire image identity into a PR.

## PMA descriptor encoding (provisional)

| Bits | Meaning |
| --- | --- |
| 43:16 | Physical base (64 KiB precision) |
| 5:0 | log2 region size, 16..44 |
| 6/7/8 | Read/write/execute |
| 9/10/11 | U/K/F permission masks |
| 12/13/14 | Cacheable/atomic-capable/device |
| 15 | Valid |
| 63:44 | Reserved zero |

Regions are base+size intervals; matching permissions intersect restrictively.
No match denies access. Atomic operations require cacheable, atomic-capable,
non-device RAM, additionally restricted by the page attribute.
Reset entries 0..12 cover ROM, SRAM, controllers, UART/on-SoC windows, ECAM,
PCI apertures and bootstrap F-only RAM. Entries 13..15 are retained code,
scratch and information descriptors for the developer loader. The provisional
DMA policy excludes every explicit F-only region; the broad
bootstrap F-only RAM entry does not deny all DMA. Firmware must program and
retain the reserved descriptors consistently on all harts. Firmware may
consolidate low MMIO regions to free descriptors for retained EFI code/data. DMA checks the union
of retained restrictions across harts. PMA writes invalidate IOMMU mappings;
fault/invalidation queues recheck their backing memory on every use, so a
region protected after queue setup cannot become a DMA back door.

## CPU and DMA page tables

Native roots split the two 47-bit halves of a canonical 48-bit VA. PPTBRU/K
bits 1:0 select 0=16 KiB, 1=4 KiB, 2=8 KiB (3 invalid); bit 2 is LEGACY.
Root alignment matches the granule except split legacy roots, aligned to 4 KiB.
Native 4 KiB uses widths 8/9/9/9, native 8 KiB 4/10/10/10, native 16 KiB
11/11/11. Valid nonleaf PTEs have R/W/X clear. Leaf permissions and aligned
superpages follow the architecture. The current physical width is 44 bits.

Native PTE: V0,R1,W2,X3,U4,G5,A6,D7, ATTR9:8, RSW11:10, address47:12, C63.
ATTR values 0 normal, 1 device, 2 write-combining, 3 non-cacheable restrict PMA
attributes; AMOs require ATTR=0. Hardware updates A/D atomically. W without R
and unimplemented physical-address bits fault. Cache policy has no timing model.

Legacy uses canonical 43-bit VA and original Alpha PTEs: PFN63:32 multiplied
by 8192, V0, fault bits 1/2/3, read permissions 8/9, write permissions 12/13.
Software bits, including Linux bits 17/18, are preserved. Native A/D bits are
never written into legacy PTEs. PPTBRU points at the original root's first
512 entries; PPTBRK points 4096 bytes later. The walker indexes 9/10/10 bits
per half. Full-PAL forces legacy format; explicit LEGACY also works in PAL mode.

## Board MMIO

All described controller registers are little endian. Syscon, timer and IOMMU
use naturally aligned 64-bit accesses; KAIA uses 32-bit control registers and
64-bit pending/enable words. These remain provisional controller bindings.

| Physical window | Device |
| --- | --- |
| 00000000..00FFFFFF | 16 MiB ROM/XIP window (64 MiB backing pending) |
| 01000000..011FFFFF | 2 MiB SRAM; upper MiB reserved for DTB |
| 02000000 | Syscon |
| 03000000 | KAIA, 1024 slots, source 0 reserved |
| 04000000 | KTIMER, 24 MHz |
| 10000000 + n*10000 | Four 16550 UARTs, byte accesses |
| 30000000 | IOMMU |
| 40000000 | PCIe ECAM |
| 50000000 | PCI PIO aperture |
| 80000000 | PCI32 MMIO aperture |
| 100000000 | RAM |
| 10000000000 | PCI64 MMIO aperture |

Syscon +00 identity, +08 ABI version, +10 hart count, +18 reset entry, +20 DTB,
+28 RAM base, +30 RAM size, +38 capabilities, +60 requested model (1/2).
Write +100: 1 shutdown, 2 reset, 0x3333 failed-test shutdown. Mailbox
+1000+32*hart has entry +0, argument +8, started +10, command +18 (1=start).
Starts require an F caller. Active PAL starts enter FPALBASE with the same
PAL/PMA state, R16=Info, R17=hart, R18=1; other GPRs zero. Before activation,
the earlier firmware entry/argument binding applies. Full-PAL excludes siblings.
+6000 writes 1/2 perform global TLB/translated-code invalidation for the test ROM.

KAIA +4*source route, +1000+4*source priority, +2000+4*source edge flag.
Per-hart file +10000+1000*hart: +00 claim, +40 inject, +48 clear/ack,
+100..178 pending words (RO), +200..278 enable words (RW). Level sources
remain pending while asserted, including IOMMU fault source 4. MSI destinations
are restricted to sources 64..1023. UARTs/PCI INTx use the wired routes.
KTIMER +0 count, +8 frequency, +100+8*hart comparator (UINT64_MAX disables),
+1000+8*hart software IPI bit. Timer and IPI feed local CPU causes directly.

## IOMMU context v2 and queues (provisional)

Control +00 bit 0 enables translation; +08 context base (16 KiB aligned),
+10 write invalidates all; +18 fault count/clear, +20 last address, +28 last
info. Context address = base+32*RID, with four little-endian 64-bit words:

1. Lower root address, enable bit 0, granule bits 2:1 (0=16K,1=4K,2=8K),
   LEGACY bit 3 (requires 8K).
2. Upper root address with matching enable/format.
3. MSI table address (4 KiB aligned), enable bit 0.
4. Reserved zero.

IOVAs are unsigned 48-bit native or 43-bit legacy, split at bit 47/42 into
per-half roots. Root alignment is the granule, or 4 KiB for legacy half-roots.
Page tables use the CPU formats above. DMA targets, queues and page tables must
be ordinary RAM outside retained PAL regions; arbitrary MMIO DMA is denied.
Updating a context/PTE requires an invalidation before reuse.

The one exception is a private, requester-specific MSI IOVA 0x03F00000. It
accepts aligned 32-bit writes, payload 0..255; reads and other widths/offsets
fault. A remap table has 256 64-bit entries: valid bit 0, destination hart 7:4,
source 25:16, remaining bits zero. Missing contexts/tables/entries deny delivery.
Real PCI MSI and MSI-X use this path, so one requester cannot choose another's
interrupt destinations.

Fault ring registers: +30 base, +38 power-of-two entry count, +40 producer head
(RO), +48 consumer tail, +50 overflow (write zero clears). Records are 32 bytes:
address, RID/access info, fault sequence and reserved zero. Bases are 32-byte
aligned; ring sizes are 2..4096 entries. Enable/disable before changing a base.
Faults or overflow hold KAIA source 4 until drained/cleared; acknowledging the
interrupt file alone cannot suppress an asserted level.

Invalidation ring: +60 base, +68 count, +70 consumer head (RO), +78 producer tail,
+80 status (RO), +88 completed token (RO), +90 capabilities=7. Descriptors are
four 64-bit words: op bits 7:0 (1=global,2=RID,3=fence), RID bits 31:16 only
for op 2; words 1/2 zero; word 3 completion token. Execution is synchronous and
ordered. Status 1 bad configuration, 2 malformed descriptor, 3 inaccessible
queue memory, 4 overrun. Repair a bad descriptor and rewrite the same tail to
retry; no failed descriptor is consumed. Disable resets queue state.

## PAL image developer loader and retained information

`pal-image=FILE` cannot be combined with bios/kernel/initrd. The test ROM starts
at PC=0/FMODEL=0, writes protected PAL registers and commits FMODEL before
XRET to PAL reset. R16 points at Info, R17=hart, R18=0 on primary reset; all
other GPRs are zero. Each hart establishes its own stack and execution state.

KPAL v1 uses the canonical digest/range rules in the Platform Specification.
This path is Permissive only: hashes and constraints are checked, signatures
are not authenticated. No persistent policy exists; minimum SVN is zero for
all personalities. It is not a replacement for UEFI staging/ExitBootServices,
Enforce policy, TPM measurement or PAL image installation/confirmation.

Info is read-only F memory, 64 KiB aligned. The **provisional** little-endian
layout below fills currently unspecified binary structure details:

| Offset | Value |
| --- | --- |
| 000/004 | u32 PPI version 0x10000 / fixed header size 0x100 |
| 008 | u64 hardware capabilities |
| 010/018 | u64 hart count / pointer to u64 hart ID array at +100 |
| 020/028 | u64 retained code base / allocated size |
| 030/038 | u64 scratch base / requested bytes per hart |
| 040/048/050 | u64 KAIA / timer / UART addresses |
| 058/060 | u64 memory-map pointer (+200) / retained KSI entry (ROM+100) |
| 070/080 | u64 DTB / OS handoff pointer (zero in this test agent) |
| 180 | identity: u32 personality, u32 SVN, u64 image version, u32 PPI, u32 flags, 48-byte digest |
| 200 | u64 memory-map count, then 32-byte {base,size,usage,reserved} entries; usage 1 retained, 0 free |

Each scalar not marked u32 is u64; unspecified bytes are zero. Code size is
rounded to a power of two for the PMA descriptor, scratch covers all harts,
and at least the first 16 MiB RAM remains firmware-reserved. HWRPB, ACPI/SMBIOS,
TPM log and OS handoff structures are not supplied by this agent.

The test ROM uses direct F calls, R16=function, R17.. arguments, R0=value,
R1=error, R26 return address. It clobbers R2/R3. Function numbers are explicitly
provisional: 0 version, 1 probe, 2 off, 3 reset, 4 putc, 5 nonblocking getc
(-1 value if empty), 6 time, 7 hart start, 10 status, 11 set local timer,
12 send IPI, 13 remote TLB flush, 14 remote instruction synchronization.
Unsupported functions return error -2; bad hart IDs return -3. Hart start takes
ID, entry and argument; active PAL hardware always enters its reset vector.
Stop, suspend, MODEL_SELECT and runtime/image management are not exposed.
Only PAL/F software may call this physical entry; OS software needs PAL calls.

The following retained emulator backends document existing wire formats from
the earlier companion firmware. They remain available for the firmware port;
they do not establish that the earlier firmware runs on the revised CPU ABI.

## Draft firmware keyboard mailbox

The syscon DTB node advertises `kotl,keyboard-mailbox = <0x2000>`. Naturally
aligned 64-bit read at syscon+0x2000 reports queued key count (0..63). Reading
+0x2008 dequeues one key: bit 63 marks a valid entry, bits 31:16 contain the EFI
scan code and bits 15:0 contain the Unicode character. Empty reads return zero.
This register has a dequeue side effect and should not be read speculatively.
Reset clears queued keys and shift state; overflow drops the newest key.

The QEMU window and QMP keyboard paths feed the same polled queue. Basic US
letters, digits, shift, enter, backspace, tab, space, a few punctuation keys,
UP/DOWN/LEFT/RIGHT, ESC, F1, F9, F10 and F12 are translated. F9/F10/F12 use
scan codes 0x13/0x14/0x16, UP/DOWN 1/2, and RIGHT/LEFT 3/4.
No interrupt is generated. This is an explicit draft platform extension for
firmware interaction, not a claimed USB/PS2 or full input-device implementation.

Bochs display uses the existing QEMU PCI endpoint. The earlier companion firmware reserved
the top 32 MiB of PCI32 MMIO for its framebuffer/register BARs; the active NVMe
controller uses the bottom aperture. These are firmware allocation choices,
not additional fixed hardware registers.

## Draft firmware configuration store

The syscon DTB node advertises `kotl,settings-store = <0x3000>`. Accesses are
naturally aligned 64-bit little-endian operations:

| Offset | Read | Write |
| --- | --- | --- |
| 0x3000 | Capability: 1 reset-retained, 2 file-backed | Ignored |
| 0x3008 | Last commit result: 0 success, 1 host write failure | Commit staging with 0x4b4f544c53415645 |
| 0x3100–0x3138 | Eight words of committed data | Eight words of staging data |

Committed and staging buffers are distinct. A failed save preserves committed
data. Reset clears staging and status, retaining committed data. With
`settings-file=PATH`, QEMU reads an existing exact-size file at machine creation;
a missing file starts empty. Wrong-size files warn and start empty; other read
errors fail startup. On commit GLib requests consistent replacement and durable synchronization of
the specified file with the staged 64 bytes before publishing the new committed value. With no path, commit is
memory-only. This storage is not a DMA target or a general host-file interface.

The firmware record is 64 bytes: magic 0x315445534c544f4b at +0, u32 version 1
at +8, size 64 at +12, startup delay at +16, graphics enable at +20, preferred
PCI requester at +24, namespace at +28, ESP ordinal at +32, CRC32 at +36, and
24 reserved bytes at +40. CRC covers the entire record with the checksum zeroed.
The firmware accepts delays 2/5/10, graphics 0/1, requester <256 and ordinal
<128. An invalid record uses defaults without an implicit commit. There are no
EFI variable attributes, authentication, wear leveling, or crash recovery journal.

## Draft EFI variable backing store

The syscon DTB node advertises `kotl,variable-store = <0x4000>`. Accesses are
aligned 64-bit little-endian operations:

| Offset | Read | Write |
| --- | --- | --- |
| 0x4000 | Capability: 2 with `variables-file`, otherwise 0 | Ignored |
| 0x4008 | Last commit result: 0 success, 1 failure | Commit staging with 0x4b4f544c56415253 |
| 0x10000–0x1fff8 | 8192 committed words (64 KiB) | 8192 staging words |

Staging is distinct from committed data. Reset clears staging/status and retains
committed data. Missing files start empty; wrong-size files warn and start empty;
other read failures stop startup. A commit requires a file backend. GLib's
consistent/durable replacement must succeed before committed data changes. The
firmware serializes only nonvolatile variables, preserving their old in-memory
values on failed host commits. This store is CPU MMIO, not a DMA destination.

The firmware's version-1 wire format has a 64-byte header: u64 magic
0x315241564c544f4b, u32 version 1, record size 1184, total size 65536, CRC32,
record count, and nine reserved words. CRC covers all 65536 bytes with its word
at offset 20 zeroed. The header is followed by 32 fixed records and zero padding.
A record contains a 16-byte GUID, u32 attributes/data-size/name-units/reserved,
64 UTF-16 name units and 1024 data bytes. Zero attributes mark an entirely zero
unused record. Used names include their terminator, have 2..64 units, and contain
no earlier NUL. Data size is 1..1024. Only NV|BS and NV|BS|RT records persist.
Duplicate name/GUID records invalidate the complete store.

This is an explicitly provisional serialization/backing interface, separate
from the standard EFI variable API. It has no authentication or flash journal.
### Experimental RTC service

The earlier EDK II platform driver used 64-bit syscon registers at offset `0x5000`:
UTC Unix seconds (read/write signed, range 1900–9999), `0x5008` host nanoseconds
(read only), `0x5010` capability version 1, and `0x5018` write status (0 success,
1 out of range). Setting seconds adjusts an offset against QEMU's host clock.
The offset survives a guest reset, but is not persisted across QEMU processes.
This is an emulator service for the port, not the proposed physical I2C RTC.
