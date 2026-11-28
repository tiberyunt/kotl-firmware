# Kotl EV9 Architecture and RTL Specification

Draft 0.1 · 24 November 2026 · Companion to the Kotl EV9 Platform Specification

Consistency revision · 28 November 2026: this revision fixes the PAL handoff
boundary, requires legacy hardware walking on current parts, and clarifies
full-PAL visibility, physical protection and firmware translation rules.

## 1. Purpose, scope, precedence

This document is meant to be detailed enough to build the Z1-lite core from, and to serve as the common base for the 21564 core. Part I (sections 2 to 7) defines the architecture: registers, encodings, memory model, privileged architecture and MMU. Part II (sections 8 to 13) defines the microarchitecture of the Z1-lite test core, the RTL module breakdown, the verification plan and bring-up. The 21564 microarchitecture is in its own document, which reuses this verification and RTL framework. The Platform Specification covers everything outside the core: memory map, interrupt controller, PCIe, storage, USB and boot.

Where sources disagree, the order of precedence is: (1) the machine-readable Sail model, once it exists; (2) this document; (3) the kotl-ev9 QEMU machine. A disagreement is a bug to be logged in the errata list (section 13), never silently resolved in code.

Terminology follows the original Alpha manuals: byte = 8 bits, word = 16, longword = 32, quadword = 64. A hart is one hardware thread. EV9 is the architecture. The implementations are the 21564 (base processor), Z1 (middle range) and Z1-lite (test only); see Platform Specification, section 1.1.

Architectural decisions shared with the Platform Specification:

- Page tables use two root registers (one per address-space half) and the translation granule is configurable at 4 KB, 8 KB or 16 KB. Table depth depends on the granule: 4 levels at 4 KB, 4 at 8 KB and 3 at 16 KB (section 7).
- The encodings left open there (atomics, speculation barrier, cache operations) are defined in section 4.

Status words: **\[FIXED\]** means decided, **\[TENTATIVE\]** means a reasonable default that may change before RTL freeze, **\[VERIFY\]** means a value recalled from the original Alpha architecture that must be checked against binutils or QEMU test vectors before use.

# Part I: Architecture

## 2. Architectural state

| State | Width | Notes |
| --- | --- | --- |
| PC | 64 | Bits 1:0 always 0. Canonical at K/U (bits 63:48 equal bit 47, or legacy canonical form); zero-extended implemented physical address at F |
| R0 to R30 | 64 | Integer registers. R31 reads as 0, writes are discarded |
| F0 to F30 | 64 | Floating-point registers. F31 reads as +0.0, writes are discarded |
| FPCR | 64 | Rounding mode, trap enables, sticky status (section 4.8) |
| Lock state | address + valid | LL/SC reservation, one per hart |
| PUNIQUE | 64 | Per-hart user thread pointer (section 6.7) |
| Privileged registers |  | Section 6.2 |

Each hart has its own copy of all of the above. Little-endian only. There is no big-endian mode.

### 2.1 Register conventions (software ABI, unchanged from Alpha)

| Registers | Role |
| --- | --- |
| R0 | Return value |
| R1 to R8 | Caller-saved temporaries |
| R9 to R14 | Callee-saved |
| R15 | Frame pointer (callee-saved) |
| R16 to R21 | Arguments 0 to 5 |
| R22 to R25 | Caller-saved temporaries |
| R26 | Return address |
| R27 | Procedure value (callee address on a call) |
| R28 | Assembler temporary |
| R29 | Global pointer |
| R30 | Stack pointer |
| R31 | Zero |
| F0, F1 | Return values |
| F2 to F9 | Callee-saved |
| F10 to F15 | Caller-saved temporaries |
| F16 to F21 | FP arguments |
| F22 to F30 | Caller-saved temporaries |

### 2.2 Data formats

- Integers: byte, word, longword, quadword. Longword results of operate instructions are sign-extended to 64 bits.
- Floating point: IEEE 754 single (S) and double (T). A single-precision value held in an FP register uses the double-precision layout (exponent widened); LDS/STS and CVT instructions convert between memory and register images. **\[VERIFY\]**
- VAX floating-point formats (F, G, D) are not supported (section 4.9).

### 2.3 Alignment

- Loads and stores of 1, 2, 4 and 8 bytes at any alignment are handled in hardware on all current parts, including accesses that cross a 64-byte line (split into two accesses, not atomic). **\[TENTATIVE\]**
- LDQ\_U and STQ\_U ignore the low 3 address bits, as in Alpha.
- Load-locked, store-conditional, and AMO instructions require natural alignment, otherwise they raise ALIGN.
- Instruction fetch addresses are always 4-byte aligned; branch and jump targets have bits 1:0 cleared.

## 3. Instruction formats

All instructions are 32 bits. Bit numbers are inclusive ranges.

| Format | 31:26 | 25:21 | 20:16 | 15:13 | 12 | 11:5 | 4:0 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Operate (register) | opcode | Ra | Rb | 0 | 0 | function | Rc |
| Operate (literal) | opcode | Ra | literal\[7:0\] in 20:13 |  | 1 | function | Rc |

| Format | 31:26 | 25:21 | 20:16 | 15:0 |
| --- | --- | --- | --- | --- |
| Memory | opcode | Ra | Rb | disp16 (signed) |
| Memory function (MISC) | opcode | Ra | Rb | function16 |
| Jump (opcode 0x1A) | opcode | Ra | Rb | type\[15:14\], hint\[13:0\] |
| Branch | opcode | Ra | disp21 (signed) in 20:0 |  |
| Privileged register access | opcode | Ra | Rb | register index (16 bits) |

| Format | 31:26 | 25:21 | 20:16 | 15:5 | 4:0 |
| --- | --- | --- | --- | --- | --- |
| FP operate | opcode | Fa | Fb | function (11 bits) | Fc |
| PAL call | 0x00 | function\[25:0\] |  |  |  |

Address rules: memory effective address is Rb + sign-extended disp16. Branch target is PC + 4 + 4 times sign-extended disp21. In the operate-literal form the 8-bit literal is zero-extended and replaces Rb. R31 as a source reads 0.

## 4. Instruction set

### 4.1 Opcode map

| Opcode | Instruction or group | EV9 status |
| --- | --- | --- |
| 0x00 | CALL\_PAL | Base, see 6.6 |
| 0x01 to 0x03, 0x06, 0x07 | Reserved | Raises ILLEGAL |
| 0x04 | AMO group | EV9 extension (4.5) |
| 0x05 | BITM group | EV9 extension (4.6) |
| 0x08 / 0x09 | LDA / LDAH | Base |
| 0x0A / 0x0C | LDBU / LDWU | Base (BWX) |
| 0x0B / 0x0F | LDQ\_U / STQ\_U | Base |
| 0x0D / 0x0E | STW / STB | Base (BWX) |
| 0x10 / 0x11 / 0x12 / 0x13 | INTA / INTL / INTS / INTM | Base |
| 0x14 | ITFP (IEEE subset only) | Base (FIX) |
| 0x15 | FLTV (VAX FP) | Reserved, raises ILLEGAL |
| 0x16 / 0x17 | FLTI (IEEE FP) / FLTL (FP misc) | Base |
| 0x18 | MISC | Base plus EV9 codes (4.7) |
| 0x19 / 0x1D | PR\_READ / PR\_WRITE | Privileged (6.2) |
| 0x1A | JSR group | Base |
| 0x1B / 0x1F | PLD / PST (physical load/store) | F level only |
| 0x1C | FPTI | Base (BWX/CIX/MVI/FIX) |
| 0x1E | XRET | Privileged (6.4) |
| 0x20, 0x21, 0x24, 0x25 | LDF, LDG, STF, STG (VAX) | Reserved, raises ILLEGAL |
| 0x22 / 0x23 / 0x26 / 0x27 | LDS / LDT / STS / STT | Base |
| 0x28 / 0x29 | LDL / LDQ | Base |
| 0x2A / 0x2B | LDL\_L / LDQ\_L | Base |
| 0x2C / 0x2D | STL / STQ | Base |
| 0x2E / 0x2F | STL\_C / STQ\_C | Base |
| 0x30 / 0x34 | BR / BSR | Base |
| 0x31 FBEQ, 0x32 FBLT, 0x33 FBLE, 0x35 FBNE, 0x36 FBGE, 0x37 FBGT | FP branches | Base |
| 0x38 BLBC, 0x39 BEQ, 0x3A BLT, 0x3B BLE, 0x3C BLBS, 0x3D BNE, 0x3E BGE, 0x3F BGT | Integer branches | Base |

