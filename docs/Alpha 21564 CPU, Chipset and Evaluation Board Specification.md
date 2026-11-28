# Alpha 21564 CPU, Chipset and Evaluation Board Specification

Draft 0.1 · 25 November 2026 · Companion to the Kotl EV9 Platform Specification and the Kotl EV9 Architecture and RTL Specification

Consistency revision · 28 November 2026: both native and legacy page walking
are required; PAL selection and final handoff follow the Platform Specification,
section 7.3. Firmware remains physically addressed at F until that transfer.

## 0. Status, naming and conventions

- **What this is.** The architecture (instruction set, memory model, privileged architecture, MMU with 4, 8 and 16 KB granules) is defined in the EV9 Architecture Specification, Part I, and is not repeated here. This document defines one implementation of it, the **21564**, together with its companion chipset and an evaluation board. In the original Alpha numbering (21064, 21164, 21264, 21364, 21464) the 21564 is the EV9 generation.
- **Product family.** The 21564 is the base processor. Z1 (middle range) and later parts are built from it by adding tiles and uncore (Platform Specification, 1.1). Z1-lite (in-order, FPGA-friendly) is the test-only core used to bring up software and firmware before silicon. All parts run the same software, firmware and platform. The two sockets are Socket 21580 (desktop and workstation, up to 32 cores) and Socket 21581 (server, up to 128 cores). The 21564 uses Socket 21580.
- **Naming note.** Alpha, AXP and the 21xxx part numbers originate with DEC, Compaq and HP. Treat Alpha 21564 as a working name and get a legal review before it appears on a product.
- Tags follow the other documents: **\[FIXED\]** decided, **\[TENTATIVE\]** a default that may change, **\[OPEN\]** undecided. Every number below is a design target until a performance model and later synthesis confirm it.

## 1. Goals and design rules

### 1.1 Targets

| Goal | Target |
| --- | --- |
| Cores | 4, one hart each (SMT reserved in the architecture, off in this part) **\[FIXED\]** |
| Core class | 4-wide out-of-order, 128-entry reorder buffer |
| Per-clock performance | Comparable to a modern 4-wide mobile-class core (Arm Cortex-A76 class), to be confirmed by the model in 2.12 **\[TENTATIVE\]** |
| Frequency | 2.5 GHz base, 3.0 GHz boost **\[TENTATIVE\]** |
| Power | 30 W for the whole SoC at base clock **\[TENTATIVE\]** |
| Process | 7 to 12 nm class FinFET, to be selected **\[OPEN\]** |
| Memory | 2 channels of DDR5 (each 2 x 32 bit), up to DDR5-5600, up to 128 GB, ECC on server SKUs |
| I/O | 24 PCIe lanes: Gen 5 x16, Gen 5 x4 (M.2), Gen 4 x4 to the chipset |
| Software | PAL mode and full-PAL mode, with a PAL image the OS supplies (Platform Specification, 3.3 and 7.3) |

### 1.2 Design rules for easy implementation

These are requirements on every later design decision.

1. **Four wide, not eight.** Rename, bypass and register-file cost grow faster than linearly with width, and returns diminish. The cancelled Alpha 21464 (EV8) was 8-wide with SMT, and its complexity is a warning.
2. **Small distributed schedulers** instead of one large wakeup-and-select window. They are easier to close timing on and cost less power.
3. **Use the ISA's simplicity.** Fixed 32-bit instructions need no micro-op cache, and there are no condition flags, so renaming has one fewer thing to track.
4. **No exotic features in the first part:** no SMT, no value prediction, no run-ahead execution, no wide vectors.
5. **One parameterized generator** produces the core. Every size in this document is a parameter.
6. **Reuse Z1-lite.** Decoder tables, FPU, MMU and walker, coherence protocol, interrupt controller, debug module and the retire-trace checker are shared with Z1-lite and verified there first.
7. **A feature enters the RTL only if the model shows it is worth its cost.** Section 2.12 lists the decisions so far.
8. **Power by construction:** clock gating at every register group, structures sized to what the model needs and no larger, and no speculation that costs more energy than it saves.

