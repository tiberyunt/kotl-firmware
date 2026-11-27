# Kotl EV9 QEMU bring-up bindings, draft 0.1

These are provisional implementation definitions for the supplied **Kotl EV9
Platform Specification, draft 0.1**. They fill the missing register/encoding
pieces needed for executable tests. They are not amendments to that document,
a frozen ISA, a UEFI registration, or a platform conformance statement.

The provided Alpha architecture manual remains the reference for legacy Alpha
behavior. Native EV9 deliberately changes that privileged environment. The
existing Clipper firmware remains a legacy PAL-based test platform.

## Reset and discovery

Primary hart: PC=0, privilege F, MMU off, interrupts off. Secondary harts are
stopped. Integer arguments: a0=hart ID, a1=physical DTB pointer. A released
secondary additionally receives a2=mailbox argument. General registers are
cleared before those arguments are installed. The test stack is in SRAM;
software establishes its own stack and GP.

The DTB is at 0x01100000 in SRAM, with a reserved 1 MiB window. It describes
`kotl,ev9-qemu-draft01`, CPU nodes and `cpu-map`, native MMU parameters, and the
platform address windows. Child compatibility strings ending in `draft01`
identify these provisional controller layouts. Standard UART and ECAM bindings
retain their usual compatibility names. RAM starts at 0x100000000.

## Native instruction encoding

Alpha instruction size and integer register numbering are unchanged. These
encodings are specific to the experimental `ev9` CPU; legacy CPUs retain the
old behavior.

| Operation | Encoding |
| --- | --- |
| CSR read | opcode 0x19, destination Ra in bits 25:21, CSR ID in bits 15:0 |
| CSR write | opcode 0x1d, source Rb in bits 20:16, CSR ID in bits 15:0 |
| ERET | 0x04000000 (opcode 0x01, function 0) |
| WFI | 0x04000001 |
| K-to-F service trap | 0x04000002 |
| Atomic RMW/CAS | opcode 0x02; Ra=value/desired, Rb=address, Rc=old value/expected |

Atomic function bits are 11:5. Functions 0..5 are ADD, SWAP, AND, OR, XOR and
CAS at 64 bits; 0x10..0x15 select their 32-bit variants. Old 32-bit values are
sign-extended. CAS reads the expected value from Rc and returns the actual old
value there. Function 0x20 performs 128-bit CAS: Ra/Ra+1 are the desired low/high
halves and Rc/Rc+1 are expected low/high halves and returned old halves. Ra and
Rc must be even and at most 28. The address must be naturally aligned.
Bits 12 and 13 request acquire/release; the prototype currently applies full
sequentially consistent barriers for all combinations. Bits 15:14 are reserved.

CALL_PAL IMB (0x86) remains available as instruction-stream synchronization.
Other CALL_PAL functions trap to the native handler; they do not enter PALmode
or implement the legacy privileged PAL functions. VAX FP operations trap rather
than executing the inherited legacy implementation.

## CSRs

All CSRs except TIME require K or F privilege. V privilege is reserved and
rejected. Undefined/read-only writes trap. FTVEC is writable only from F.

| ID | Name | Meaning |
| --- | --- | --- |
| 0 | STATUS | bit 0 interrupt enable; bit 1 MMU enable; bit 2 supervisor user-memory data access; bits 9:8 privilege (U=0,K=1,F=3) |
| 1 | TVEC | Native trap handler PC, aligned to four bytes |
| 2 | EPC | Exception return PC |
| 3 | CAUSE | Native exception code; bit 63 marks an external interrupt |
| 4 | TVAL | Fault VA, or CALL_PAL/service function code |
| 5 | PTBR | Physical root page table and granule selector in bit 0 |
| 6 | ASN | 16-bit address-space number; changing it flushes the software TLB |
| 7 | SCRATCH | Software scratch |
| 8 | HARTID | Read-only hart ID |
| 9 | TIME | Read-only KTIMER count |
| 10 | FREQUENCY | Read-only 24000000 Hz |
| 11 | TLBI | Any write invalidates local translations; reads zero |
| 12 | SAVED_STATUS | Status restored by ERET |
| 13 | FTVEC | Firmware service trap PC |

Exception causes currently match QEMU Alpha's local numbers: machine check 1,
external interrupt 4 (plus bit 63), translation/protection fault 5, alignment 6,
illegal/privileged instruction 7, arithmetic 8, FP-disabled 9, and PAL/service
trap 10. This is a draft software ABI, not an assigned architectural numbering.