**\[VERIFY\]** the full map against binutils (opcodes/alpha-opc.c) before the decoder is written.

### 4.2 Memory and branch semantics

- LDA: Ra = Rb + disp. LDAH: Ra = Rb + (disp shifted left 16). No memory access.
- Loads: LDBU, LDWU zero-extend; LDL sign-extends; LDQ, LDQ\_U full width. Stores write the low 1, 2, 4, 8 bytes of Ra.
- LDL\_L/LDQ\_L set the lock state; STL\_C/STQ\_C store only if the reservation is still valid and write 1 (success) or 0 (failure) into Ra (section 5.4).
- BR and BSR: Ra = PC + 4 (the link), then branch. Conditional branches test Ra against zero (or its low bit for BLBC/BLBS) and take the branch if true.
- JSR group (opcode 0x1A): type bits 15:14 are JMP = 0, JSR = 1, RET = 2, JSR\_COROUTINE = 3. Target = Rb with bits 1:0 cleared, link PC + 4 into Ra. The type and the 14-bit hint steer the branch predictor and return stack, and never change architectural behavior.

### 4.3 Integer operate functions

Function codes are hexadecimal, in bits 11:5. **\[VERIFY\]**

| Opcode | Functions |
| --- | --- |
| 0x10 INTA | ADDL 00, S4ADDL 02, SUBL 09, S4SUBL 0B, CMPBGE 0F, S8ADDL 12, S8SUBL 1B, CMPULT 1D, ADDQ 20, S4ADDQ 22, SUBQ 29, S4SUBQ 2B, CMPEQ 2D, S8ADDQ 32, S8SUBQ 3B, CMPULE 3D, ADDL/V 40, SUBL/V 49, CMPLT 4D, ADDQ/V 60, SUBQ/V 69, CMPLE 6D |
| 0x11 INTL | AND 00, BIC 08, CMOVLBS 14, CMOVLBC 16, BIS 20, CMOVEQ 24, CMOVNE 26, ORNOT 28, XOR 40, CMOVLT 44, CMOVGE 46, EQV 48, AMASK 61, CMOVLE 64, CMOVGT 66, IMPLVER 6C |
| 0x12 INTS | MSKBL 02, EXTBL 06, INSBL 0B, MSKWL 12, EXTWL 16, INSWL 1B, MSKLL 22, EXTLL 26, INSLL 2B, ZAP 30, ZAPNOT 31, MSKQL 32, SRL 34, EXTQL 36, SLL 39, INSQL 3B, SRA 3C, MSKWH 52, INSWH 57, EXTWH 5A, MSKLH 62, INSLH 67, EXTLH 6A, MSKQH 72, INSQH 77, EXTQH 7A |
| 0x13 INTM | MULL 00, MULQ 20, UMULH 30, MULL/V 40, MULQ/V 60 |
| 0x1C FPTI | SEXTB 00, SEXTW 01, CTPOP 30, PERR 31, CTLZ 32, CTTZ 33, UNPKBW 34, UNPKBL 35, PKWB 36, PKLB 37, MINSB8 38, MINSW4 39, MINUB8 3A, MINUW4 3B, MAXUB8 3C, MAXUW4 3D, MAXSB8 3E, MAXSW4 3F, FTOIT 70, FTOIS 78 |

Notes for the RTL:

- The /V forms trap with ARITH on signed overflow.
- CMOVxx reads three sources (Ra for the condition, Rb or literal for the value, and the old value of Rc) and always writes Rc. The register file therefore needs three read ports on the CMOV path.
- There is no integer divide instruction. Division is a library call. **\[TENTATIVE\]** A DIV extension in the BITM space is reserved for later.
- AMASK returns its argument with a bit cleared for each implemented feature. Bits: 0 BWX, 1 FIX, 2 CIX, 8 MVI, 9 precise arithmetic traps, 16 AMO, 17 BITM, 18 EV9 barriers and cache operations, 19 4 KB granule, 20 8 KB granule, 21 16 KB granule. All current parts physically implement all of them. IMPLVER returns 9 in firmware/PAL mode and at F, while full-PAL K/U discovery is filtered as specified in 6.9. FPARTID identifies the physical part. **\[TENTATIVE\]**

### 4.4 FP and transfer instructions

Exact function-code tables for opcodes 0x14, 0x16, 0x17 are generated from the machine-readable ISA description rather than typed here, to avoid transcription errors. The IEEE subset to implement:

- 0x16 FLTI: ADDS, ADDT, SUBS, SUBT, MULS, MULT, DIVS, DIVT, CMPTUN, CMPTEQ, CMPTLT, CMPTLE, CVTTS, CVTTQ, CVTQS, CVTQT.
- 0x17 FLTL: CVTLQ, CVTQL, CPYS, CPYSN, CPYSE, MT\_FPCR, MF\_FPCR, FCMOVEQ, FCMOVNE, FCMOVLT, FCMOVGE, FCMOVLE, FCMOVGT.
- 0x14 ITFP: ITOFS, ITOFT, SQRTS, SQRTT (the VAX variants raise ILLEGAL).
- 0x1C: FTOIT, FTOIS move FP register bits to integer registers.
- Qualifiers in the function field select the rounding mode (chopped, normal, minus infinity, dynamic) and the trap-enable behavior.

### 4.5 AMO group (opcode 0x04, EV9 extension) **\[TENTATIVE\]**

Operate format with bit 12 = 0. Ra = operand, Rb = address (no displacement), Rc = result (the old memory value; R31 discards it).

| Bits | Field | Values |
| --- | --- | --- |
| 11:8 | Operation | 0 SWAP, 1 ADD, 2 AND, 3 OR, 4 XOR, 5 SMIN, 6 SMAX, 7 UMIN, 8 UMAX, 9 CAS, A CAS128 |
| 7 | Size | 0 = longword, 1 = quadword (ignored for CAS128) |
| 6:5 | Ordering | 0 relaxed, 1 acquire, 2 release, 3 acquire and release |

Semantics: old = memory\[Rb\]; memory\[Rb\] = op(old, Ra); Rc = old (sign-extended for longword). CAS compares memory with Ra and, if equal, stores the value that Rc held on entry; Rc receives the old value either way. CAS128 uses even-numbered register pairs for Ra and Rc. All AMOs are single-copy atomic, need natural alignment (ALIGN otherwise) and need a memory region marked atomic-capable (STORE\_ACCESS otherwise).

### 4.6 BITM group (opcode 0x05, EV9 extension) **\[TENTATIVE\]**

Operate format. Functions (bits 11:5): 00 RORQ, 01 ROLQ, 02 RORL, 03 ROLL, 10 BSWAPQ, 11 BSWAPL, 12 BSWAPW, 13 BREVQ. Rotate amounts come from Rb or the literal (low 6 bits, or low 5 for the longword forms). Reserved space in this group is set aside for crypto and divide extensions.

### 4.7 MISC group (opcode 0x18)

Function in bits 15:0. Existing codes (all handled): TRAPB 0000, EXCB 0400, MB 4000, WMB 4400, FETCH 8000, FETCH\_M A000, RPCC C000, RC E000, ECB E800, RS F000, WH64 F800, WH64EN FC00. **\[VERIFY\]**

EV9 additions: **\[TENTATIVE\]**

| Code | Name | Behavior |
| --- | --- | --- |
| 4800 | RMB | Load-to-load and load-to-store ordering barrier |
| 5000 | SPECB | Speculation barrier: no younger instruction executes speculatively past it |
| 5400 | ICSYNC | Instruction-stream synchronization with prior stores on this hart |
| 6000 | CBCLEAN | Write the cache line at Rb back to memory, keep it cached |
| 6400 | CBINVAL | Discard the line. From U level it behaves as CBFLUSH |
| 6800 | CBFLUSH | Write back and invalidate the line |
| 6C00 | CBZERO | Set the 64-byte line to zero without reading it |

Other notes: TRAPB and EXCB are accepted and execute as no-ops, because arithmetic traps are precise (4.8). RPCC returns the low 32 bits as the cycle counter and the high 32 bits as an OS-set offset, as in Alpha. Cache operations require write permission on the page.

### 4.8 Floating-point control and exceptions

FPCR bits (position as in Alpha) **\[VERIFY\]**: 63 SUM, 62 INED, 61 UNFD, 60 UNDZ, 59:58 DYN rounding mode, 57 IOV, 56 INE, 55 UNF, 54 OVF, 53 DZE, 52 INV, 51 OVFD, 50 DZED, 49 INVD, 48 DNZ, 47 DNOD.

