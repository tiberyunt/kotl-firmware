#include "efi-storage.h"
#ifdef KOTL_NATIVE
#include "efi-graphics.h"
#include "kotl/kotl.h"
static KOTL_BOOT_INFO native_platform;
#endif
static EFI_SYSTEM_TABLE *st;
static EFI_BOOT_SERVICES *bs;
static void text(const char *s)
{
    CHAR16 buf[160];
    UINTN n = 0;
    while (*s && n < 159)
        buf[n++] = (uint8_t)*s++;
    buf[n] = 0;
    st->ConOut->OutputString(st->ConOut, buf);
}
static void require(int ok, const char *message)
{
    if (!ok) {
        text("FAIL: ");
        text(message);
        text("\r\n");
        st->RuntimeServices->ResetSystem(2, EFI_LOAD_ERROR, 0, 0);
        for (;;) {
        }
    }
    text("PASS: ");
    text(message);
    text("\r\n");
}
static uint32_t local_crc(const void *d, UINTN n)
{
    const uint8_t *p = d;
    uint32_t c = ~0U;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (c >> 1) ^ (0xedb88320U & -(c & 1));
    }
    return ~c;
}

#ifdef KOTL_NATIVE
static void graphics_tests(void)
{
    static const EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = bs->LocateProtocol((EFI_GUID *)&guid, 0, (void **)&gop);
    if (status == EFI_NOT_FOUND)
        return;
    require(status == 0 && gop && gop->Mode->Info->HorizontalResolution == 1024 &&
                gop->Mode->Info->PixelFormat == 1,
            "GOP framebuffer mode and BGR pixel format");
    UINTN bytes = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
    require(gop->QueryMode(gop, 0, &bytes, &info) == 0 && bytes == sizeof(*info) &&
                info->PixelsPerScanLine == 1024,
            "GOP QueryMode allocates mode information");
    bs->FreePool(info);
    uint32_t unavailable = gop->Mode->MaxMode;
    require(gop->SetMode(gop, unavailable) == EFI_UNSUPPORTED &&
                gop->QueryMode(gop, unavailable, &bytes, &info) == EFI_INVALID_PARAMETER,
            "GOP rejects unavailable modes");
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL color = {0x12, 0x34, 0x56, 0}, readback[4];
    require(gop->Blt(gop, &color, 0, 0, 0, 710, 550, 2, 2, 0) == 0 &&
                gop->Blt(gop, readback, 1, 710, 550, 0, 0, 2, 2, 0) == 0 &&
                readback[3].Red == 0x56 && readback[3].Blue == 0x12,
            "GOP fill and video-to-buffer BLT");
    readback[0].Red = 0xaa;
    require(gop->Blt(gop, readback, 2, 0, 0, 710, 550, 2, 2, 0) == 0 &&
                gop->Blt(gop, 0, 3, 710, 550, 711, 550, 2, 2, 0) == 0 &&
                gop->Blt(gop, readback, 1, 711, 550, 0, 0, 2, 2, 0) == 0 && readback[0].Red == 0xaa,
            "GOP buffer-to-video and overlapping video BLT");
    require(gop->Blt(gop, &color, 0, 0, 0, 1023, 767, 2, 2, 0) == EFI_INVALID_PARAMETER &&
                gop->Blt(gop, readback, 1, 0, 0, 0, 0, 2, 2, 4) == EFI_INVALID_PARAMETER &&
                gop->Blt(gop, 0, 2, 0, 0, 0, 0, 1, 1, 0) == EFI_INVALID_PARAMETER,
            "GOP rejects out-of-range rectangles and short strides");
    for (uint32_t number = 1; number < gop->Mode->MaxMode; number++) {
        static const uint32_t widths[] = {1024, 1920, 2560, 3840};
        static const uint32_t heights[] = {768, 1080, 1440, 2160};
        require(number < sizeof(widths) / sizeof(widths[0]) &&
                    gop->QueryMode(gop, number, &bytes, &info) == 0 &&
                    info->HorizontalResolution == widths[number] &&
                    info->VerticalResolution == heights[number] &&
                    gop->SetMode(gop, number) == 0 &&
                    gop->Mode->Info->PixelsPerScanLine == widths[number] &&
                    gop->Mode->FrameBufferSize == (UINTN)widths[number] * heights[number] * 4,
                "GOP switches to an advertised high resolution");
        bs->FreePool(info);
        require(gop->Blt(gop, &color, 0, 0, 0, widths[number] - 1, heights[number] - 1, 1, 1, 0) == 0 &&
                    gop->Blt(gop, readback, 1, widths[number] - 1, heights[number] - 1, 0, 0, 1, 1, 0) == 0 &&
                    readback[0].Red == color.Red,
                "GOP BLT reaches the last pixel in the active mode");
    }
    require(gop->SetMode(gop, 0) == 0, "GOP restores the base mode");
    volatile uint32_t *fb = (void *)gop->Mode->FrameBufferBase;
    fb[590 * 1024 + 790] = 0x123456;
    require(fb[590 * 1024 + 790] == 0x123456, "GOP physical framebuffer is directly writable");
}
#endif
static int bytes_same(const void *left, const void *right, UINTN size)
{
    const uint8_t *a = left, *b = right;
    for (UINTN i = 0; i < size; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}
static int valid_table(EFI_TABLE_HEADER *h)
{
    _Alignas(8) uint8_t copy[512];
    if (h->HeaderSize > sizeof(copy))
        return 0;
    for (UINTN i = 0; i < h->HeaderSize; i++)
        copy[i] = ((uint8_t *)h)[i];
    ((EFI_TABLE_HEADER *)copy)->CRC32 = 0;
    return local_crc(copy, h->HeaderSize) == h->CRC32;
}
typedef struct {
    unsigned calls, order;
    EFI_TPL tpl;
} EVENT_NOTE;
static unsigned event_order;
static EVENT_NOTE before_note, exit_note, legacy_exit_note, end_timer_note;
static int before_allocation_ok, exit_allocation_blocked, exit_timers_stopped;
static EFI_EVENT end_timer;
static void event_note(EFI_EVENT event, void *context)
{
    (void)event;
    EVENT_NOTE *note = context;
    note->calls++;
    note->order = ++event_order;
    EFI_TPL old = bs->RaiseTPL(TPL_HIGH_LEVEL);
    note->tpl = old;
    bs->RestoreTPL(old);
}
static void wait_note(EFI_EVENT event, void *context)
{
    event_note(event, context);
    bs->SignalEvent(event);
}
static EFI_EVENT recreated;
static void close_note(EFI_EVENT event, void *context)
{
    event_note(event, context);
    bs->CloseEvent(event);
    bs->CreateEvent(0, 0, 0, 0, &recreated);
}
static unsigned nested_stage, nested_observed;
static EFI_EVENT nested_high;
static void nested_notify(EFI_EVENT event, void *context)
{
    (void)event;
    if (context) {
        nested_observed = nested_stage;
        return;
    }
    nested_stage = 1;
    bs->SignalEvent(nested_high);
    nested_stage = 2;
}
static void before_exit_note(EFI_EVENT event, void *context)
{
    event_note(event, context);
    void *pool = 0;
    before_allocation_ok = bs->AllocatePool(EfiBootServicesData, 8, &pool) == 0;
    if (pool)
        bs->FreePool(pool);
}
static void exit_notify(EFI_EVENT event, void *context)
{
    event_note(event, context);
    void *pool = 0;
    exit_allocation_blocked = bs->AllocatePool(EfiBootServicesData, 8, &pool) == EFI_UNSUPPORTED;
    unsigned calls = end_timer_note.calls;
    bs->Stall(3000);
    exit_timers_stopped = end_timer_note.calls == calls &&
                          bs->SetTimer(end_timer, TimerRelative, 1) == EFI_UNSUPPORTED;
}
static void event_tests(void)
{
    EFI_EVENT plain = 0, timer = 0, notify = 0, wait = 0, other = 0;
    UINTN index = 99;
    require(bs->CreateEvent(0, 0, 0, 0, &plain) == 0 && bs->CheckEvent(plain) == EFI_NOT_READY,
            "event create starts unsignaled");
    require(bs->SignalEvent(plain) == 0 && bs->CheckEvent(plain) == 0 &&
                bs->CheckEvent(plain) == EFI_NOT_READY,
            "CheckEvent consumes signal");
    require(bs->SetTimer(plain, TimerRelative, 1) == EFI_INVALID_PARAMETER &&
                bs->SignalEvent((EFI_EVENT)123) == EFI_INVALID_PARAMETER &&
                bs->WaitForEvent(0, &plain, &index) == EFI_INVALID_PARAMETER,
            "event services reject invalid handles and parameters");
    require(bs->CreateEvent(EVT_NOTIFY_WAIT | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, 0,
                            &other) == EFI_INVALID_PARAMETER &&
                bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, 0, 0, &other) ==
                    EFI_INVALID_PARAMETER &&
                bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_APPLICATION, event_note, 0, &other) ==
                    EFI_INVALID_PARAMETER &&
                bs->CreateEvent(0x40, 0, 0, 0, &other) == EFI_INVALID_PARAMETER,
            "event type and notification TPL validation");
    require(bs->CreateEvent(EVT_RUNTIME | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, event_note, 0, &other) ==
                    EFI_UNSUPPORTED &&
                bs->CreateEventEx(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, event_note, 0, 0,
                                  &other) == EFI_INVALID_PARAMETER,
            "unsupported runtime events and invalid CreateEventEx type rejected");
    EFI_EVENT old = plain;
    require(bs->CloseEvent(plain) == 0 && bs->CreateEvent(0, 0, 0, 0, &plain) == 0 &&
                plain != old && bs->CloseEvent(old) == EFI_INVALID_PARAMETER,
            "closed event handles stay invalid after slot reuse");
    require(bs->CreateEvent(EVT_TIMER, 0, 0, 0, &timer) == 0 &&
                bs->SetTimer(timer, TimerRelative, 10000) == 0,
            "relative timer in 100ns units");
    EFI_EVENT list[] = {plain, timer};
    require(bs->WaitForEvent(2, list, &index) == 0 && index == 1 &&
                bs->CheckEvent(timer) == EFI_NOT_READY,
            "WaitForEvent selects and consumes timer");
    require(bs->SetTimer(timer, TimerRelative, 0) == 0 && bs->WaitForEvent(1, &timer, &index) == 0,
            "zero-delay timer waits for next tick");
    require(bs->SetTimer(timer, TimerRelative, ~0ULL) == EFI_INVALID_PARAMETER &&
                bs->SetTimer(timer, (EFI_TIMER_DELAY)99, 1) == EFI_INVALID_PARAMETER,
            "timer duration overflow and invalid delay rejected");
    EVENT_NOTE periodic = {0};
    require(bs->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, &periodic,
                            &notify) == 0 &&
                bs->SetTimer(notify, TimerPeriodic, 10000) == 0,
            "periodic notification timer");
    bs->Stall(5000);
    require(periodic.calls >= 3 && periodic.tpl == TPL_CALLBACK,
            "Stall dispatches periodic timer at callback TPL");
    bs->SetTimer(notify, TimerCancel, ~0ULL);
    unsigned calls = periodic.calls;
    bs->Stall(3000);
    require(periodic.calls == calls, "TimerCancel stops future notifications");
    EFI_TPL saved = bs->RaiseTPL(TPL_NOTIFY);
    bs->SetTimer(notify, TimerPeriodic, 10000);
    bs->Stall(4000);
    require(periodic.calls == calls && saved == TPL_APPLICATION,
            "raised TPL defers timer notifications");
    bs->RestoreTPL(saved);
    require(periodic.calls == calls + 1, "RestoreTPL dispatches coalesced timer notification");
    bs->SetTimer(notify, TimerCancel, 0);
    require(bs->CheckEvent(notify) == EFI_INVALID_PARAMETER &&
                bs->WaitForEvent(1, &notify, &index) == EFI_INVALID_PARAMETER && index == 0,
            "notify-signal events cannot be waited on");
    saved = bs->RaiseTPL(TPL_CALLBACK);
    require(bs->WaitForEvent(1, &plain, &index) == EFI_UNSUPPORTED,
            "WaitForEvent requires application TPL");
    bs->RestoreTPL(saved);
    EVENT_NOTE waiting = {0};
    require(bs->CreateEvent(EVT_NOTIFY_WAIT, TPL_CALLBACK, wait_note, &waiting, &wait) == 0 &&
                bs->CheckEvent(wait) == 0 && waiting.calls == 1 &&
                bs->WaitForEvent(1, &wait, &index) == 0 && waiting.calls == 2,
            "wait notifications may signal their own event");
    EVENT_NOTE a = {0}, b = {0}, c = {0};
    EFI_EVENT ea, eb, ec;
    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, &a, &ea);
    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, &b, &eb);
    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, event_note, &c, &ec);
    saved = bs->RaiseTPL(TPL_HIGH_LEVEL);
    event_order = 0;
    bs->SignalEvent(eb);
    bs->SignalEvent(ea);
    bs->SignalEvent(ec);
    bs->SignalEvent(ea);
    bs->RestoreTPL(saved);
    require(c.order == 1 && b.order == 2 && a.order == 3 && a.calls == 1 && c.tpl == TPL_NOTIFY,
            "callbacks honor TPL priority, FIFO and signal coalescing");
    bs->SignalEvent(ea);
    require(a.calls == 2, "notify-signal state resets before callback");
    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, nested_notify, (void *)1, &nested_high);
    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, nested_notify, 0, &other);
    bs->SignalEvent(other);
    require(nested_observed == 1 && nested_stage == 2,
            "higher TPL notifications preempt lower callbacks");
    bs->CloseEvent(other);
    bs->CloseEvent(nested_high);
    EVENT_NOTE closing = {0};
    bs->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, close_note, &closing, &other);
    old = other;
    require(bs->SignalEvent(other) == 0 && closing.calls == 1 && recreated != old &&
                bs->SignalEvent(old) == EFI_INVALID_PARAMETER,
            "callback may close and replace its own event");
    bs->CloseEvent(recreated);
    EFI_GUID group = {0x12345234, 1, 2, {0, 1, 2, 3, 4, 5, 6, 7}};
    EFI_EVENT ga, gb, gc;
    EVENT_NOTE na = {0}, nb = {0};
    bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, &na, &group, &ga);
    bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, event_note, &nb, &group, &gb);
    bs->CreateEventEx(0, 0, 0, 0, &group, &gc);
    saved = bs->RaiseTPL(TPL_HIGH_LEVEL);
    event_order = 0;
    bs->SignalEvent(gc);
    bs->RestoreTPL(saved);
    bs->SignalEvent(gc);
    require(na.calls == 1 && nb.calls == 1 && nb.order < na.order && bs->CheckEvent(gc) == 0,
            "CreateEventEx groups signal all members before dispatch");
    bs->CloseEvent(ga);
    bs->CloseEvent(gb);
    bs->CloseEvent(gc);
    EVENT_NOTE map = {0};
    EFI_GUID map_guid = EFI_EVENT_GROUP_MEMORY_MAP_CHANGE;
    bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, event_note, &map, &map_guid, &ga);
    void *pool = 0;
    bs->AllocatePool(EfiBootServicesData, 8, &pool);
    bs->FreePool(pool);
    require(map.calls == 2, "memory-map event group tracks allocation and release");
    bs->CloseEvent(ga);
    EVENT_NOTE config = {0};
    bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, &config, &group, &ga);
    bs->InstallConfigurationTable(&group, &config);
    bs->InstallConfigurationTable(&group, &na);
    bs->InstallConfigurationTable(&group, 0);
    require(config.calls == 3, "configuration-table changes notify their event group");
    bs->CloseEvent(ga);
    EFI_EVENT cleanup[] = {plain, timer, notify, wait, ea, eb, ec};
    for (unsigned i = 0; i < sizeof(cleanup) / sizeof(cleanup[0]); i++)
        bs->CloseEvent(cleanup[i]);
    EFI_EVENT capacity[64];
    unsigned n = 0;
    while (n < 64 && bs->CreateEvent(0, 0, 0, 0, &capacity[n]) == 0)
        n++;
    require(n > 0 && n < 64 && bs->CreateEvent(0, 0, 0, 0, &other) == EFI_OUT_OF_RESOURCES,
            "event capacity is bounded and exhausted safely");
    for (unsigned i = 0; i < n; i++)
        bs->CloseEvent(capacity[i]);
    require(bs->CreateEvent(0, 0, 0, 0, &other) == 0 && bs->CloseEvent(other) == 0,
            "closed event capacity is reusable");
    require(st->ConIn->WaitForKey && bs->CheckEvent(st->ConIn->WaitForKey) == EFI_NOT_READY,
            "console publishes an initially empty WaitForKey event");
    EFI_GUID before = EFI_EVENT_GROUP_BEFORE_EXIT_BOOT_SERVICES,
             exit = EFI_EVENT_GROUP_EXIT_BOOT_SERVICES;
    bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, before_exit_note, &before_note, &before, &ga);
    bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, exit_notify, &exit_note, &exit, &gb);
    bs->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, event_note, &legacy_exit_note, &gc);
    bs->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, event_note, &end_timer_note,
                    &end_timer);
}