A trap saves EPC, CAUSE, TVAL and STATUS, disables interrupts, and enters K
(or remains F when already F). An explicit K-to-F service trap enters FTVEC at
F privilege and sets TVAL=0x100. ERET restores EPC/STATUS and may return only to
an equal or lower privilege level. Software must save trap CSRs before nesting;
there is only one hardware save set. No integer register is implicitly saved.
Firmware is responsible for implementing the KSI service dispatcher.

## Native MMU and IOMMU PTEs

64-bit entries, with physical address bits 47:14, and low flags:

| Bit | Meaning |
| --- | --- |
| 0 | Valid |
| 1 | Read |
| 2 | Write; requires Read |
| 3 | Execute |
| 4 | User |
| 5 | Accessed, hardware updated |
| 6 | Dirty, hardware updated |
| 7 | Global (reserved for later TLB tagging optimization) |
| 8 | Device attribute (functional prototype does not simulate cache policy) |
| 9 | Write-combining attribute (same limitation) |

Bits 13:10 and 63:48 must be zero. A valid entry without R/W/X is a next-level
table pointer. The four levels use VA bit 47 (one-bit root index), bits 46:36,
35:25, 24:14, and a 14-bit offset. Virtual addresses must be sign-extended from
bit 47. Leaves are permitted at the bottom three levels, giving 16 KiB, 32 MiB
and 64 GiB mappings. Physical leaf bases must have the corresponding alignment.
The walker checks permissions and updates A/D bits before filling a translation.
QEMU caches 4 KiB pieces internally for both native granules.

K execution from User pages is denied. K data access to User pages requires
STATUS bit 2. U access requires the User flag. Changing STATUS, PTBR or ASN
flushes local translations. Device/cache attributes and Global are represented
in the draft format but no cache hierarchy or global-ASN optimization is modeled.

## System controller: 0x02000000, 16 MiB

Naturally aligned 64-bit accesses.

| Offset | Access | Meaning |
| --- | --- | --- |
| 0x00 | RO | Identity 0x004b4f544c455639 |
| 0x08 | RO | Register ABI 0x00010000 |
| 0x10 | RO | Hart count |
| 0x18 | RO | Reset/debug entry PC |
| 0x20 | RO | DTB address |
| 0x28 | RO | DRAM base |
| 0x30 | RO | DRAM byte length |
| 0x38 | RO | Bring-up capabilities 0x1f: native CPU, KAIA, KTIMER, PCIe, translating IOMMU |
| 0x100 | WO | 1=power off, 2=system reset, 0x3333=test failure shutdown |

Hart mailbox n is at 0x1000+n*32: entry +0, argument +8, started status +16,
command +24. Writing command 1 releases a stopped hart at a four-byte-aligned
entry in flash or DRAM. A running hart cannot be restarted this way. WFI does
not make the hart eligible for restart. Stop/suspend and model selection are
not implemented here; they require firmware KSI policy.

## KAIA: 0x03000000, 16 MiB

Source IDs 1..1023, zero reserved. Distributor arrays use 32-bit entries:
route-to-hart at 0x0000+id*4, priority at 0x1000+id*4, edge flag at
0x2000+id*4 (0=level,1=edge). Priorities 1..255 are enabled priorities; 0 masks
the source. Highest priority wins, with lower source ID breaking ties.

Hart file n: 0x10000+n*0x1000. Claim at +0 is 32-bit. Software inject/clear at
+0x40/+0x48 are 32-bit source IDs. Pending and enable bitmaps are 16 naturally
aligned 64-bit words at +0x100 and +0x200. Bit zero stays disabled. Claim clears
software/edge pending sources; level interrupts remain pending until the device
is deasserted. CPU interrupt delivery additionally requires STATUS.IE.

Wired IDs: UARTs 1,9,17,25; timer 2 per hart; IPI 3 per hart; IOMMU fault 4 to
hart zero; PCI INTx 32..35 using GPEX swizzling. Software IRQ injection is a CPU
facility. PCI MSI/interrupt remapping is not implemented, so DMA into these
files is denied.

## KTIMER: 0x04000000, 16 MiB

64-bit counter +0, frequency +8. Per-hart compare at +0x100+8*n; UINT64_MAX
cancels a timer. An expired compare injects source 2 into that hart. Setting a
new compare clears its pending timer interrupt. IPI set/clear is at
+0x1000+8*n (bit 0). Native TIME reads the same 24 MHz virtual-time counter.

## IOMMU: 0x30000000, 16 MiB