EV9 decisions: **\[FIXED\]**

- FP exceptions are precise. The faulting instruction's PC is reported and no younger instruction has modified state. Software completion (the /S qualifier) is accepted and ignored.
- Denormal inputs and results are handled in hardware. The trap-on-denormal behavior of early Alpha does not exist.
- FP instructions raise FP\_DISABLED when PSTATUS.FS is Off.

### 4.9 Reserved and unsupported

Reserved opcodes and reserved function codes raise ILLEGAL. VAX floating point (opcodes 0x15, 0x20, 0x21, 0x24, 0x25 and VAX variants in 0x14) raises ILLEGAL. In PAL mode (6.6), PALcode may emulate them in software.

## 5. Memory model

### 5.1 Model summary **\[FIXED\]**

EV9 is **weakly ordered and multi-copy atomic**: once a store is visible to any other hart, it is visible to all of them. Each hart appears to execute its own instructions in program order, but its memory accesses can be reordered as constrained below.

### 5.2 Preserved program order

Memory operation A precedes B (and no hart may observe the reverse) if A is before B in program order and any of these hold:

1. Same address: any two accesses to the same address stay in order, including two loads (per-location coherence).
2. Address dependency: B's address depends on a value loaded by A.
3. Data dependency: B is a store whose data depends on a value loaded by A.
4. Control dependency: B is a store and a branch that depends on A precedes it.
5. A or B has acquire or release ordering (AMO ordering bits), where an acquire orders itself before all later accesses and a release orders all earlier accesses before itself.
6. A barrier is between them: MB orders everything, WMB orders store before store, RMB orders load before load and load before store.
7. A is a load-locked and B is the matching store-conditional.

Everything else may be reordered. A load may be satisfied from the hart's own store buffer before the store is globally visible.

### 5.3 Instruction fetch

Instruction fetch is not coherent with stores. After modifying code, software executes ICSYNC (or CALL\_PAL IMB) on the writing hart and, on other harts, arranges a remote ICSYNC through KSI REMOTE\_ICACHE\_SYNC.

### 5.4 Load-locked and store-conditional

- The reservation granule is 64 bytes on all current parts (any granule from 16 to 128 bytes is legal).
- A store-conditional succeeds only if no other hart (or device) wrote the granule since the load-locked, and no trap or XRET occurred on this hart.
- Between a load-locked and its store-conditional, the code may contain only register operations and not-taken forward branches, for up to 32 instructions, to be guaranteed to eventually succeed. Other sequences may livelock.
- A store-conditional always clears the reservation, success or not.

### 5.5 Page-table and TLB ordering

Stores to page tables are visible to the table walker only after a barrier. A TLB invalidation (6.2) is required after any change to a valid mapping, and ordered after the table update with MB.

## 6. Privileged architecture

### 6.1 Levels

U (0), K (1), V (2, reserved, not implemented on any current part), F (3). A hart is in exactly one level. F is entered at reset. Every exception, interrupt and CALL\_PAL enters F, except IMB/RDUNIQUE/WRUNIQUE when the explicitly enabled fast path completes the call without a trap. Nothing is delegated to K. K is where an OS kernel runs: it differs from U in memory permissions and in which instructions are allowed. F runs the PAL image and firmware with translation off.

### 6.2 Privileged registers

Accessed by PR\_READ (0x19: Ra = register) and PR\_WRITE (0x1D: register = Rb), index in bits 15:0. Only F level can use these instructions, so the PAL image and firmware manage every register in the table, including those marked K, which hold the OS kernel's view of the machine. Executing them at U or K raises ILLEGAL, which is delivered to the PAL image. Access from a level below the register's level raises ILLEGAL.

| Index | Name | Level | Description |
| --- | --- | --- | --- |
| 000 | PHARTID | K | Hart ID (read-only) |
| 001 | PSTATUS | K | See below |
| 002 | PEPC | K | PC to return to |
| 003 | PCAUSE | K | Bit 63 = interrupt, low bits = code |
| 004 | PTVAL | K | Fault address, instruction bits or CALL\_PAL function |
| 005 | PVEC | K | Trap vector base (bit 0: 0 = direct, 1 = vectored for interrupts) |
| 006, 007 | PSCRATCH0, PSCRATCH1 | K | Free for the OS |
| 008 | PIE | K | Interrupt enable mask |
| 009 | PIP | K | Interrupt pending bits |
| 00A | PPTBRU | K | Root, granule and format of the lower-half page table (7.1) |
| 00B | PPTBRK | K | Root, granule and format of the upper-half page table |
| 00C | PASN | K | Current 16-bit address-space ID |
| 00D | PUNIQUE | K | Thread pointer (6.7) |
| 00E | PCOUNTEREN | K | Bits 0 to 2 let U read PTIME, PCYCLE, PINSTRET |
| 010 to 012 | PTIME, PCYCLE, PINSTRET | K, U if enabled | Read-only counters |
| 018 | PTLBIVA | K | Write a virtual address: invalidate that page for the current ASID |
| 019 | PTLBIASN | K | Write an ASID: invalidate all non-global entries for it |
| 01A | PTLBIALL | K | Any write: invalidate the whole TLB |
| 020 to 03F | Performance monitor | K | Event select, counters, overflow |
| 040 to 07F | Debug triggers | F | Breakpoint and watchpoint triggers |
| 100 | FSTATUS | F | Level, interrupt enable and previous state |
| 101 to 105 | FEPC, FCAUSE, FTVAL, FVEC, FSCRATCH | F | F-level trap state |
| 106 | FEDELEG | F | Reserved: delegation to K is not supported (reads 0) |
| 107 | FIDELEG | F | Reserved: delegation to K is not supported (reads 0) |
| 108 | FMODEL | F | 0 = firmware (reset state), 1 = PAL mode, 2 = full-PAL mode (platform spec 3.3) |
| 109 | FPALBASE | F | PALcode entry base in PAL mode |
| 10A | FCAP | F | Positive hardware capability bitmap: 1 means present. Low feature positions correspond to AMASK; bits 32 to 34 are defined in 6.9. Not filtered by the OS model |
| 10B | FPARTID | F | Read-only part ID: 1 Z1-lite, 2 21564, 3 Z1; unaffected by full-PAL OS feature hiding |
| 110 to 11F | FPMA0 to FPMA15 | F | Physical memory attribute regions (7.6) |

PSTATUS bits: 0 IE (interrupt enable for K), 1 PIE (previous IE), 2 PLVL (previous level, 0 = U, 1 = K), 4:3 FS (FP state: 0 off, 1 initial, 2 clean, 3 dirty), 5 SUM (K may access U pages), 6 MXR (loads may read executable-only pages). The current level is internal state, not a PSTATUS bit.

### 6.3 Traps

Synchronous exception codes (PCAUSE with bit 63 = 0):

| Code | Name | PTVAL |
| --- | --- | --- |
| 0 | ILLEGAL | Instruction bits |
| 1 | FP\_DISABLED | Instruction bits |
| 2 | INST\_ACCESS | Fault address |
| 3 | INST\_PAGE\_FAULT | Fault address |
| 4 | LOAD\_ACCESS | Fault address |
| 5 | LOAD\_PAGE\_FAULT | Fault address |
| 6 | STORE\_ACCESS (includes AMO) | Fault address |
| 7 | STORE\_PAGE\_FAULT (includes AMO) | Fault address |
| 8 | ALIGN | Fault address |
| 9 | ARITH | Exception summary (integer overflow, FP flags) |
| 10 | BREAKPOINT | PC or trigger ID |
| 11 | CALL\_PAL | Function number |
| 12 | KSI\_CALL | Reserved (not used: the OS reaches KSI through PAL calls) |

Interrupt codes (bit 63 = 1): 0 software, 1 timer, 2 external (from KAIA), 3 performance-counter overflow, 4 debug request. PIP and PIE use the same bit positions.

No delegation: every exception and interrupt is taken at F, through the vector table at FPALBASE (section 6.8), and the PAL image decides what the OS sees.

### 6.4 Trap entry and return

On a trap (always taken at F):

1. FEPC = PC of the faulting instruction (for interrupts, the next instruction to execute).
2. FCAUSE and FTVAL are set.
3. The previous level and interrupt-enable are saved in FSTATUS, then interrupts are masked.
4. The hart enters F at the entry in the vector table (section 6.8), or at FVEC in firmware mode.