## 2. The CPU

### 2.1 Summary

| Parameter | Value |
| --- | --- |
| Fetch | 32 bytes (8 instructions) per cycle |
| Decode, rename, dispatch, commit | 4 per cycle |
| Reorder buffer | 128 entries |
| Integer physical registers | 128 (each 64 bits plus 1 condition bit) |
| FP physical registers | 112 (64 bits) |
| Branch checkpoints | 8 |
| Schedulers | Integer 48 entries (3 selects), memory 32 (2 selects), FP 24 (2 selects) |
| Execution ports | 3 integer, 2 memory, 2 FP |
| Load queue / store queue | 48 / 32 entries, plus a 16-entry post-commit store buffer |
| L1 I / D | 32 KB / 32 KB, 8-way, 64-byte lines |
| L2 | 512 KB per core, private, 8-way |
| L3 | 16 MB shared, 16-way, 4 slices |
| TLBs | ITLB 48, DTLB 64, shared L2 TLB 1024 (4-way), 2 page walkers |

### 2.2 Pipeline

Twelve stages from fetch to commit.

| Stage | Name | Work |
| --- | --- | --- |
| 1 | F1 | PC select, L0 and L1 branch target buffers, ITLB |
| 2 | F2 | I-cache access, direction predictor |
| 3 | F3 | Align, check predicted-taken branches, write the fetch queue |
| 4 | D1 | Decode 4 instructions |
| 5 | D2 | Crack, allocate ROB, load-queue and store-queue entries |
| 6 | RN | Rename (RAT read, free-list allocation, dependency check inside the group) |
| 7 | DP | Dispatch into the schedulers |
| 8 | IS | Wakeup and select |
| 9 | RR | Register read, bypass selection |
| 10 | EX | Execute (memory operations: address generation) |
| 11 | WB | Write back, report completion and exceptions |
| 12 | CM | Commit, free registers, release stores |

Loads continue after EX with M1 (TLB and tag compare) and M2 (data select and alignment), giving a load-to-use latency of 4 cycles. A mispredicted branch costs about 12 cycles. **\[TENTATIVE\]**

### 2.3 Front end

- **Decoupled fetch.** The branch predictor runs ahead of fetch and writes fetch targets into a 16-entry fetch target queue. Fetch and instruction prefetch follow that queue.
- **Branch target buffers:** L0 of 32 fully associative entries (no bubble); L1 of 2048 entries, 4-way (one bubble).
- **Direction predictor:** TAGE-lite: a 4K-entry bimodal table plus three tagged tables of 1K entries each with history lengths of 6, 16 and 40. History is kept speculatively with checkpoint repair. Predictor updates occur at commit. A simpler tournament predictor is the fallback if timing fails.
- **Indirect branches:** 512-entry target cache indexed by PC and history. **Return stack:** 32 entries with pointer checkpointing. The JSR type bits (4.2 of the Architecture Specification) drive both.
- **I-cache:** 32 KB, 8-way. A way predictor reads one data way when it is confident, and tags are always read; this cuts instruction-fetch energy. Prefetch is directed by the fetch target queue plus next-line.
- **Decode:** four identical decoders (fixed-width encoding). No micro-op cache. **\[FIXED\]**
- **Cracking into two micro-ops:** CMOVxx and FCMOVxx (a first micro-op evaluates the condition and carries it as a 65th bit with the value, a second selects against the old destination), so that no micro-op has more than two sources. Everything else is one micro-op.
- **Serialized instructions** (executed non-speculatively when they reach the head of the ROB): PR\_READ, PR\_WRITE, XRET, privileged CALL\_PAL, MB, WMB, RMB, SPECB, ICSYNC, cache operations, LL, SC, all AMOs, TLB maintenance. With the hardware fast path on, RDUNIQUE and WRUNIQUE are ordinary micro-ops on port I0 that read and write PUNIQUE, and IMB executes as ICSYNC.

### 2.4 Rename and dispatch

