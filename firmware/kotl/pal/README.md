# Kotl PAL: QEMU PALcode adaptation

Based on [qemu/qemu-palcode](https://github.com/qemu/qemu-palcode), commit
`99d9b4dcf27d7fbcbadab71bdc88ef6531baf6bf` (Richard Henderson and upstream
contributors). `upstream/` retains the original pal.S, pal.h, osf.h and COPYING;
`upstream.json` records their SHA-256 hashes. No proprietary DEC PAL binary is
used. The fixed revision's source archive can be fetched from GitHub codeload.

The derived sources `pal.S`, `pal.c` and `pal.h` are GPL-2.0-or-later.
Preserve their copyright/license notices and distribute the combined firmware
image and corresponding source under those terms. Original MIT firmware files
retain their source notices; see `upstream/COPYING` for the GPL text.

This is a source adaptation, not an unmodified Clipper PAL binary. EV9 has no
QEMU-private IPRs or banked PAL registers. The adaptation replaces those with
PR_READ/PR_WRITE, explicit register frames, private physical F stacks and XRET.
It replaces Typhoon/ISA/console assumptions with the PPI Info and retained KSI.
IMB and UNIQUE routines directly follow their upstream counterparts. System
value, TLB, FP-enable, IPL/status, kernel-GP, user-SP and hart-ID calls preserve
the corresponding upstream calling conventions within the supported subset.

Build outputs are `build/kotl-efi/kotl-pal.elf`, `.bin` and `.kpal`; the firmware
embeds this fixed-layout resident image. `scripts/make-resident-pal.py` generates
canonical KPAL/SHA-384 metadata. The payload has no mutable globals: per-hart
state and the F trap stack live in the PPI scratch area. Calls to retained
firmware use the service entry supplied by Info.

This is the initial firmware/UEFI personality, not a Linux-ready OSF/1 PAL.
See [the ABI and limits](../../../docs/kotl-firmware-pal.md) and run
`make test-kotl-pal` from the repository root for actual UEFI-to-kernel tests.
The upstream snapshot is a reference/provenance source; the build compiles the
adapted sources beside it, not the Clipper-specific original.

OpenAI Codex assisted the adaptation, tests and documentation.