XRET (opcode 0x1E), executed at F, restores the saved level and interrupt-enable from FSTATUS and sets PC to FEPC. Valid return levels are U, K, and F for a nested synchronous exception; V is reserved on current parts. Returning to F leaves translation off and interrupts masked. Exceptions are precise: all older instructions have completed and no younger instruction has changed architectural state. Interrupts are taken at an instruction boundary. Interrupts are masked while the hart is at F; synchronous faults at F can still occur. They are taken at U always, and at K when the kernel interrupt-enable is set. PAL must save FEPC/FCAUSE/FTVAL/FSTATUS before a faultable operation if it needs to preserve an outer trap context; no implicit hardware nesting stack is promised by this draft.

### 6.5 Reset and hart start

Out of reset a hart is at F with interrupts disabled, the MMU off (physical addresses), caches invalidated, FMODEL = 0 (firmware mode: traps go to FVEC), and PC at the boot vector (hart 0) or parked (other harts, released with KSI HART\_START). The reset state of every register is listed in the generated register map.

### 6.6 CALL\_PAL handling

Call-PAL function numbers 0x00 to 0x3F are privileged, 0x80 to 0xBF are unprivileged.

After the OS starts:

- **Hardware fast path.** When FFASTPAL bit 0 is set (section 6.9), the hardware executes IMB (86) as ICSYNC, RDUNIQUE (9E) by loading R0 from PUNIQUE, and WRUNIQUE (9F) by writing PUNIQUE from R16, all with no trap. The PAL image saves and restores PUNIQUE on context switch.
- **Everything else enters the PAL image** through the vector table (6.8): all other CALL\_PAL functions, and these three when FFASTPAL bit 0 is clear. The image decides what each does. BPT (80), BUGCHK (81), CALLSYS (83) and GENTRAP (AA) are the unprivileged calls that Alpha operating systems use for breakpoints, bug checks, system calls and generic traps.
- **Firmware services.** There is no hardware KSI call from K. The PPI recommends privileged CALL\_PAL 0x3F as the firmware service call, which the image forwards to KSI (Platform Specification, 3.5).

In PAL mode (FMODEL = 1) and full-PAL mode (FMODEL = 2), CALL\_PAL functions other than enabled fast-path calls, reserved opcodes (including VAX FP), and all exceptions and interrupts enter F through the vector table at FPALBASE (section 6.8). The loaded PAL image implements its OS-facing interface on top of the hardware. The PR instructions are F-level only. Full-PAL mode differs from PAL mode as described in section 6.9. In firmware mode (FMODEL = 0, the reset state), traps go to FVEC and no PAL image is involved; OS PAL fast paths are inactive.

### 6.7 Per-hart thread pointer

PUNIQUE is saved and restored by the PAL image on context switch. The hardware fast path for RDUNIQUE and WRUNIQUE keeps TLS access cheap, which matters because Alpha toolchains use them on every thread-local access. **\[FIXED\]**

### 6.8 PAL platform interface (PPI), version 1.0 **\[TENTATIVE\]**

The PPI is the hardware-facing contract of every PAL image. An image may implement any OS-facing PAL interface it likes, as long as it follows this contract. How images are packaged, verified, selected and upgraded is in the Platform Specification, section 7.3.

#### Registers for PAL images

| Index | Name | Description |
| --- | --- | --- |
| 10C | FPALID | Read-only: personality, version and SVN of the active image |
| 10D | FPALSIZE | Read-only: size of the PAL region |
| 10E | FPALINFO | Physical address of the PAL Info Block |
| 120 | FTLBW | Write: insert the TLB entry described by FTLBVA and the value written (for software TLB refill). Reported by FCAP bit 32 |
| 121 | FTLBVA | Write: virtual address, ASN, and instruction or data select for the next FTLBW |
| 140 to 147 | FSCRATCH0 to FSCRATCH7 | Eight scratch registers per hart for PAL use. FSCRATCH (index 105) is an alias for FSCRATCH0 |

All of these are F-level registers, and access from a lower level raises ILLEGAL.

#### Entry vector table

Offsets are from FPALBASE. Each slot is 64 bytes (16 instructions), and longer code jumps out of the slot.

| Offset | Event |
| --- | --- |
| 0x0000 | Reset and hart start |
| 0x0100 | Machine check and other asynchronous errors |
| 0x0200 | Interrupt (cause in FCAUSE) |
| 0x0300 | Synchronous exception (cause in FCAUSE, details in FTVAL) |
| 0x0400 | Reserved opcode, including VAX FP instructions, for software emulation |
| 0x2000 + 64 x function | Privileged CALL\_PAL, functions 0x00 to 0x3F |
| 0x3000 + 64 x (function - 0x80) | Unprivileged CALL\_PAL, functions 0x80 to 0xBF |

Hardware computes the CALL\_PAL target, so no software decoding is needed. The CALL\_PAL layout follows the original architecture so existing PAL sources port easily. **\[VERIFY\]** In PAL mode and full-PAL mode, IMB, RDUNIQUE and WRUNIQUE enter the vector like every other call, unless FFASTPAL bit 0 is set (section 6.9). The hardware then executes them directly, with no trap. Whether PAL mode sets that bit by default is **\[OPEN\]**.

#### State at entry (reset and hart start)

- The hart is at F level with translation off, interrupts masked, and caches coherent. FMODEL is the committed selection, 1 for PAL mode or 2 for full-PAL mode; FPALBASE is the load address. Pre-exit selection alone leaves FMODEL = 0. The handoff rules in the Platform Specification, section 7.3, apply before this entry state is established.
- R16 holds the physical address of the PAL Info Block, R17 the hart ID, and R18 the reason (0 first start, 1 hart start, 2 resume from sleep). All other registers are zero.

#### PAL Info Block

Built by firmware before the image runs, in F-only memory.

| Field | Meaning |
| --- | --- |
| ppi\_version | PPI version the firmware supports |
| features | Hardware feature bits (same layout as FCAP) |
| hart\_count, hart\_ids | The harts and their IDs |
| pal\_region\_base, pal\_region\_size | The PAL region |
| pal\_data\_base | Start of the per-hart F-only data (size comes from the image header) |
| kaia\_base, ktimer\_base, uart\_base | Interrupt controller, timer and firmware console |
| memory\_map | Pointer to the platform memory map |
| ksi\_table | Pointer to the firmware service entry, offering the KSI functions to F-level code |
| hwrpb, devicetree, tpm\_log | Pointers, zero if not used |
| os\_handoff | Physical pointer to the retained loader descriptor copied before ExitBootServices; its versioned contents are defined by the selected PAL personality |

#### What a PAL image must do

- Run only from its own region and its per-hart data, and touch no other firmware memory.
- Save and restore the interrupted level's state, and keep interrupts masked until it is ready for them.
- Handle machine checks without hanging: report through the firmware, then recover or reset.
- Support hart start and stop through the firmware service table.
- Use only the registers and instructions defined in this section and in 6.2 at F level.
- Return to the OS with XRET, which restores the level, interrupt enable and PC from FSTATUS and FEPC.

#### Access to OS memory

PLD and PST (opcodes 0x1B and 0x1F) take a mode field: physical, or virtual as the interrupted level. Virtual mode uses the OS's page tables and that level's permissions, and a fault is reported to the PAL as a nested exception. **\[TENTATIVE\]**

#### Compatibility rules

- The PPI major version changes only when an old image would break on a new part. Minor versions add fields and registers. Firmware refuses an image whose major version it does not support.
- The features an image needs are listed in its header, and firmware refuses an image that needs a feature the hardware lacks.
- Z1-lite, the 21564 and Z1 all implement PPI 1.x.

### 6.9 Full-PAL mode (FMODEL = 2) **\[TENTATIVE\]**

Full-PAL mode presents the classic Alpha machine: PALcode owns every privileged function, the CPU identifies itself as an EV7-class (21364) part, and the EV9 additions are invisible. SRM boots use it (Platform Specification, 3.3.1). Traps, interrupts and CALL\_PAL enter the PAL image exactly as in PAL mode (section 6.6, vector table in 6.8).

#### Differences from PAL mode