- Two register alias tables (integer and FP), each with 32 entries. R31 and F31 map to a hardwired zero physical register.
- **Free list** held as a bit vector with priority encoders. A physical register is freed when the instruction that overwrote its architectural register commits.
- **Checkpoints:** the RAT is snapshotted at every branch, up to 8 in flight. A mispredict restores a snapshot in one cycle. Dispatch of a ninth branch stalls. Exceptions restore the retirement RAT instead.
- **Dispatch** stalls when any of these is full: ROB, a scheduler, the load or store queue, free registers, or checkpoints.
- The integer register file is built as two replicated copies, each written by every writer and each serving half of the readers (copy A: ports I0, I1, M0; copy B: I2, M1). This avoids a single very-high-port file.

### 2.5 Schedulers and execution

- Each scheduler is age-ordered (an age matrix per scheduler) and selects the oldest ready entries. Wakeup uses tag broadcast.
- **Speculative wakeup of load consumers** assumes an L1 hit. If the load misses or is delayed, its consumers are cancelled and issue again (selective replay).

| Port | Units |
| --- | --- |
| I0 | ALU, branch |
| I1 | ALU, shifter, byte manipulation (EXT, INS, MSK, ZAP), CTLZ, CTTZ, CTPOP, rotate, byte swap |
| I2 | ALU, branch, 64 x 64 multiplier, MVI operations |
| M0 | Load address generation and load pipeline |
| M1 | Load or store address generation, store data (at most one store per cycle) |
| F0 | FP add, subtract, compare, convert, divide and square root |
| F1 | FP multiply, second FP adder, integer-to-FP and FP-to-integer moves |

Target result latencies (cycles): integer ALU, shift and byte manipulation 1; CTLZ, CTTZ, CTPOP 2; MULQ 3, UMULH 4; load hit 4; FP add 3, multiply 4, compare 2; DIVS 10, DIVT 15, SQRTS 12, SQRTT 20 (iterative, other FP work continues); integer/FP moves 2. **\[TENTATIVE\]**

The FP register file has 6 read and 4 write ports. Bypass is complete inside the integer cluster, and crossing to the FP cluster costs one extra cycle.

### 2.6 Load/store unit

- **Queues:** 48-entry load queue and 32-entry store queue, both allocated in order at D2. Retired stores move to a 16-entry store buffer and drain to the L1 one per cycle, merging neighbors in the same line.
- **Two loads per cycle** through the banked L1 data cache (8 banks of 8 bytes). Two loads to the same bank in one cycle conflict, and the younger one retries.
- **Address disambiguation:** loads may pass older stores whose addresses are unknown. A 1024-entry memory-dependence predictor (indexed by load PC, remembers that a load collided with a store) makes such a load wait for older stores. A violation flushes from the load.
- **Store-to-load forwarding** from the store queue and store buffer when the store fully covers the load. A partial overlap waits.
- **Meeting the EV9 memory model** (Architecture Specification, section 5):
  - Same-address ordering, including load to load: when a load executes, it checks the load queue for a younger load to the same address that already ran, and replays that load.
  - Address and data dependencies hold naturally.
  - Control dependencies for stores: stores do not become visible before their branches commit.
  - MB: younger memory operations wait until the MB commits and the store buffer is empty. WMB: a marker in the store queue keeps order. RMB: younger loads wait until older loads complete.
  - Acquire and release AMO ordering is handled at commit time.
  - Multi-copy atomicity: a store becomes visible only when written into the L1 line held exclusively, and is never forwarded to another core before that.
- **LL, SC and AMOs execute at the head of the ROB**, non-speculatively. The AMO unit sits in the L1 data cache controller and operates on a line held exclusively.
- **Misaligned accesses** are split into two accesses inside the load/store unit, at no cost for accesses that stay in one line.
- Uncached and device accesses are issued in order at the head of the ROB.

### 2.7 Commit and exceptions