static const EFI_GUID variable_guid = {0xa6c00f10, 0x1188, 0x4419, {1, 2, 3, 4, 5, 6, 7, 8}};
static const CHAR16 runtime_name[] = {'R', 'u', 'n', 't', 'i', 'm', 'e', 0};
static const CHAR16 boot_name[] = {'B', 'o', 'o', 't', 'O', 'n', 'l', 'y', 0};
static int nv_available;
static void variable_tests(void)
{
    EFI_RUNTIME_SERVICES *rt = st->RuntimeServices;
    static const CHAR16 name[] = {'T', 'e', 's', 't', 0};
    static const CHAR16 unicode[] = {0x3b1, 0x4e2d, 0};
    const uint32_t bs = EFI_VARIABLE_BOOTSERVICE_ACCESS, access = bs | EFI_VARIABLE_RUNTIME_ACCESS;
    const uint32_t nv = access | EFI_VARIABLE_NON_VOLATILE;
    EFI_GUID other = variable_guid;
    other.Data4[7]++;
    uint64_t maximum, remaining, limit;
    require(rt->QueryVariableInfo(bs, &maximum, &remaining, &limit) == 0 && maximum >= remaining &&
                limit == 1024,
            "QueryVariableInfo reports bounded storage");
    UINTN size = 0;
    uint32_t attributes = 0;
    uint8_t buffer[16];
    require(rt->GetVariable(name, &variable_guid, 0, &size, 0) == EFI_NOT_FOUND,
            "missing variable returns EFI_NOT_FOUND");
    require(rt->SetVariable(name, &variable_guid, bs, 3, "abc") == 0 &&
                rt->GetVariable(name, &variable_guid, &attributes, &size, 0) ==
                    EFI_BUFFER_TOO_SMALL &&
                size == 3 && attributes == bs,
            "SetVariable and GetVariable sizing");
    size = sizeof(buffer);
    require(rt->GetVariable(name, &variable_guid, 0, &size, buffer) == 0 && size == 3 &&
                buffer[0] == 'a' && buffer[2] == 'c',
            "GetVariable returns exact bytes and optional attributes");
    require(rt->SetVariable(name, &other, bs, 1, "z") == 0,
            "variable namespaces distinguish vendor GUIDs");
    require(rt->SetVariable(name, &variable_guid, access, 1, "x") == EFI_INVALID_PARAMETER &&
                rt->SetVariable(name, &variable_guid, bs, 1, "q") == 0,
            "variable updates preserve access attributes");
    require(rt->SetVariable(name, &variable_guid, bs | EFI_VARIABLE_APPEND_WRITE, 2, "rs") == 0,
            "append-write extends an existing variable");
    size = sizeof(buffer);
    require(rt->GetVariable(name, &variable_guid, &attributes, &size, buffer) == 0 && size == 3 &&
                buffer[0] == 'q' && buffer[2] == 's' && attributes == bs,
            "append modifier is not stored as an attribute");
    require(
        rt->SetVariable(name, &variable_guid, bs | EFI_VARIABLE_APPEND_WRITE, 0, 0) == 0 &&
            rt->SetVariable(unicode, &variable_guid, access | EFI_VARIABLE_APPEND_WRITE, 0, 0) == 0,
        "zero-length append neither deletes nor creates variables");
    size = sizeof(buffer);
    require(rt->GetVariable(unicode, &variable_guid, 0, &size, buffer) == EFI_NOT_FOUND &&
                rt->SetVariable(unicode, &variable_guid, access, 1, "u") == 0,
            "UTF-16 variable names are supported");
    require(rt->SetVariable(name, &variable_guid, EFI_VARIABLE_RUNTIME_ACCESS, 1, "a") ==
                    EFI_INVALID_PARAMETER &&
                rt->SetVariable(name, &variable_guid, bs, 1025, buffer) == EFI_INVALID_PARAMETER &&
                rt->SetVariable(name, &variable_guid, bs, 1, 0) == EFI_INVALID_PARAMETER,
            "variable services reject invalid access, size and buffers");
    require(rt->SetVariable(name, &variable_guid,
                            bs | EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS, 1,
                            "a") == EFI_UNSUPPORTED,
            "authenticated variable writes are explicitly unsupported");
    CHAR16 long_name[65];
    for (unsigned i = 0; i < 65; i++)
        long_name[i] = 'x';
    require(rt->SetVariable(long_name, &variable_guid, bs, 1, "a") == EFI_INVALID_PARAMETER &&
                rt->SetVariable((CHAR16[]){0}, &variable_guid, bs, 1, "a") == EFI_INVALID_PARAMETER,
            "unterminated and empty variable names rejected");
    CHAR16 next[64] = {0};
    EFI_GUID cursor = {0};
    UINTN bytes = 2;
    require(rt->GetNextVariableName(&bytes, next, &cursor) == EFI_BUFFER_TOO_SMALL && bytes > 2 &&
                next[0] == 0,
            "variable enumeration reports required name buffer");
    bytes = sizeof(next);
    require(rt->GetNextVariableName(&bytes, next, &cursor) == 0,
            "variable enumeration starts from empty name");
    unsigned found = 1;
    while (found < 32) {
        bytes = sizeof(next);
        EFI_STATUS status = rt->GetNextVariableName(&bytes, next, &cursor);
        if (status == EFI_NOT_FOUND)
            break;
        if (status)
            return;
        found++;
    }
    require(found >= 3 && found < 32, "variable enumeration reaches end without duplicates");
    next[0] = 'x';
    next[1] = 0;
    bytes = sizeof(next);
    require(rt->GetNextVariableName(&bytes, next, &cursor) == EFI_INVALID_PARAMETER &&
                rt->GetNextVariableName(&(UINTN){1}, next, &cursor) == EFI_INVALID_PARAMETER,
            "variable enumeration validates cursor and terminator");
    require(rt->SetVariable(name, &variable_guid, 0, 0, 0) == 0 &&
                rt->SetVariable(name, &variable_guid, 0, 0, 0) == EFI_NOT_FOUND &&
                rt->SetVariable(name, &other, bs, 0, 0) == 0 &&
                rt->SetVariable(unicode, &variable_guid, 0, 0, 0) == 0,
            "variable deletion removes values and recovers storage");
    uint64_t recovered;
    require(rt->QueryVariableInfo(bs, &maximum, &recovered, &limit) == 0 && recovered == remaining,
            "variable storage accounting recovers after deletion");
    CHAR16 numbered[4] = {'V', '0', '0', 0};
    unsigned n = 0;
    for (; n < 33; n++) {
        numbered[1] = '0' + n / 10;
        numbered[2] = '0' + n % 10;
        EFI_STATUS status = rt->SetVariable(numbered, &variable_guid, bs, 1, "a");
        if (status == EFI_OUT_OF_RESOURCES)
            break;
        if (status)
            return;
    }
    require(n > 0 && n <= 32, "variable slot exhaustion is handled safely");
    for (unsigned i = 0; i < n; i++) {
        numbered[1] = '0' + i / 10;
        numbered[2] = '0' + i % 10;
        rt->SetVariable(numbered, &variable_guid, 0, 0, 0);
    }
    EFI_STATUS supported = rt->QueryVariableInfo(nv, &maximum, &remaining, &limit);
    require(supported == 0 || supported == EFI_UNSUPPORTED, "nonvolatile support is discoverable");
    nv_available = supported == 0;
    static const EFI_GUID runtime_guid = EFI_RT_PROPERTIES_TABLE_GUID;
    EFI_RT_PROPERTIES_TABLE *properties = 0;
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++)
        if (bytes_same(&st->ConfigurationTable[i].VendorGuid, &runtime_guid, sizeof(runtime_guid)))
            properties = st->ConfigurationTable[i].VendorTable;
    require(properties && properties->Version == 1 && properties->Length == 8 &&
                properties->RuntimeServicesSupported == (nv_available ? 0x2470U : 0x2430U),
            "runtime properties advertise only implemented services");
    if (nv_available) {
        require(rt->SetVariable(name, &variable_guid, nv, 3, "nv!") == 0 &&
                    rt->SetVariable(name, &variable_guid, 0, 0, 0) == 0,
                "nonvolatile variables commit and delete through persistent backend");
    } else
        require(rt->SetVariable(name, &variable_guid, nv, 1, "x") == EFI_UNSUPPORTED,
                "nonvolatile writes require a persistent backend");
    require(rt->SetVariable(runtime_name, &variable_guid, access, 1, "r") == 0 &&
                rt->SetVariable(boot_name, &variable_guid, bs, 1, "b") == 0,
            "prepare variables for post-ExitBootServices access checks");
}
static int variable_runtime_tests(void)
{
    EFI_RUNTIME_SERVICES *rt = st->RuntimeServices;
    uint8_t data = 0;
    UINTN size = 1;
    if (rt->GetVariable(runtime_name, &variable_guid, 0, &size, &data) || data != 'r')
        return 0;
    size = 1;
    if (rt->GetVariable(boot_name, &variable_guid, 0, &size, &data) != EFI_NOT_FOUND)
        return 0;
    if (rt->SetVariable(runtime_name, &variable_guid, 6, 1, "x") != EFI_WRITE_PROTECTED)
        return 0;
    if (rt->SetVariable(boot_name, &variable_guid, 0, 0, 0) != EFI_NOT_FOUND)
        return 0;
    CHAR16 next[64] = {0};
    UINTN bytes = sizeof(next);
    EFI_GUID guid = {0};
    int found_runtime = 0;
    for (unsigned i = 0; i < 33; i++) {
        bytes = sizeof(next);
        EFI_STATUS status = rt->GetNextVariableName(&bytes, next, &guid);
        if (status == EFI_NOT_FOUND)
            break;
        if (status || i == 32)
            return 0;
        if (bytes_same(&guid, &variable_guid, sizeof(guid))) {
            if (next[0] == 'B')
                return 0;
            if (next[0] == 'R')
                found_runtime = 1;
        }
    }
    if (!found_runtime)
        return 0;
    if (nv_available) {
        static const CHAR16 name[] = {'R', 't', 'N', 'v', 0};
        if (rt->SetVariable(name, &variable_guid, 7, 1, "n"))
            return 0;
        size = 1;
        if (rt->GetVariable(name, &variable_guid, 0, &size, &data) || data != 'n')
            return 0;
        if (rt->SetVariable(name, &variable_guid, 0, 0, 0))
            return 0;
    }
    return 1;
}