Naturally aligned 64-bit register accesses. +0 enables translation (bit 0);
reset is disabled/default-deny, not identity bypass. +8 is a 16 KiB-aligned
context-table base. +0x10 invalidates all DMA translations. +0x18 is the fault
count (any write clears count and fault interrupt). +0x20 is the latest fault
IOVA; +0x28 is requester ID in bits 31:16 and read/write flags in bits 1:0.
The counter and latest-fault registers are a bring-up facility, not the queued
fault reporting required by the full specification.

Contexts are indexed by 16-bit PCI requester ID, 16 bytes each. The first word
contains the native page-table root in bits 47:14 and enable bit 0. The second
word is reserved. The walker uses the CPU's 16 KiB/large-page PTE shape, checks
read/write permission, and updates A/D bits. It accepts unsigned 48-bit IOVAs
and only allows translations whose resulting 16 KiB page lies entirely in
DRAM. Table/context accesses must also be within DRAM. MMIO DMA and MSI injection
are denied. Software invalidates after changing tables or contexts. There are
no passthrough mappings, 8 KiB domains or device assignment APIs yet.

## PCIe and UART

GPEX ECAM covers 256 buses at 0x40000000. PIO registers map the first 64 KiB
of the platform aperture at 0x50000000. PCI MMIO windows are identity mapped:
0x80000000 for 2 GiB, and 0x10000000000 for 1 TiB. Firmware/OS configures BARs;
there is no legacy PALcode PCI setup. NVMe and xHCI use QEMU's existing device
implementations through this host bridge and the requester IOMMU.

UART n is at 0x10000000+n*0x10000, standard 16550 register stride 1, clock
1.8432 MHz, firmware baud 115200 8N1. Only the UART's actual register bytes are
implemented inside each reserved 64 KiB slot. No ISA I/O decoding is involved.

## Explicitly pending

EDK II, complete KSI services, PAL compatibility/8 KiB regime, remaining
EV9 ISA additions, MSI remapping, fault queues, runtime/secure boot and the
remaining platform peripherals. Migration is blocked while the state ABI is
unversioned. CPU cache/SMT timing and the formal memory model are not simulated.

## Resident firmware service subset

These provisional firmware bindings are implemented by `firmware/kotl/`, rather
than by QEMU's service-trap transport. K-level callers execute `KSICALL`
(0x04000002), with function in r16 and argument in r17; r0 returns a signed
error and r1 returns the result. Other integer registers are preserved.

| Function | ID | Behavior |
| --- | ---: | --- |
| VERSION | 0 | Returns 0x10000 |
| PROBE | 1 | Argument is function ID; result 1 if implemented, otherwise 0 |
| SYSTEM_OFF | 2 | Powers off the machine |
| SYSTEM_RESET | 3 | Resets the machine |
| CONSOLE_PUTC | 4 | Writes argument's low byte |
| CONSOLE_GETC | 5 | Reads a byte; error -6 if none available |
| TIME | 6 | Returns the 24 MHz counter |
| MODEL_SELECT | 15 | Argument 0 selects native mode idempotently; PAL mode is unsupported |

Successful calls return error 0; unsupported calls return -2. Unknown functions
are not advertised by PROBE. Model selection records native policy; it does not
implement a PAL transition or a secure privilege boundary. The firmware runs
only the primary hart and leaves secondary harts parked. See its README for
retained-memory and execution restrictions.

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

Bochs display uses the existing QEMU PCI endpoint. Native firmware reserves
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

The EDK II platform driver uses 64-bit syscon registers at offset `0x5000`:
UTC Unix seconds (read/write signed, range 1900–9999), `0x5008` host nanoseconds
(read only), `0x5010` capability version 1, and `0x5018` write status (0 success,
1 out of range). Setting seconds adjusts an offset against QEMU's host clock.
The offset survives a guest reset, but is not persisted across QEMU processes.
This is an emulator service for the port, not the proposed physical I2C RTC.
Native PTBR bit 0 selects the translation granule. Zero uses the existing
16 KiB tables and one selects 4 KiB tables with 512 entries per level. Root
addresses use the matching page alignment. QEMU's internal TLB uses 4 KiB
entries; the Clipper/PAL walker continues to decode its 8 KiB page tables.
Native PTBR bit 0 selects the translation granule. Zero uses the existing
16 KiB tables and one selects 4 KiB tables with 512 entries per level. Root
addresses use the matching page alignment. QEMU's internal TLB uses 4 KiB
entries; the Clipper/PAL walker continues to decode its 8 KiB page tables.