- Commit retires up to 4 instructions per cycle in order. Exceptions are recorded in the ROB entry and taken only at commit, so every exception (including FP exceptions and integer overflow traps) is precise, and no operation needs to be serialized just to keep FP traps precise. **\[FIXED\]**
- On an exception or interrupt, younger instructions are flushed, the RAT is restored from the retirement RAT, and the free list is rebuilt from the ROB. Entering the PAL image costs about the same as a mispredicted branch: the PAL image shares the architectural register files, so nothing is saved or swapped, and XRET restarts fetch at the saved PC.
- The core emits one retire record per instruction for the co-simulation checker.

### 2.8 Address translation

ITLB 48 and DTLB 64 entries, fully associative, with the DTLB serving two lookups per cycle. A shared second-level TLB of 1024 entries (4-way), a 16-entry superpage TLB, and a 32-entry page-walk cache for upper-level entries. Two hardware walkers per core handle all three native granules (4, 8 and 16 KB) and the legacy 3-level 8 KB format; legacy support is mandatory in both PAL models. Walks may be speculative, subject to permissions, FPMA protection and precise faults. Native accessed/dirty updates use an atomic read-unique request and cache write; legacy PTEs retain their own format and fault semantics. Optional software refill is reported by FCAP bit 32; it does not replace the required legacy walker.

### 2.9 Caches and coherence

| Level | Configuration | Latency |
| --- | --- | --- |
| L1 I | 32 KB, 8-way, virtually indexed, physically tagged, parity | 3 cycles |
| L1 D | 32 KB, 8-way, write-back, 12 miss-status registers, parity | 4 cycles |
| L2 | 512 KB private, 8-way, inclusive of L1, 16 miss-status registers, ECC | about 12 cycles |
| L3 | 16 MB shared, 16-way, four slices interleaved by address, non-inclusive, ECC | about 38 cycles |

- Each L1 way is 4 KB, so the L1 never aliases at any granule. **\[FIXED\]**
- **Prefetch:** stride prefetcher at L1 (16 streams); stream prefetcher at L2 with throttling when accuracy drops. Prefetchers can be disabled by a privileged register.
- **Replacement:** tree pseudo-LRU in L1 and L2, a re-reference-interval policy in L3.
- **Coherence:** directory-based MESI with the directory in the L3 slices, over the TileLink-style five-channel protocol chosen for Z1-lite (Architecture Specification, 8.10).

### 2.10 Uncore

- **Interconnect:** crossbar connecting the four L2s to the four L3 slices, with the memory controller hub and the I/O bridge as additional targets.
- **Memory controller:** 2 DDR5 channels, each two 32-bit sub-channels, with read and write queues, bank-aware scheduling, refresh management and optional side-band ECC. The PHY is licensed IP.
- **PCIe:** 24 lanes with Gen 5 PHYs: one x16 root port (bifurcates to x8/x8 or x8/x4/x4), one x4 root port for M.2, and one x4 port that connects to the chipset at Gen 4 (section 3).
- **IOMMU, KAIA interrupt controller, KTIMER, system controller, boot ROM (64 KB), on-chip SRAM (2 MB), debug module and PMU** as in the Platform Specification.
- **Monitoring:** two temperature sensors per core, one per L3 slice and one in the I/O area, plus per-rail current estimators.

### 2.11 Power and clocks

- **Clock gating:** automatic at every register group, with an enable derived from valid bits, so idle pipeline stages and idle execution ports don't toggle. A design rule enforced at review, not a late optimization.
- **Voltage and frequency:** one shared core rail and per-core PLL dividers, so cores can run at different frequencies without separate regulators. **\[FIXED\]**
- **Idle states:**

| State | What it means | Exit time (target) |
| --- | --- | --- |
| C0 | Running | None |
| C1 | Clock-gated after a wait-for-interrupt instruction | Under 1 microsecond |
| C2 | Core power-gated after flushing L1 (L2 keeps state) | About 50 microseconds |
| C3 | Core and L2 off after flushing both | About 200 microseconds |
| Package | All cores in C3, L3 in retention, memory in self-refresh | About 1 millisecond |