static void protocol_tests(void)
{
    EFI_GUID input_guid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID;
    EFI_GUID output_guid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
    void *console = 0;
    require(bs->HandleProtocol(st->ConsoleInHandle, &input_guid, &console) == 0 &&
                console == st->ConIn,
            "console input handle protocol");
    require(bs->HandleProtocol(st->ConsoleOutHandle, &output_guid, &console) == 0 &&
                console == st->ConOut,
            "console output handle protocol");
    EFI_GUID guid = {0xf8820791, 0x5e2f, 0x42f1, {1, 2, 3, 4, 5, 6, 7, 8}};
    EFI_HANDLE handle = 0;
    UINTN first = 17, second = 29;
    void *interface = 0;
    require(bs->InstallProtocolInterface(&handle, &guid, 0, &first) == 0 && handle,
            "InstallProtocolInterface");
    require(bs->InstallProtocolInterface(&handle, &guid, 0, &second) == EFI_INVALID_PARAMETER,
            "duplicate protocol rejected");
    require(bs->HandleProtocol(handle, &guid, &interface) == 0 && interface == &first,
            "HandleProtocol database lookup");
    require(bs->LocateProtocol(&guid, 0, &interface) == 0 && interface == &first, "LocateProtocol");
    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    require(bs->LocateHandleBuffer(ByProtocol, &guid, 0, &count, &handles) == 0 && count == 1 &&
                handles[0] == handle,
            "LocateHandleBuffer by protocol");
    require(bs->FreePool(handles) == 0, "free handle enumeration buffer");
    require(bs->OpenProtocol(handle, &guid, &interface, 0, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL) ==
                    0 &&
                interface == &first,
            "OpenProtocol GET_PROTOCOL");
    require(bs->OpenProtocol(handle, &guid, 0, 0, 0, EFI_OPEN_PROTOCOL_TEST_PROTOCOL) == 0,
            "OpenProtocol TEST_PROTOCOL");
    require(bs->ReinstallProtocolInterface(handle, &guid, &first, &second) == 0 &&
                bs->HandleProtocol(handle, &guid, &interface) == 0 && interface == &second,
            "ReinstallProtocolInterface");
    require(bs->UninstallProtocolInterface(handle, &guid, &second) == 0 &&
                bs->LocateProtocol(&guid, 0, &interface) == EFI_NOT_FOUND,
            "UninstallProtocolInterface");
    int recycled = 1;
    for (unsigned i = 0; i < 40; i++) {
        EFI_HANDLE marker = 0;
        interface = &first;
        if (bs->InstallProtocolInterface(&marker, &guid, 0, 0) ||
            bs->HandleProtocol(marker, &guid, &interface) || interface ||
            bs->UninstallProtocolInterface(marker, &guid, 0)) {
            recycled = 0;
            break;
        }
    }
    require(recycled, "marker protocols and handle reuse");
    UINTN original = st->NumberOfTableEntries;
    require(bs->InstallConfigurationTable(&guid, &first) == 0 &&
                st->NumberOfTableEntries == original + 1 && valid_table(&st->Hdr),
            "InstallConfigurationTable and system CRC");
    require(bs->InstallConfigurationTable(&guid, &second) == 0 &&
                st->NumberOfTableEntries == original + 1,
            "replace configuration table");
    require(bs->InstallConfigurationTable(&guid, 0) == 0 && st->NumberOfTableEntries == original &&
                valid_table(&st->Hdr) && bs->InstallConfigurationTable(&guid, 0) == EFI_NOT_FOUND,
            "remove configuration table");
}
static void disk_tests(EFI_HANDLE device)
{
    EFI_GUID block_guid = EFI_BLOCK_IO_PROTOCOL_GUID,
             fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_BLOCK_IO_PROTOCOL *block = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0, *located = 0;
    require(bs->HandleProtocol(device, &block_guid, (void **)&block) == 0 &&
                block->Media->LogicalPartition && block->Media->ReadOnly,
            "ESP Block IO protocol");
    uint8_t sector[512];
    require(block->ReadBlocks(block, block->Media->MediaId, 0, 512, sector) == 0 &&
                sector[510] == 0x55 && sector[511] == 0xaa,
            "Block IO reads FAT boot sector");
    require(block->ReadBlocks(block, 99, 0, 512, sector) == EFI_MEDIA_CHANGED,
            "Block IO media ID validation");
    require(block->ReadBlocks(block, 1, 0, 511, sector) == EFI_BAD_BUFFER_SIZE,
            "Block IO size validation");
    require(block->ReadBlocks(block, 1, block->Media->LastBlock + 1, 512, sector) ==
                EFI_INVALID_PARAMETER,
            "Block IO LBA bounds");
    require(block->WriteBlocks(block, 1, 0, 512, sector) == EFI_WRITE_PROTECTED,
            "Block IO is read-only");
    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    require(bs->LocateHandleBuffer(ByProtocol, &block_guid, 0, &count, &handles) == 0 && count == 2,
            "whole-disk and ESP handles");
    require(bs->FreePool(handles) == 0, "free block handle enumeration");
    require(bs->HandleProtocol(device, &fs_guid, (void **)&fs) == 0 &&
                bs->LocateProtocol(&fs_guid, 0, (void **)&located) == 0 && located == fs,
            "Simple File System protocol");
    EFI_FILE_PROTOCOL *root = 0, *file = 0;
    require(fs->OpenVolume(fs, &root) == 0, "OpenVolume");
    static const CHAR16 path[] = {'P', 'A', 'T', 'T', 'E', 'R', 'N', '.', 'B', 'I', 'N', 0};
    require(root->Open(root, &file, path, EFI_FILE_MODE_READ, 0) == 0, "open FAT32 file");
    EFI_GUID info_guid = EFI_FILE_INFO_ID;
    UINTN size = 0;
    require(file->GetInfo(file, &info_guid, &size, 0) == EFI_BUFFER_TOO_SMALL && size >= 80,
            "File GetInfo sizing");
    _Alignas(8) uint8_t info_bytes[1024];
    size = sizeof(info_bytes);
    require(file->GetInfo(file, &info_guid, &size, info_bytes) == 0 &&
                ((EFI_FILE_INFO *)info_bytes)->FileSize == 2049,
            "File GetInfo size");
    uint8_t data[31];
    size = sizeof(data);
    require(file->Read(file, &size, data) == 0 && size == sizeof(data), "File Read");
    int valid = 1;
    for (unsigned i = 0; i < sizeof(data); i++)
        if (data[i] != (uint8_t)(i * 37 + 11))
            valid = 0;
    require(valid, "file contents from disk");
    uint64_t position;
    require(file->GetPosition(file, &position) == 0 && position == 31, "File GetPosition");
    size = 31;
    require(file->Read(file, &size, 0) == EFI_INVALID_PARAMETER &&
                file->GetPosition(file, &position) == 0 && position == 31,
            "invalid Read preserves file position");
    require(file->SetPosition(file, 509) == 0, "File SetPosition");
    size = sizeof(data);
    require(file->Read(file, &size, data) == 0 && size == sizeof(data),
            "read across fragmented clusters");
    valid = 1;
    for (unsigned i = 0; i < sizeof(data); i++)
        if (data[i] != (uint8_t)((509 + i) * 37 + 11))
            valid = 0;
    require(valid, "fragmented file contents");
    require(file->SetPosition(file, UINT64_MAX) == 0, "seek end of file");
    size = sizeof(data);
    require(file->Read(file, &size, data) == 0 && size == 0, "end-of-file read");
    size = sizeof(data);
    require(file->Write(file, &size, data) == EFI_WRITE_PROTECTED, "File Write is protected");
    require(file->Close(file) == 0, "File Close");
    static const CHAR16 missing[] = {'N', 'O', 'N', 'E', 0};
    require(root->Open(root, &file, missing, 1, 0) == EFI_NOT_FOUND, "missing file rejected");
    size = 0;
    require(root->Read(root, &size, 0) == EFI_BUFFER_TOO_SMALL,
            "directory sizing preserves position");
    size = sizeof(info_bytes);
    require(root->Read(root, &size, info_bytes) == 0 &&
                ((EFI_FILE_INFO *)info_bytes)->FileName[0] == 'E',
            "directory enumeration");
    static const CHAR16 bootdir[] = {'E', 'F', 'I', '\\', 'B', 'O', 'O', 'T', 0};
    require(root->Open(root, &file, bootdir, 1, 0) == 0, "open nested directory");
    int found = 0;
    for (unsigned i = 0; i < 4; i++) {
        size = sizeof(info_bytes);
        EFI_STATUS status = file->Read(file, &size, info_bytes);
        require(status == 0, "nested directory Read");
        if (!size)
            break;
        EFI_FILE_INFO *info = (void *)info_bytes;
        if (info->FileName[0] == 'B' && info->FileName[8] == '4' && info->FileName[9] == '.')
            found = 1;
    }
    require(found, "VFAT long filename enumeration");
    require(file->Close(file) == 0 && root->Close(root) == 0, "close filesystem handles");
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *table)
{
    st = table;
    bs = table->BootServices;
    text("Alpha EFI application executing native AXP instructions\r\n");
    require(st->Hdr.Signature == EFI_SYSTEM_TABLE_SIGNATURE && valid_table(&st->Hdr) &&
                valid_table(&bs->Hdr) && valid_table(&st->RuntimeServices->Hdr),
            "EFI table signatures and CRC32");
    uint32_t crc = 0;
    require(bs->CalculateCrc32("123456789", 9, &crc) == 0 && crc == 0xcbf43926,
            "CalculateCrc32 known vector");
    EFI_GUID guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *info = 0;
    require(bs->HandleProtocol(image, &guid, (void **)&info) == 0 && info->SystemTable == st &&
                info->ImageBase != 0,
            "LoadedImage protocol");
    protocol_tests();
    event_tests();
    variable_tests();
#ifdef KOTL_NATIVE
    graphics_tests();
#endif
#ifdef KOTL_NATIVE
    EFI_GUID fdt_guid = {
        0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}};
    void *dtb = 0;
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        int match = 1;
        for (unsigned j = 0; j < sizeof(fdt_guid); j++)
            if (((uint8_t *)&st->ConfigurationTable[i].VendorGuid)[j] != ((uint8_t *)&fdt_guid)[j])
                match = 0;
        if (match)
            dtb = st->ConfigurationTable[i].VendorTable;
    }
    require(dtb && kotl_parse_fdt(dtb, &native_platform),
            "FDT published through EFI configuration table");
    require(KOTL_READ_CSR(0) == 0x102 && !(KOTL_READ_CSR(5) & 0x3fff),
            "native K privilege and 16 KiB identity page tables");
    KSI_RESULT version = ksi_call(KSI_VERSION, 0);
    require(version.error == 0 && version.value == 0x10000 &&
                ksi_call(KSI_PROBE, KSI_TIME).value == 1,
            "resident KSI discovery");
    require(ksi_call(KSI_PROBE, 0xdead).value == 0 && ksi_call(0xdead, 0).error == KSI_UNSUPPORTED,
            "unsupported KSI functions rejected");
    require(ksi_call(KSI_MODEL_SELECT, 1).error == KSI_UNSUPPORTED,
            "unimplemented PAL model is not selected");