| Item | PAL mode (FMODEL = 1) | Full-PAL mode (FMODEL = 2) |
| --- | --- | --- |
| EV9 instructions (AMO group, BITM group, RMB, SPECB, ICSYNC, cache operations) | Available when AMASK reports them | Hidden at K/U: reserved-opcode vector (0x0400). Remain available to F-level PAL and firmware |
| IMPLVER and AMASK at K/U | 9, full feature mask | 3 (EV7), legacy mask: bits 0, 1, 2 and 8 cleared **\[VERIFY\]** against the EV7 documentation. F-level discovery still reports the physical EV9 capabilities |
| Translation granules | 4, 8 and 16 KB | 8 KB with the legacy page-table format only. The granule fields in the root registers are ignored |
| Hardware page walker | All native granules and legacy 8 KB on every current part; enabled unless the image requires supported software refill | Legacy format only; enabled unless the image requires supported software refill. PAL may change FWALK subject to FCAP bit 32 |
| SMT | Allowed if the part has it | Off: one hart per core. Other harts stay parked and are not listed in the PAL Info Block |
| Virtualization | The V level can be used by PAL on a part that implements it | Disabled |
| Hardware fast path for IMB, RDUNIQUE, WRUNIQUE | Initial value from image flag bit 4; PAL may change it | On at model activation; PAL may clear it |
| Firmware runtime services | Available through a personality-provided PAL/KSI bridge | Same underlying services. A UEFI-aware personality may expose the bridge; a classic SRM personality may expose only SRM callbacks |

#### Registers and feature bits

| Index | Name | Description |
| --- | --- | --- |
| 10F | FFASTPAL | Bit 0: hardware fast path for IMB, RDUNIQUE and WRUNIQUE. Reset value 0 in firmware mode. At activation: 1 in full-PAL mode, image flag bit 4 in PAL mode. PAL may subsequently change it and saves/restores PUNIQUE on context switch |
| 122 | FWALK | Bit 0: hardware page walker enable, reset value 1. Current parts implement native 4/8/16 KB and legacy 8 KB formats. May be cleared only if FCAP bit 32 reports software refill; clearing it makes TLB misses trap to PAL rather than walk |

New FCAP bits: 32 software TLB refill (FTLBW) present; 33 hardware walker handles the legacy 8 KB format; 34 hypervisor level implemented. Bit 33 MUST be 1 on Z1-lite, the 21564, Z1 and the QEMU reference. Bit 32 is optional and controls acceptance of images requiring software refill. Bit 34 is 0 on all current parts. An image requiring software refill enters with FWALK = 0; other images enter with FWALK = 1. All reserved capability bits are zero.

#### Rules

1. **Fixed for the boot.** MODEL\_SELECT(full-pal) validates and locks a pending full-PAL selection before ExitBootServices. FMODEL stays 0 during UEFI; the retained Enter handoff commits it to 2 only after successful exit (Platform Specification, 7.3). A standalone SRM boot commits only after its own firmware-ownership handoff. The image must declare full-PAL support (header flag bit 6) and legacy format (bit 1), or selection fails. The active model changes only at reset.
2. **One gate hides the EV9 additions from the OS.** EXT\_HIDE is active at K/U when FMODEL = 2. It makes decode treat opcodes 0x04 and 0x05 and the EV9 MISC function codes as reserved opcodes, and makes IMPLVER and AMASK report the legacy values there. F-level PAL and firmware retain the EV9 instruction set and true capability discovery, so the implementation does not remove hardware assistance from PAL itself.
3. **Privileged instructions** (PR\_READ, PR\_WRITE, XRET, PLD, PST) work only at F level, as in PAL mode.
4. **Virtualization is disabled.** The V level, two-stage translation and guest interrupt files are off, and no register or instruction belonging to them can be reached. The IOMMU runs single-stage, for DMA protection only.
5. **Translation.** Every root register is treated as 8 KB with LEGACY set. The legacy walker is mandatory on current parts and is enabled unless the image explicitly requires supported software refill. PAL may subsequently clear FWALK and use FTLBW only when FCAP bit 32 is set. These translation controls affect K/U; F remains physically addressed.
6. **Hardware kept in every model** stays available: the page walker, the configurable fast path, and shared-register PAL entry. Entry has no mandatory register-bank swap; ordinary-trap cost is a design target, not an architectural cycle bound.
7. **Testing before OpenSRM exists:** use a small UEFI test loader and the QEMU option machine kotl-ev9,model=full-pal,pal-image=FILE.

## 7. Memory management

### 7.1 Address spaces

- 48-bit virtual addresses, canonical: bits 63:48 must equal bit 47. Anything else raises a page fault (access fault for instruction fetch).
- The lower half (bit 47 = 0) is translated through PPTBRU, the upper half (bit 47 = 1) through PPTBRK. Each half has 47 usable bits.
- Physical addresses are 48 bits. Current parts implement 44; bits above that must be zero in a PTE or the walk faults.
- The translation granule is **4 KB, 8 KB or 16 KB**, chosen separately for each half by its root register. 16 KB is the default for new software, native 8 KB is also available, and 4 KB supports software that assumes 4 KB pages. A half uses one granule at a time. Root register layout: bits 47:12 hold the physical root address; bits 1:0 are GRANULE (0 = 16 KB, 1 = 4 KB, 2 = 8 KB, 3 reserved); bit 2 is LEGACY (8 KB only, section 7.4); all other bits are reserved and zero. Native roots are aligned to their granule. Legacy roots address a half-table and require 4 KB alignment, as specified in 7.4. Every current part implements all three native granules and the legacy format. AMASK bits 19 to 21 report native granules in PAL mode. **\[FIXED\]**
- Translation is off at F level and in reset. At K and U it is always on.

### 7.2 Page-table format

Every table is exactly one granule in size and holds 8-byte entries. Levels are numbered from 0 (the leaf level, mapping one granule) up to the root. A leaf entry may appear at any level, which gives the page sizes below.

| Granule | Offset bits | Levels | Index bits, root first | Page sizes (level 0 upward) |
| --- | --- | --- | --- | --- |
| 4 KB | 12 | 4 | 8, 9, 9, 9 | 4 KB, 2 MB, 1 GB, 512 GB |
| 8 KB | 13 | 4 | 4, 10, 10, 10 | 8 KB, 8 MB, 8 GB, 8 TB |
| 16 KB | 14 | 3 | 11, 11, 11 | 16 KB, 32 MB, 64 GB |

The root table of the 4 KB granule has 256 entries and that of the 8 KB granule has 16, so those root tables are only partly used. For example, the 16 KB granule indexes virtual address bits 46:36, 35:25 and 24:14.

PTE layout (the same for every granule): bits 47:12 hold physical address bits 47:12 of the page or of the next table, so no shifting is needed. For granules larger than 4 KB the low bits of this field (bit 12 for 8 KB, bits 13:12 for 16 KB) must be zero. Bits 11:0 are flags, bit 63 is the contiguous hint, and bits 62:48 are reserved and must be zero.

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | V | Valid |
| 1 | R | Readable |
| 2 | W | Writable |
| 3 | X | Executable |
| 4 | U | User-accessible |
| 5 | G | Global (ignores ASID) |
| 6 | A | Accessed |
| 7 | D | Dirty |
| 9:8 | ATTR | 0 normal cacheable, 1 device (strongly ordered), 2 write-combining, 3 non-cacheable |
| 11:10 | RSW | Free for software |
| 63 | C | Contiguous hint (a naturally aligned group of 16 entries maps one region 16 times the page size) |

A PTE with V = 1 and R = W = X = 0 points to the next level. W = 1 with R = 0 is reserved (page fault).

### 7.3 Walk algorithm and faults

1. Check canonical form. Pick the root register from bit 47.
2. At each level, read the PTE (physical access through the cache hierarchy). If V = 0, or reserved bits are set, or the entry is reserved: page fault. If it is a pointer and we are at level 0: page fault. If it is a leaf, stop.
3. Check permissions: U bit against the current level (K may touch U pages only with PSTATUS.SUM and never executes them), R/W/X against the access (R or, with MXR, X for loads), alignment of a large-page leaf.
4. If A is 0, or D is 0 on a store, the walker sets the bit with an atomic read-modify-write of the PTE. If the PTE is not in atomic-capable memory it raises a page fault instead. **\[FIXED\]**
5. Physical memory attributes (7.6) are checked on the final address: failure raises an access fault.

The fault type (instruction, load, store) selects the cause code. PTVAL holds the faulting virtual address.

### 7.4 Legacy 8 KB format (PAL mode)