- **Energy features:** way-predicted I-cache, banked L1 data array, small loop-friendly fetch queue, filtering of wrong-path prefetches, and lazy wakeup of idle schedulers.

| Budget at base clock (target) | Power |
| --- | --- |
| 4 cores, 4 W each | 16 W |
| 4 private L2s | 2 W |
| L3, crossbar and uncore | 3 W |
| DDR5 controller and PHY | 3 W |
| PCIe PHYs and I/O | 4 W |
| Clocks, sensors and leakage margin | 2 W |
| **Total** | **30 W** |

### 2.12 Why these sizes

| Decision | Reason | Cost avoided or accepted |
| --- | --- | --- |
| 4-wide, 128-entry ROB | Gets most of the available parallelism in typical code | A 6- or 8-wide core would need much larger rename, register file and bypass networks |
| Three small schedulers | Short critical paths, simple age matrices | Slightly less flexible than one unified window |
| CMOV cracked in two | Keeps every micro-op at two sources | One extra micro-op per CMOV (a small share of instructions) |
| No SMT | Lower power and verification cost, simpler memory ordering | Less throughput on a busy 4-core part |
| 32 KB 8-way L1 | Alias-free at every page granule, low hit latency | Smaller than a 64 KB L1, partly offset by the 512 KB private L2 |
| TAGE-lite predictor | Most of TAGE's accuracy at a small fraction of its cost | A little less accurate than a full TAGE-SC-L |
| Memory-dependence predictor | Lets loads run ahead of stores safely | Occasional flushes on mispredictions |
| Precise exceptions at commit | No special handling for FP traps | Needs a reorder buffer, which the design has anyway |

**Performance model first.** Before RTL, build a cycle-approximate model (either a trace-driven simulator, or an older gem5 release if it still carries an Alpha ISA, which should be checked) driven by traces from QEMU. Use it to confirm every size above on integer, floating-point and memory-bound workloads, and replace the targets with measured numbers.

### 2.13 Scaling to other parts

The 21564 is the base. Z1 and later parts are built from it without redesigning the core.

- **Tile.** One core, its private L2 and one L3 slice, with a standard interface to the interconnect. A part is N tiles plus memory controllers, PCIe and the chipset link.
- **What scales with core count:** the interconnect (the 4-core crossbar suits up to 8 tiles; above that a ring or mesh is needed **\[OPEN\]**), the L3 size (4 MB per tile), the coherence directory, the number of memory channels and PCIe lanes, and the power-delivery design.
- **What does not change:** the core microarchitecture, the ISA and platform, the L1 and L2 design, and the firmware interface. A new part changes only generator parameters and the uncore.
- **Verification:** Z1 reuses the 21564's. The new work is the larger uncore (interconnect, directory and memory system) and its coherence and power verification.

| Part | Socket | Cores | DDR5 channels | PCIe lanes |
| --- | --- | --- | --- | --- |
| 21564 | 21580 | 4 | 2 | 20 Gen 5 device lanes + Gen 4 x4 chipset link = 24 total |
| Z1 | 21580 | 8 to 16 **\[TENTATIVE\]** | 4 | 48 Gen 5 device lanes + Gen 4 x4 chipset link = 52 total |

## 3. The chipset: 21564 PCH

### 3.1 Role and link

The CPU already contains the memory controller, the main PCIe ports and the interrupt controller, so the chipset is a single I/O hub, the **platform controller hub (PCH)**, not a north and south bridge pair.

- **Link:** a standard PCIe Gen 4 x4 connection to the CPU, with no proprietary protocol. The PCH appears to software as a PCIe switch (one upstream port, several downstream ports) with integrated endpoints behind it, so no special driver is needed. **\[FIXED\]**
- **Bandwidth:** about 8 GB/s each way. Devices that need more (fast NVMe, GPUs) hang off the CPU's own lanes.
- **DMA** from every PCH endpoint passes through the CPU's IOMMU, identified by PCIe requester ID.
- **Interrupts:** MSI and MSI-X go straight up the link to the CPU's KAIA. Wired interrupts (UART, GPIO, SD, thermal) are collected by a small wired-interrupt controller inside the PCH, which forwards them as MSI writes.
- **No hidden management processor.** The PCH contains only hardwired state machines and register blocks, with no embedded CPU running vendor firmware. **\[FIXED\]**