#endif
    if (info->DeviceHandle)
        disk_tests(info->DeviceHandle);
    EFI_PHYSICAL_ADDRESS pa = 0;
    require(bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 2, &pa) == 0 && !(pa & 4095),
            "AllocatePages");
    volatile uint64_t *p = (void *)(ALPHA_KSEG + pa);
#ifdef KOTL_NATIVE
    require((uint64_t)p == pa && pa >= EFI_RAM_BASE,
            "EFI allocation pointers equal physical addresses");
#endif
    *p = 0xdec0dedcafebabeULL;
    require(*p == 0xdec0dedcafebabeULL, "allocated RAM read/write");
    require(bs->FreePages(pa, 2) == 0 && bs->FreePages(pa, 2) == EFI_INVALID_PARAMETER,
            "FreePages and double-free rejection");
    pa = EFI_TEST_FIXED_ADDRESS;
    require(bs->AllocatePages(AllocateAddress, EfiLoaderData, 1, &pa) == 0 &&
                pa == EFI_TEST_FIXED_ADDRESS,
            "AllocateAddress");
    require(bs->FreePages(pa, 1) == 0, "FreePages fixed address");
    pa = EFI_TEST_FIXED_ADDRESS - 1;
    require(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1, &pa) == 0 &&
                pa < EFI_TEST_FIXED_ADDRESS,
            "AllocateMaxAddress");
    require(bs->FreePages(pa, 1) == 0, "FreePages maximum address");
    void *pool = 0;
    require(bs->AllocatePool(EfiBootServicesData, 99, &pool) == 0 && pool != 0, "AllocatePool");
    bs->SetMem(pool, 99, 0x5a);
    require(((uint8_t *)pool)[98] == 0x5a, "SetMem");
    bs->CopyMem((uint8_t *)pool + 1, pool, 98);
    require(((uint8_t *)pool)[98] == 0x5a, "overlapping CopyMem");
    require(bs->FreePool(pool) == 0 && bs->FreePool(pool) == EFI_INVALID_PARAMETER,
            "FreePool and double-free rejection");
    uint8_t invalid[64] = {0};
    EFI_HANDLE bad = 0;
    require(bs->LoadImage(0, image, 0, invalid, sizeof(invalid), &bad) != 0,
            "invalid PE image rejected");
    UINTN size = 0, key, ds;
    uint32_t ver;
    require(bs->GetMemoryMap(&size, 0, &key, &ds, &ver) == EFI_BUFFER_TOO_SMALL && ds == 40 &&
                ver == 1,
            "GetMemoryMap sizing");
    EFI_MEMORY_DESCRIPTOR map[128];
    size = sizeof(map);
    require(bs->GetMemoryMap(&size, map, &key, &ds, &ver) == 0, "GetMemoryMap descriptors");
    UINTN oldkey = key;
    require(bs->AllocatePool(EfiLoaderData, 1, &pool) == 0, "allocation changes memory map");
    require(bs->ExitBootServices(image, oldkey) == EFI_INVALID_PARAMETER,
            "stale ExitBootServices key rejected");
    require(bs->FreePool(pool) == 0, "release temporary allocation");
    size = sizeof(map);
    require(bs->GetMemoryMap(&size, map, &key, &ds, &ver) == 0, "final memory map");
    require(bs->ExitBootServices(0, key) == EFI_INVALID_PARAMETER,
            "invalid ExitBootServices handle rejected");
    require(bs->Stall(1) == 0, "firmware virtual-time Stall");
    text("PASS: application services complete; exiting boot services\r\n");
    require(bs->ExitBootServices(image, key) == EFI_INVALID_PARAMETER && before_note.calls == 1 &&
                before_allocation_ok,
            "BeforeExitBootServices map changes require a fresh map key");
    size = sizeof(map);
    if (bs->GetMemoryMap(&size, map, &key, &ds, &ver))
        return EFI_LOAD_ERROR;
    bs->SetTimer(end_timer, TimerPeriodic, 10000);
    if (bs->ExitBootServices(image, key) != 0)
        return EFI_LOAD_ERROR;
    if (before_note.calls != 1 || exit_note.calls != 1 || legacy_exit_note.calls != 1 ||
        !exit_allocation_blocked || !exit_timers_stopped)
        return EFI_LOAD_ERROR;
    if (!variable_runtime_tests())
        return EFI_LOAD_ERROR;
    /* Console services have ended. Only runtime services are used after this point. */
    if (st->BootServices || st->ConOut || !valid_table(&st->Hdr))
        return EFI_LOAD_ERROR;
#ifdef KOTL_NATIVE
    if (*(volatile uint64_t *)native_platform.iommu != 0)
        return EFI_LOAD_ERROR;
    KSI_RESULT time = ksi_call(KSI_TIME, 0);
    if (time.error || !time.value)
        return EFI_LOAD_ERROR;
#endif
    st->RuntimeServices->ResetSystem(2, EFI_SUCCESS, 0, 0);
    return EFI_SUCCESS;
}
