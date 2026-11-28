# Kotl firmware PAL binding, development revision 1

The supplied Markdown specifications define the intended platform. GUIDs,
structure offsets and function numbers here remain provisional. The developer
firmware supports a trusted compiled-in image; it does not claim the complete
production PAL protocol or OSF/1 compatibility.

## UEFI and selection

Firmware/EFI applications run at physical F privilege with FMODEL=0. The common
`KOTL_NATIVE` build macro identifies the platform, not a hardware-only OS model.
`include/efi-kotl-pal.h` defines the provisional protocol GUID and C structures.
GetInfo reports PPI 1.0, Kotl personality 0x80000000, resident ID 1 and supported
models 1/2. Policy 1 means the resident-only Permissive developer build.
External Stage returns EFI_UNSUPPORTED and never publishes an image ID.

Activate accepts resident ID, model, a fixed-size KOTL_OS_HANDOFF and output
Enter/context pointers. It validates the model, descriptor version/size,
canonical/aligned entry and stack, roots and format. Both half-roots must select the same format; a legacy root requires 43-bit
canonical entry/stack/GP addresses. Full-PAL requires explicit legacy roots. The resident copy is compared with its immutable ROM source;
code, scratch and metadata are prepared in fixed reserved memory. The supplied
descriptor is copied. All fallible work precedes the one-shot selection lock.
Repeated Activate returns EFI_ALREADY_STARTED, preserving the first transfer.
FMODEL remains zero. No dynamic transfer allocation is needed. At transfer, private EFI RAM and
IOMMU metadata remain F-only alongside PAL regions. The OS receives K access
to the emulated UART/IOMMU/PCI apertures; syscon, timer and KAIA remain F-only.
DMA cannot enter an explicit F-only region, even with an OS-controlled context.

KOTL_OS_HANDOFF contains u64 magic/version/size, virtual entry/stack/GP,
physical lower/upper roots including selectors, and six argument values.
The selected PAL consumes the retained copy at Info+0x800. A future OSF/1/Linux
personality will need a distinct versioned descriptor and HWRPB/PCB contract.

Early Enter returns EFI_NOT_READY without changing state. Failed
ExitBootServices/map-key attempts preserve the preparation. After successful
exit, Enter checks context identity, the complete prepared Info/context CRC and the
resident code copy, masks interrupts,
quiesces hart timer/IPI files, programs F-only PMAs for EFI runtime/private
metadata and PAL code/data/Info, and commits FMODEL, then
enters the PAL reset vector with PPI registers. It allocates nothing and calls
no Boot Services. Invalid context after exit is fail-stop. A successful transfer
never returns. The CRC detects accidental modification, not hostile F code;
UEFI applications already execute with F privileges in this prototype.

The PAL region is linked at RAM+6 MiB, data at +7 MiB, Info at +0x6f0000.
The generated `kotl-pal.kpal` records canonical SHA-384 metadata for that resident
payload. It is a fixed-layout firmware artifact, not a generally relocatable
PAL image for arbitrary loaders. Runtime resident trust derives from the
compiled firmware; no external image verification or signature trust is claimed.

## PAL Info and discovery

Info uses the companion QEMU provisional PPI layout: version/header at +0,
features +8, hart count/list +0x10/+0x18, code +0x20/+0x28, data +0x30/+0x38,
KAIA/timer/UART +0x40/+0x48/+0x50, memory map +0x58, KSI +0x60,
DTB +0x70, copied OS handoff +0x80, IDs +0x100, identity +0x180,
memory map +0x200. Full-PAL lists only one thread per core; other harts remain
parked. Sixteen scratch areas permit physical hart IDs up to 15.

Before normal EFI boot, firmware extends the existing reserved DTB in place
with `/firmware/kotl-pal`. `kotl,resident-image=1`, `kotl,personality`,
`kotl,ppi-version`, `kotl,policy`, `kotl,pending-model` and `kotl,active-model`
distinguish resident/prepared/committed state. It allocates no OS memory and
never changes DTB size during final transfer.

## Retained KSI

At fixed boot-info address 0x01000000, field +128 contains the physical service
entry. Its raw F ABI is R16=function, R17=argument, R0=value, R1=signed error,
R26=return address. The assembly C adapter handles Alpha aggregate returns and
preserves GP. PAL receives the same entry through Info and uses only that entry
and its own private regions, rather than reaching into other firmware state.

Provisional services: 0 version, 1 probe, 2 off, 3 reset, 4 putc, 5 nonblocking
getc (error -6 when empty), 6 24 MHz time, 16 boot-ownership release, 17 wall
clock nanoseconds. Unsupported returns error -2. MODEL_SELECT is internal to
the protocol in this revision; the old raw native-model selection call is gone.
No post-exit Boot Services entry is exposed through the PAL console bridge.

## Personality subset

The adapted GPL PAL uses EV9 vectors and PRs, physical per-hart trap stacks,
explicit interrupted GPR/trap-state preservation, and XRET. It carries QEMU's
IMB/RDUNIQUE/WRUNIQUE routines over to EV9; the normal dispatcher implements
HALT, CSERVE time, WRFEN, WRVPTPTR, WRVAL/RDVAL, TBI, SWPIPL/RDPS,
WRKGP, WRUSP/RDUSP and WHAMI. CSERVE selectors 0x100/0x101 provide Kotl console
output/input through retained KSI. Unsupported calls and unhandled exceptions
report and reset, rather than pretending successful OS emulation.

Full OSF/1 PCB switching, WR_ENT/frame delivery, syscall return, user exception
return, HWRPB/SRM, interrupts and secondary restart are pending. The current
kernel transport test is intentionally smaller than an operating system.