```
   CPU (21564)                              PCH
  +-----------+   PCIe Gen4 x4 (KLink)   +----------------------------+
  | PCIe RC   |<------------------------>| upstream port              |
  | IOMMU     |                          |  +-- xHCI (USB)            |
  | KAIA      |                          |  +-- AHCI (SATA)           |
  | boot SPI  |                          |  +-- 2.5 GbE MAC           |
  +-----------+                          |  +-- HD Audio, SDHCI       |
                                         |  +-- display controller    |
                                         |  +-- UART, I2C, SPI, GPIO  |
                                         |  downstream PCIe Gen3 x8   |
                                         +----------------------------+
```

### 3.2 Functions

| Block | Specification |
| --- | --- |
| Uplink | PCIe Gen 4 x4 to the CPU |
| Downstream PCIe | 8 lanes at Gen 3, configurable as x4 plus four x1, or two x4 |
| SATA | AHCI 1.3.1, 4 ports at 6 Gb/s, hot-plug, NCQ |
| USB | xHCI 1.2 with 4 ports of USB 3.2 Gen 2 (10 Gb/s), 2 of USB 3.2 Gen 1, 6 of USB 2.0. Two ports support Type-C through external power-delivery controllers on I2C. One device-mode port for recovery |
| Ethernet | One 2.5 GbE MAC (SGMII or 2500BASE-X to an external PHY), descriptor-ring DMA, checksum offload, 4 receive queues, wake on LAN. A MAC with an existing Linux driver is preferred, for example a Synopsys DesignWare Ethernet QoS core |
| Audio | HD Audio-compatible controller, 2 codecs |
| Display | Simple display controller with one output (HDMI or DisplayPort through an external PHY), up to 3840 x 2160 at 30 Hz, reading a linear framebuffer from system memory. No 3D |
| Cards | SD host controller, SDHCI-compatible, UHS-I |
| TPM | SPI controller with chip select for a TPM 2.0 device (TIS interface) |
| Low-speed I/O | 4 UARTs (16550), 4 I2C, 1 SMBus, 2 SPI, 64 GPIO, 4 PWM outputs with tachometer inputs, watchdog |
| RTC | Battery-backed, with an external 32.768 kHz crystal |
| Sensors and control | Temperature sensors, fan control loops, board power-sequencing state machine, wake logic |
| Package and power | BGA of about 400 balls, about 5 W **\[TENTATIVE\]** |

**Boot flash is attached to the CPU, not the PCH**, because the CPU boots before the link is trained. The PCH's SPI controllers are for the TPM and for expansion.

### 3.3 Power states, reset and clocks

- States: S0 (running), S3 (suspend to RAM, DRAM in self-refresh), S5 (soft off). Wake sources: USB, Ethernet magic packet, RTC alarm, power button, selected GPIO.
- The PCH's sequencing state machine controls the ATX supply (PS\_ON, power-good), orders the board rails, and releases device/CPU resets once their supplies and reference clocks are stable. Hart 0 must be able to execute boot ROM and stage 1 from flash/SRAM before DRAM training completes. DRAM reset release is not DRAM calibration: F-level stage 1 performs training, and normal DRAM traffic and secondary-hart release wait for success. PCIe link initialization follows the firmware sequence.
- Clock inputs: 100 MHz differential reference from the board's clock generator, a 25 MHz crystal, and 32.768 kHz for the RTC.
- The PCH is built in a mature, low-cost process node. It does not need to share the CPU's process.

## 4. Evaluation board: KEB-1

### 4.1 Summary