**Legacy format (PAL mode or full-PAL mode).** Selected by LEGACY with the 8 KB granule in PAL mode, and forced by full-PAL mode. The original single-root address decomposition is 3 levels of 10 bits and a 13-bit offset, with 43-bit canonical virtual addresses (bits 63:43 equal bit 42). This is distinct from the native 8 KB format's four levels and 48-bit canonical addresses. It uses the original Alpha PTE layout: valid, fault-on-read/write/execute, ASM, granularity hint, per-mode read/write enables, and PFN in the high 32 bits. The exact PTE map remains **\[VERIFY\]** against the original architecture reference and generated register map.

The legacy hardware walker is mandatory on every current part. To adapt the
original top-level table to two roots, PPTBRU points at its lower 512 entries and
PPTBRK at its upper 512 entries (the original 8 KB table base plus 4 KB). In this
format only, root addresses require 4 KB alignment rather than 8 KB. After
checking legacy canonical form, bit 42 selects the half; the walker indexes
VA[41:33] within that half, then VA[32:23] and VA[22:13] in the two full 8 KB
tables. The canonical sign extension makes this half selection agree with bit
47. A base-page PFN is shifted by 13. Native-format A/D flags MUST NOT be written
into legacy PTEs. Legacy permission/fault semantics follow its own PTE format.
Where FTLBW is present, PAL may choose software refill by clearing FWALK; the
OS-visible translation format and permissions remain the same.

### 7.5 TLBs and address-space IDs

- Entries are tagged with the 16-bit ASN, a global flag, the granule (4, 8 or 16 KB) and the page size. A global entry matches any ASN.
- The three PTLBI registers (6.2) are the only invalidation mechanism. Remote invalidation is a KSI call (REMOTE\_FENCE).
- Software may change PASN without flushing. When ASNs wrap, the OS issues PTLBIALL.
- Changing a root register's granule requires PTLBIALL before the new tables are used.
- The TLB may cache any valid translation, including one a later store has made invalid, until software invalidates it.

### 7.6 Physical memory attributes

F-level registers FPMA0 to FPMA15 each define a region: base, size (power of two,
64 KB minimum), permitted privilege levels, and attributes: readable, writable,
executable, cacheable, atomic-capable, device ordering. An F-only region denies
every K/U access, including accesses through aliases, legacy mappings and
software-filled TLB entries. Physical PLD/PST still obey the region's F-level
permissions; interrupted-level virtual accesses obey the interrupted level's
permissions. DMA isolation must also exclude retained firmware/PAL regions in
the IOMMU. Accesses outside every enabled region raise an access fault.

Firmware programs regions from the platform memory map. If regions overlap,
all matching regions must permit the access, and the most restrictive memory
attributes apply; an overlap cannot grant access denied by another region.
Active PAL code is F-only, readable/executable and non-writable; its per-hart
data is F-only, readable/writable and non-executable. Retained firmware is
protected separately. The PTE ATTR field can only restrict the physical-region
attributes, never relax them. The binary region-descriptor encoding, including
the privilege mask, remains **\[OPEN\]** and must be fixed before RTL freeze.

# Part II: Z1-lite (test core) microarchitecture and RTL plan

## 8. Z1-lite microarchitecture

### 8.1 Configurations

One RTL generator produces every configuration from parameters. The architecture in Part I is identical in all of them. Sizes below are design targets, not synthesis results. **\[TENTATIVE\]**

| Config | Cores x harts | L1 I / D | L2 | L3 | DRAM | I/O | Target |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Z1-lite-1 | 1 x 1 | 16 KB / 16 KB | 256 KB | none | DDR3/4, 32 or 64 bit | UART, SD, Ethernet | FPGA, 50 to 100 MHz |
| Z1-lite-2 | 2 x 1 | 32 KB / 32 KB | 512 KB shared | none | DDR3/4, 64 bit | adds xHCI | FPGA |
| Z1-lite-4 | 4 x 1 | 32 KB / 32 KB | 512 KB per core | none (directory at memory side) | DDR3/4, 64 bit | as above | Large FPGA |
| Z1-lite-2T | 2 x 2 | 32 KB / 32 KB | 512 KB shared | none | DDR3/4, 64 bit | as above | FPGA, tests the SMT rules |

Z1-lite exists only for testing and is never a product. The 21564 (base processor) and Z1 (middle range) are out-of-order parts described in the 21564 document and the Platform Specification, section 1.1.

All L1 caches use 64-byte lines and are 8-way at 32 KB and 4-way at 16 KB, so each way is 4 KB. With a virtually indexed, physically tagged L1 this keeps the index inside the page offset of the smallest (4 KB) granule, so no alias handling is needed at any granule.

### 8.2 Pipeline

Eight stages, in order, 2-wide, with issue decided per hart.

| Stage | Name | Work |
| --- | --- | --- |
| 1 | IF1 | Choose the hart, send PC to I-cache and ITLB, look up the branch predictor |
| 2 | IF2 | I-cache tag compare, read 16 bytes (4 instructions) into that hart's fetch buffer |
| 3 | PD | Predecode and align, decode direct branch targets, redirect early on a predicted-taken branch |
| 4 | ID | Decode, form issue pairs, check the scoreboard, assign slots |
| 5 | RR | Register read and bypass selection |
| 6 | EX | ALU, shifter, branch resolve, AGU, start multiply, FP stage 1 |
| 7 | MEM | D-cache tag and data, DTLB, store-buffer lookup, fault checks, later FP stages |
| 8 | WB | Register write, commit, trap check |

Target result latencies, in cycles from the producing instruction's EX: integer ALU, shifts, byte manipulation, CMOV 1; CTLZ, CTTZ, CTPOP 2; MULL 4, MULQ 5, UMULH 6 (pipelined); load hit 3; FP add, subtract, multiply, convert 4; FP compare 2; DIVS 12, DIVT 20, SQRTS 14, SQRTT 24 (not pipelined); integer to FP and FP to integer moves 2. **\[TENTATIVE\]**

Issue reserves execution, return-buffer and completion-queue capacity before
dispatch. An instruction without those credits stays in ID; memory misses and
iterative units wait locally rather than creating a global combinational stall
path. Variable-latency results carry a completion tag and enter buffered return
ports. A cache miss keeps its destination busy until the data or fault returns.

**Precise retirement is required even in this in-order core.** Each hart has a
parameterized in-order completion queue allocated at issue. Fixed and variable
latency instructions record their result or fault there; up to two oldest ready
instructions retire in order at WB/commit. Architectural register writes and
store visibility occur only at retirement. Completed results can bypass to
dependent instructions before retirement, but the scoreboard prevents a younger
writer from overwriting an in-flight destination. A fault or interrupt is taken
at a retirement boundary and discards younger queued results and uncommitted
stores. Cancelled completion tags are invalidated so a late response cannot
write a reused queue entry. This does not require out-of-order issue or register renaming, but it
does require retaining results until older faulting operations are resolved.

### 8.3 Issue, pairing and slots

Per hart, ID holds a 4-entry decode queue. Each cycle ID tries to issue the two oldest instructions of the selected hart.

| Slot | Executes |
| --- | --- |
| A | Full integer ALU (adds, logic, shifts, byte manipulation, compares, CMOV, CIX, MVI, BITM), branches and jumps, integer multiply, FP operations, privileged register access |
| B | Simple integer ALU (add, subtract, scaled add, logic, compares, LDA, LDAH), address generation and the load/store port |

Pairing rules:

1. Two instructions issue together only if each fits a slot (the older one may take either slot).
2. No dependency of the younger instruction on the older one inside a pair.
3. At most one memory operation, one branch, one FP operation and one multiply per cycle.
4. Serializing instructions issue alone, after older instructions complete, and block younger ones of the same hart until they finish: PR\_WRITE, XRET, CALL\_PAL, MB, WMB, RMB, SPECB, ICSYNC, cache operations, AMO, LL, SC.
5. The scoreboard holds one entry per architectural register per hart. A ready bit and producer tag track variable-latency operations; fixed-latency readiness may also use a cycle counter. Sources must be ready or available from bypass, and a destination with an older in-flight writer blocks issue. Exception status is retained in the completion queue until retirement.

### 8.4 Front end

- **Fetch:** one aligned 16-byte group per cycle for one hart. The hart is chosen round-robin among harts whose fetch buffer has room. A taken branch in the group discards later slots.
- **Predictor:** 256-entry 2-way BTB with type bits; 8K two-bit counters indexed by PC and a 12-bit global history (history register per hart, tables shared); a 16-entry return stack per hart, driven by the JSR type bits. A misprediction found in EX costs about 5 cycles.
- **I-cache misses** request the line from L2 and prefetch the next line. Wrong-path fetches are cancelled per hart.
- **Translation:** ITLB of 32 fully associative entries.