| Item | Specification |
| --- | --- |
| Form factor | Mini-ITX, 170 x 170 mm |
| CPU | Socket 21580 (LGA, Platform Specification 1.2), with only the 21564's 2 memory channels and 24 PCIe lanes wired and the rest of the pins unused **\[TENTATIVE\]** |
| Memory | 2 DDR5 UDIMM slots (one per channel), ECC UDIMMs supported |
| Expansion | PCIe 5.0 x16 slot (CPU); M.2 2280 M-key Gen 5 x4 (CPU); M.2 2280 M-key Gen 3 x4 (PCH); PCIe 3.0 x4 open-ended slot (PCH) |
| Storage | 4 SATA 6 Gb/s |
| USB | Rear: 2 USB-A 3.2 Gen 2, 2 USB-A 3.2 Gen 1, 1 USB-C 3.2 Gen 2. Internal headers: 2 dual USB 2.0 (4 ports). 1 USB-C device-mode port for recovery |
| Network | 1 x 2.5 GbE RJ45 |
| Display | HDMI from the PCH display controller (firmware console), plus any PCIe GPU for real graphics |
| Audio | HD Audio codec, line out and microphone in |
| Power input | ATX 24-pin plus 8-pin CPU power. A 250 W ATX supply is recommended |
| Board layers | 12 layers, low-loss laminate **\[TENTATIVE\]** |

### 4.2 Boot, debug and bring-up

- **Boot flash:** two socketed 32 MB quad-SPI NOR chips (A and B images) with an in-circuit programming header and a boot-select jumper.
- **Console:** USB-to-UART bridge to UART0, a second UART on a header.
- **JTAG:** 20-pin header to the CPU debug module, with halt-at-reset.
- **TPM:** header for an SPI TPM 2.0 module.
- **Diagnostics:** two-digit hex display driven by firmware (boot stage codes), LEDs for each power rail and boot state, power, reset and clear-RTC buttons, test points and current-sense shunts on every rail.
- **Cooling:** three 4-pin PWM fan headers with tachometer, thermal sensor on the board near the socket, mounting for a 35 W heatsink.
- **Recovery:** if both firmware images are bad, the CPU boot ROM falls back to loading a recovery image over USB device mode.

### 4.3 Power

| Rail | Source |
| --- | --- |
| CPU core | 4-phase regulator, about 60 A continuous |
| CPU uncore | 2-phase regulator |
| DDR5 | 5 V to each DIMM's own power-management chip |
| PCIe and I/O | 1.8 V and 3.3 V regulators |
| Slots | 12 V and 3.3 V from the ATX connector (x16 slot up to 75 W) |

Board budget at full load: CPU 30 W plus regulator losses, PCH 5 W, 2 DIMMs 12 W, x16 slot 75 W, 2 M.2 drives 16 W, USB 15 W, giving about 170 W. The PCH sequences all rails.

### 4.4 Clocks and signal integrity

- A 100 MHz clock generator drives the CPU, the PCH, the x16 slot and both M.2 slots separately. Spread spectrum is off by default and selectable by a switch.
- The PCIe 5.0 slot is routed short with footprints for an optional retimer, and the board is first validated at Gen 4.
- DDR5 follows the memory IP vendor's reference layout, with length matching per byte lane and simulation before fabrication.

### 4.5 Pre-silicon variant (KEB-0) **\[OPEN\]**

Before the 21564 exists, a simpler carrier board with a large FPGA module can run the Z1-lite configurations on real DDR, PCIe and USB, with the same firmware and the same PCH-equivalent chips where available. This de-risks the software stack and the board's power, boot and debug design.

## 5. Firmware and software requirements

- Same stack as the platform: UEFI (EDK II), KSI, devicetree, optional ACPI, and Linux on a PAL image, in PAL mode or full-PAL mode. UEFI runs at F with translation off; 4 KB EFI allocation units do not require a 4 KB firmware translation regime. Activate/MODEL\_SELECT prepares the handoff before ExitBootServices; a retained Enter entry commits it afterward and enters PAL before the kernel (Platform Specification, 7.3).
- **New for the 21564:** FPARTID at F-level privileged index 10B (Z1-lite = 1, 21564 = 2, Z1 = 3). IMPLVER returns 9 in PAL mode and to F-level discovery, while K/U see 3 in full-PAL mode; FPARTID and FCAP always describe the physical part **\[TENTATIVE\]**. Also: idle states C1 to C3 through KSI for the OS idle driver; frequency control through the system controller; thermal trip and throttle interrupts.
- **Performance monitor events:** cycles, instructions retired, branch mispredicts, L1 instruction, L1 data, L2 and L3 misses, ITLB, DTLB and L2 TLB misses, ROB-full and scheduler-full stalls, memory-dependence flushes, load replays, DRAM reads and writes, and PCIe traffic.
- All PCH devices use standard Linux drivers: xHCI, AHCI, HD Audio, SDHCI, NVMe, a common Ethernet MAC, and a framebuffer.

## 6. Implementation plan

### 6.1 Staged core build

| Stage | Deliverable | Exit criteria |
| --- | --- | --- |
| O1 | 2-wide out-of-order integer core: rename, ROB, one scheduler | Integer test suite matches the model |
| O2 | Load/store queues, disambiguation, forwarding | Memory directed and random tests pass; litmus tests match the formal model |
| O3 | 4-wide with three schedulers and branch checkpoints | Random tests with heavy branch and memory density run clean |
| O4 | FP pipelines and precise FP exceptions | TestFloat clean; FP trap tests pass |
| O5 | Z1-lite's caches, MMU and coherence integrated | Linux boots on one core in simulation |
| O6 | Four cores | SMP Linux boots; multi-core litmus tests pass |
| O7 | Performance tuning against the model | Per-benchmark IPC within an agreed margin of the model |
| O8 | Power: clock-gating coverage and activity-based power estimates | Estimates match the budget in 2.11 within 15 percent |
| O9 | Physical feasibility: floorplan and timing on the target library | 2.5 GHz at the target corner with margin |

### 6.2 Verification additions for out-of-order

- Everything in Part II of the Architecture Specification still applies (retire-trace co-simulation, litmus tests, formal checks).
- **Speculation stress:** random programs with dense branches, loads and stores, and many mispredictions.
- **Formal invariants:** no physical register is leaked or duplicated, ROB pointers stay consistent, checkpoints restore exactly, the free list never overflows.
- **Replay and flush tests:** load replays, memory-dependence violations, interrupts arriving at every pipeline state.
- **Power intent:** a power-format file for the power-gated domains, checked for isolation and retention.

### 6.3 References and prototypes

- Study open-source out-of-order cores (for example BOOM and XiangShan) for structure and pitfalls. They implement other ISAs, so reuse ideas, not code, and check each licence before copying anything.
- A 4-wide out-of-order core is large for an FPGA. Expect to prototype a single core, and to simulate or emulate the full four-core SoC, which may need a multi-FPGA emulation system.
- **Cost reality.** Licensed IP for DDR5, PCIe Gen 5 and USB, plus masks, at a 7 to 12 nm class node costs many millions of dollars, as far as I know. Check with foundry and shuttle providers before committing.
- **Economy option (21564-E) \[OPEN\]:** a mature node (22 to 28 nm), 1.5 to 2 GHz, DDR4 or LPDDR4, and PCIe Gen 3 or 4. The same microarchitecture and software, a fraction of the cost and risk.

## 7. Open items

1. Process node and foundry, and whether to build the economy variant first.
2. Exact core sizes after the performance model (ROB, schedulers, queues, caches).
3. TAGE-lite versus a simpler tournament predictor, once timing is known.
4. L3 inclusion policy and directory size.
5. Whether a vector extension joins a later stepping.
6. SMT in a later stepping (the architecture reserves it).
7. Chipset bandwidth: whether Gen 4 x4 is enough or a wider link is needed.
8. Socket 21580 pin-out and package definition, and which regulator vendor family to use.
9. The KEB-0 FPGA carrier: which FPGA module and which peripheral chips.
10. Product naming after legal review.