### 8.5 Register files and bypass

- Integer file: 32 registers per hart, 64 bits, 6 read and 3 write ports (two issue slots, plus one for load and multiply returns). R31 is not stored.
- FP file: 32 per hart, 4 read and 2 write ports (one FP operation per cycle, plus store data and a load return).
- On FPGA the multi-port files are built from replicated LUT RAM with a live-value table. Port counts are the main area cost, so they are parameters of the generator.
- Bypass paths: EX to EX, MEM to EX, WB to RR, and completed queue results to consumers. Execution results first enter the completion queue; only retirement writes architectural files. Fixed-latency return slots may be reserved at issue. Unpredictable load returns use tagged buffers and fair arbitration, so a port collision delays a result without losing it or exposing a younger architectural write.

### 8.6 Load/store unit

- **One memory operation per cycle.** D-cache 32 KB, 8-way (16 KB, 4-way on Z1-lite-1), write-back, write-allocate, non-blocking, with 8 miss-status registers (at least 2 reserved per hart).
- **Store buffer:** 8 entries per hart, drained in order, merging adjacent committed stores to the same line. Unretired stores retain their address/data/fault state in the completion machinery and cannot become externally visible. Loads may forward from an older same-hart store when it fully covers the load and its address/data are known; a partial overlap waits.
- **Byte and word access (BWX):** byte enables in the cache write path, load alignment and sign-extension in the data return path. Accesses that cross a line are split into two requests.
- **Ordering in Z1-lite is stronger than the architecture requires.** Issue is in order, stores drain in order, and a miss never lets a younger access to the same line bypass it. As a result, dependency orderings in 5.2 hold automatically. The barriers are implemented as follows: MB waits for the store buffer to drain and all outstanding loads to return; WMB is a no-op on Z1-lite (the buffer already drains in order); RMB blocks younger memory operations until older loads have returned; release waits as MB does for older accesses; acquire blocks younger memory operations until its own load returns.
- **LL/SC:** one reservation register per hart (line address and valid bit), cleared by an invalidating snoop, an eviction of the line, any trap or XRET, and any store-conditional.
- **AMO:** executed at the retirement head after older accesses and faults are resolved, in the L1 data cache once the line is held exclusively, using an adder, logic unit and comparator in the LSU. Its memory update and architectural completion form one retirement operation; interrupts cannot split them. CAS128 reads and writes two registers over two cycles and is serializing. AMOs to memory that is not cacheable raise STORE\_ACCESS on all current parts.
- **Device accesses:** MMIO reads and writes execute only at the retirement head, after older required memory operations complete. They are never speculative or replayed merely to repair a pipeline hazard; buffering must not duplicate externally visible device effects.

### 8.7 Address translation hardware

- DTLB: 64 fully associative entries. Shared second-level TLB: 512 entries, 4-way, holding base-granule pages of any granule (each entry is tagged with its granule). Larger pages live in the L1 TLBs and in a 16-entry superpage TLB.
- Page-walk cache of 16 entries holding upper-level PTEs.
- One hardware walker per core, shared by the harts, first-come first-served. PTE reads go through the core's L2. Setting the A or D bit takes the line exclusively with a read-unique request and writes it back through the cache, which makes the update atomic.
- The walker state machine is parameterized by granule: 9 index bits per level for 4 KB, 10 for 8 KB and 11 for 16 KB (the root level may use fewer), with each root register selecting which.

### 8.8 Floating-point unit

- Pipelined adder and multiplier (4 stages) shared by S and T formats, a compare and convert block, and one iterative divide and square-root unit (radix-4).
- FP exceptions are precise through the per-hart completion queue (8.2), regardless of completion order. If no overflow/underflow/inexact trap is enabled, divide and square root may execute non-blockingly; invalid and divide-by-zero can be detected from operands at issue. If any of those traps is enabled, divide/square root is serializing to limit resource pressure. Serialization is an implementation policy, not a substitute for precise retirement. **\[FIXED\]**
- Add, subtract, multiply and convert attach exception status to their completion entries. Younger results, including fast integer results, must not update architectural registers or release stores ahead of an unresolved older FP instruction; flushing alone cannot undo a write that already became architectural.
- Verify against Berkeley SoftFloat and TestFloat for all five rounding behaviors.

### 8.9 Multithreading (SMT) rules

| Per hart (replicated) | Shared by the harts in a core |
| --- | --- |
| PC, fetch buffer, decode queue | Pipeline stages and issue slots |
| Register files, scoreboard | ALUs, multiplier, FPU, AGU |
| Global history, return stack | Caches, TLBs, walker, BTB and counter tables |
| Store buffer, reservation | Miss-status registers (partitioned) |
| Privileged registers, FPCR, PUNIQUE | L2 and everything beyond |

- A flush (trap, mispredict, XRET) only affects the hart that caused it.
- A serializing instruction blocks only its own hart.
- Core-wide operations (PTLBI, cache operations on shared lines) take effect for both harts at one point in time.
- Thread select: prefer the hart with fewer instructions in flight, with a starvation counter that forces the other hart after 32 consecutive losses.
- Most Z1-lite configurations have one hart; the generator must produce a working core with the second hart parameter off.

### 8.10 Caches and coherence

- L2 organization follows the selected configuration in 8.1: shared on Z1-lite-2/2T and private on Z1-lite-4 and the 21564. It is inclusive of the L1s it serves, with presence bits so probes reach only relevant L1s.
- Current Z1-lite presets have no L3. Where configured for the 21564, L3 is shared, 16-way, split into four slices interleaved by line address, and non-inclusive. Each slice carries a directory (snoop filter) with a presence vector and state for lines cached in private L2s.
- Protocol: directory-based MESI with five message channels (Acquire, Probe, Release, Grant, GrantAck) on separate virtual channels so that no cycle of dependencies can deadlock. TileLink-C is recommended because it already matches this structure and has open-source infrastructure. **\[OPEN\]**
- Interconnect: crossbar between the configured tiles/L2s, any configured L3 slices, the memory controller and the I/O bridge. Peripherals sit behind a TileLink-to-AXI4 bridge. DMA devices attach coherently through the IOMMU.
- Without an L3 (Z1-lite), the directory lives next to the memory controller.

### 8.11 Core boundary signals

- Interrupts from KAIA per hart: external, software, timer, performance, debug request.
- Hart controls: reset, halt request and acknowledge, parked/running status.
- Debug: per hart, through the debug module (8.12).
- Trace: one retire record per instruction (10.2).

### 8.12 Debug

- JTAG test access port and a debug module with abstract commands (halt, resume, single-step, register and memory access) modeled on the open RISC-V debug architecture, adapted for Alpha registers. A small debug ROM and program buffer execute at F level.
- Four address or data triggers per hart with actions: raise BREAKPOINT or halt into debug.
- Two fixed counters per hart (cycles and retired instructions), plus at least four programmable event counters. The programmable selectors include branch mispredicts, I/D cache misses and TLB misses, consistent with the Platform Specification's minimum of four programmable counters in addition to cycle/instruction counts.

## 9. RTL organization

### 9.1 Language and style **\[OPEN\]**

Recommended: Chisel with the TileLink library, for parameterization and reuse of open infrastructure (interconnect, debug module, peripherals), generating Verilog that Verilator and vendor tools consume. SpinalHDL or SystemVerilog are acceptable if every rule below is kept.

- One generator, config objects for every row of 8.1. No hand-edited generated Verilog.
- Synchronous design, one clock for the core and uncore. Reset is synchronous, asserted asynchronously and released through a synchronizer.
- Every register has a defined reset value or a comment saying why not. No latches, no tri-states inside the core.
- Memories go through a wrapper that infers BRAM on FPGA and a macro on silicon. All FPGA-specific code (clock manager, DDR controller, PCIe bridge IP, transceivers) lives in a separate fpga directory, and the core builds without it.
- Lint (Verilator) and a formal-friendly coding style are enforced in CI.

### 9.2 Module hierarchy

| Block | Contents |
| --- | --- |
| KotlSoC | Tiles, uncore, peripherals, clocks and resets |
| KotlTile (per core) | Core, L1 I and D, TLBs and walker, private L2, bridge to the interconnect |
| KotlCore | Frontend (fetch, predictor, fetch buffers), Decode and pairing, Scoreboard, Register files, ExecA (ALU, shifter, byte manipulation, branch, multiplier, privileged-register unit), ExecB (ALU, AGU), LSU (store buffer, miss registers, LL/SC, AMO), FPU, per-hart completion queues, Commit and trap logic, Privileged register file, SMT arbiter |
| Uncore | L3 slices with directory, crossbar, memory controller wrapper, I/O bridge, KAIA, KTIMER, system controller, IOMMU, debug module |
| Peripherals | PCIe root complex wrapper, xHCI, SATA, Ethernet, SD, UART, SPI, I2C, GPIO per the Platform Specification |

### 9.3 Interface rules

- Every interface is a ready/valid handshake with a documented bundle. Interfaces that cross a block boundary are defined once, in one source file.
- Core to L1: instruction fetch (address, hart, response with 16 bytes and fault), data (address, size, operation, hart, tag, data, byte enables, and a response channel that may return out of order by tag).
- Tile to interconnect: TileLink-C or its equivalent, with the physical address width as a parameter.
- The Platform Specification memory map is a generated constants file used by RTL, QEMU, firmware, and the devicetree.

### 9.4 Clock and reset

| Domain | Clock | Notes |
| --- | --- | --- |
| Core and uncore | Single (FPGA 50 to 100 MHz) | All cores, caches, interconnect |
| DRAM | Controller's clock | Async FIFO to the core domain |
| PCIe, USB, Ethernet | Their own clocks | Async FIFOs at the boundary only |

Reset sequence: power/clocks stable, then release the boot ROM, flash/SRAM path,
system controller, UART and memory-controller configuration interface. Release
hart 0 at F with translation off while DRAM is still unavailable. Stage 1 runs
from flash/SRAM, trains DRAM, then enables calibrated DRAM traffic and the
remaining coherence/I/O fabric. Other harts remain parked until firmware
initialization, model/topology selection and KSI HART\_START permit release.
Requiring DRAM calibration before hart 0 starts would deadlock firmware training.

## 10. Verification

### 10.1 Models

Three models of the same architecture: the Sail model (golden, slow), QEMU kotl-ev9 (fast, runs real software) and the RTL. Instruction tables and register maps are generated once from the Sail model and shared by the decoder, the assembler tests and the documentation.

### 10.2 Retire trace and co-simulation

The core emits one record per retired instruction: hart ID, PC, instruction bits, destination register and value, memory address, size and store data, trap cause if any, privilege level after retirement. Verilator runs compare each record against the model.

Events the model cannot predict are supplied by the RTL side: the point where an interrupt is taken, the data returned by device reads, the outcome of a store-conditional, and the timing-dependent result of A and D bit updates. The comparator accepts the RTL's choice if the model can legally produce it.

### 10.3 Test levels

| Level | What |
| --- | --- |
| Unit | ALU, shifter, byte manipulation, multiplier against a C reference on exhaustive and random inputs. FPU against SoftFloat and TestFloat |
| Block | Cache and store buffer with random traffic and a checker; TLB and walker against a model; predictor and fetch with random branch streams |
| Instruction | One directed test per instruction and per trap cause, plus a constrained random generator that obeys Alpha legality rules |
| Privileged | Trap entry/nesting/return, rejection of delegation and lower-level PR access, interrupts, both PAL models, full-PAL K/U feature hiding, FPMA isolation, all three native granules, legacy walking and optional software refill |
| Memory model | Litmus tests run on multiple cores with randomized delays, outcomes compared against the formal model |
| Formal | Instruction-level properties on the retire interface; coherence protocol checked for deadlock and safety in a model checker |
| System | Firmware, UEFI shell, Linux boot, stress and soak tests on FPGA |

### 10.4 Exit criteria

- Every instruction and every trap cause hit by a directed test and covered by random tests.
- Functional coverage for instruction pairs, hazard types, branch types, SMT interleavings, and traps taken during each pipeline condition.
- No open P0 or P1 bug. Random regressions pass N consecutive nights without a mismatch.

## 11. FPGA targets

- **Budgets** are constraints for the design, not predictions: Z1-lite-1 within about 60k LUTs and 4 Mbit of block RAM, Z1-lite-2 within about 120k LUTs and 8 Mbit, Z1-lite-4 within about 200k LUTs and 14 Mbit. Replace with measured numbers after the first synthesis. **\[TENTATIVE\]**
- **Boards:** Kria KV260 class for Z1-lite-1 (its ARM cores can serve as the chipset for early bring-up). A Kintex-7 XC7K325T board for Z1-lite-2 and, if it fits, Z1-lite-4. The 21564 and Z1 do not fit any low-cost FPGA.
- **Timing:** the likely critical paths are register-file read plus bypass, D-cache tag compare, and FPU stages. Retime early and keep ports parameterized.
- **Peripherals first:** UART, SD card, Ethernet, DDR. PCIe and NVMe wait for a board that exposes a host slot or M.2 connector.
- **Licences:** check which vendor tool edition supports the chosen part before buying a board.

## 12. Milestones and exit criteria

| Milestone | Deliverable | Exit criteria |
| --- | --- | --- |
| M0 | Repository, CI, Sail skeleton, generated tables | Decoder tables build; CI runs lint |
| M1 | Single-hart integer core in simulation | Integer directed and random tests match the model |
| M2 | Multiply, FP, BWX, CIX, MVI | TestFloat clean; all base instruction tests pass |
| M3 | Traps, privilege levels, MMU, all three native granules and legacy format | Privileged test suite passes; UEFI hello world runs at F with translation off; pre-exit PAL selection and post-exit handoff tests pass |
| M4 | Z1-lite-1 on FPGA | Boots firmware and Linux on a PAL image (PAL or full-PAL mode) to a shell, from SD |
| M5 | Two cores, coherence | Litmus suite matches the model; SMP Linux boots |
| M6 | Four cores | Multi-hour stress run without failure |
| M7 | SMT | Both-hart tests and fairness tests pass |
| M8 | 21564 platform and tapeout decision (21564 document, section 6) | Platform spec devices verified; decision review |

## 13. Open items, errata and deliverables

### 13.1 Open items

1. Legacy PTE bit-map verification and exact optional FTLBW/FTLBVA encoding. The legacy hardware walker itself is required; software refill is an optional PAL choice (7.4).
2. Mixed granules inside one address-space half (platform spec 3.4).
3. HDL choice and TileLink adoption (8.10, 9.1).
4. Bus errors on fills: reported as an asynchronous interrupt or a precise fault. **\[OPEN\]**
5. Integer divide extension (4.3), vector extension, and crypto extension.
6. Exact FP function-code tables and qualifier handling, generated from the Sail model.
7. L3 inclusion policy and directory size once real traffic is measured.
8. Whether Z1-lite-4 fits the target FPGA.
9. Every item tagged VERIFY in Part I must be checked against binutils, QEMU, and test vectors before the decoder is frozen.

### 13.2 Deliverables before RTL freeze

- Sail model of the full ISA and privileged architecture, with generated decoder, register map and function tables.
- Retire trace specification (10.2) with its data format.
- Interface definitions (9.3) and generated memory-map constants.
- Configuration presets (8.1) and their verification plans.
- Litmus test suite and its formal model.
- Errata log, started on day one.

### 13.3 Resolved consistency decisions, 28 November 2026

- MODEL\_SELECT/Activate prepares and locks a verified image before
  ExitBootServices; a retained Enter entry commits the model after successful
  exit and enters PAL before the kernel. No Boot Services protocol is called
  after exit (Platform Specification, 7.3).
- F-level firmware is physically addressed. EFI's 4 KB allocation unit is
  independent of the OS translation granule; retained runtime allocations
  remain 16 KB aligned (Platform Specification, 7.1).
- All current parts provide native 4/8/16 KB and legacy 8 KB hardware walking.
  Software refill is an optional image choice gated by FCAP, not a replacement
  for the required legacy walker (6.9, 7.4).
- Full-PAL hides EV9 extensions at K/U while retaining F-level hardware
  assistance. Runtime exposure is a PAL-personality bridge decision, not a
  hardware removal of firmware services (6.9).
- Canonical PAL hashing zeroes the digest and signature-location/size fields
  before hashing, with detached signature bytes outside the digest input
  (Platform Specification, 7.3).
- The 21564 has 20 Gen 5 device lanes plus its Gen 4 x4 PCH link, not 24 Gen 5
  device lanes plus another link. Hart 0 starts before DRAM calibration so
  stage 1 can perform that calibration (21564 Specification, 2.13 and 3.3).
- Z1-lite retires through an in-order completion queue so delayed loads/FP
  instructions cannot fault after younger results became architectural (8.2).
