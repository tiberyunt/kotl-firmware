/* Cooperative EFI event/TPL/timer core. SPDX-License-Identifier: MIT */
#include "services.h"
#define MAX_EVENTS 64
#define TICK_NS 1000000ULL
static struct EVENT {
    EFI_EVENT handle;
    uint32_t type;
    EFI_TPL tpl;
    EFI_EVENT_NOTIFY notify;
    void *context;
    EFI_HANDLE owner;
    EFI_GUID group;
    int grouped, signaled, pending, armed;
    uint64_t order, deadline, interval;
} events[MAX_EVENTS];
static EFI_TPL current_tpl = TPL_APPLICATION;
static uint64_t generation, sequence;
static int exiting, before_notified;
static const EFI_GUID exit_group = EFI_EVENT_GROUP_EXIT_BOOT_SERVICES;
static const EFI_GUID before_group = EFI_EVENT_GROUP_BEFORE_EXIT_BOOT_SERVICES;
static struct EVENT *find(EFI_EVENT handle)
{
    if (!handle)
        return 0;
    for (unsigned i = 0; i < MAX_EVENTS; i++)
        if (events[i].handle == handle)
            return &events[i];
    return 0;
}
static int same(const EFI_GUID *a, const EFI_GUID *b)
{
    return !memcmp(a, b, sizeof(*a));
}
static void queue(struct EVENT *event)
{
    if (!event->pending) {
        event->pending = 1;
        event->order = ++sequence;
    }
}
static void dispatch(void)
{
    while (boot_active) {
        struct EVENT *best = 0;
        for (unsigned i = 0; i < MAX_EVENTS; i++) {
            struct EVENT *event = &events[i];
            if (!event->handle || !event->pending || event->tpl <= current_tpl)
                continue;
            if (!best || event->tpl > best->tpl ||
                (event->tpl == best->tpl && event->order < best->order))
                best = event;
        }
        if (!best)
            return;
        EFI_EVENT handle = best->handle;
        EFI_EVENT_NOTIFY notify = best->notify;
        void *context = best->context;
        EFI_TPL old = current_tpl;
        best->pending = 0;
        if (best->type & EVT_NOTIFY_SIGNAL)
            best->signaled = 0;
        current_tpl = best->tpl;
        /* The callback may close/reuse its slot. Nothing below dereferences it. */
        notify(handle, context);
        current_tpl = old;
    }
}
static void signal_one(struct EVENT *event)
{
    if (event->signaled)
        return;
    event->signaled = 1;
    if (event->type & EVT_NOTIFY_SIGNAL)
        queue(event);
}
static void signal_members(const EFI_GUID *group)
{
    for (unsigned i = 0; i < MAX_EVENTS; i++)
        if (events[i].handle && events[i].grouped && same(group, &events[i].group))
            signal_one(&events[i]);
}
void events_signal_group(const EFI_GUID *group)
{
    if (!boot_active || !group)
        return;
    signal_members(group);
    dispatch();
}
static void signal(struct EVENT *event)
{
    if (event->signaled)
        return;
    if (event->grouped) {
        EFI_GUID group = event->group;
        signal_members(&group);
    } else
        signal_one(event);
}
void events_poll(void)
{
    if (!boot_active)
        return;
    if (!exiting) {
        uint64_t now = time_ns();
        for (unsigned i = 0; i < MAX_EVENTS; i++) {
            struct EVENT *event = &events[i];
            if (!event->handle || !event->armed || now < event->deadline)
                continue;
            if (!event->interval)
                event->armed = 0;
            else {
                /* Phase-preserving catch-up without an Alpha division helper. */
                uint64_t remainder = now - event->deadline;
                for (unsigned bit = 64; bit--;)
                    if ((remainder >> bit) >= event->interval)
                        remainder -= event->interval << bit;
                uint64_t delta = event->interval - remainder;
                if (delta > UINT64_MAX - now)
                    event->armed = 0;
                else
                    event->deadline = now + delta;
            }
            signal(event);
        }
    }
    dispatch();
}
static EFI_TPL raise_tpl(EFI_TPL tpl)
{
    EFI_TPL old = current_tpl;
    if (boot_active && tpl >= old && (tpl == 4 || tpl == 8 || tpl == 16 || tpl == 31))
        current_tpl = tpl;
    return old;
}
static void restore_tpl(EFI_TPL tpl)
{
    if (!boot_active || tpl > current_tpl || (tpl != 4 && tpl != 8 && tpl != 16 && tpl != 31))
        return;
    current_tpl = tpl;
    events_poll();
}
static EFI_STATUS create_ex(uint32_t type, EFI_TPL tpl, EFI_EVENT_NOTIFY notify, void *context,
                            const EFI_GUID *group, EFI_EVENT *out)
{
    if (!boot_active || exiting || current_tpl >= TPL_HIGH_LEVEL || !out)
        return EFI_INVALID_PARAMETER;
    if (type == EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE || (type & EVT_RUNTIME))
        return EFI_UNSUPPORTED;
    if (type == EVT_SIGNAL_EXIT_BOOT_SERVICES)
        return EFI_INVALID_PARAMETER;
    if (type & ~(EVT_TIMER | EVT_NOTIFY_SIGNAL | EVT_NOTIFY_WAIT))
        return EFI_INVALID_PARAMETER;
    uint32_t notifications = type & (EVT_NOTIFY_SIGNAL | EVT_NOTIFY_WAIT);
    if (notifications == (EVT_NOTIFY_SIGNAL | EVT_NOTIFY_WAIT))
        return EFI_INVALID_PARAMETER;
    if (notifications && (!notify || (tpl != TPL_CALLBACK && tpl != TPL_NOTIFY)))
        return EFI_INVALID_PARAMETER;
    for (unsigned i = 0; i < MAX_EVENTS; i++)
        if (!events[i].handle) {
            struct EVENT *event = &events[i];
            memset(event, 0, sizeof(*event));
            event->handle = (void *)(++generation * 256 + i + 1);
            event->type = type;
            event->tpl = tpl;
            event->notify = notify;
            event->context = context;
            event->owner = running_image;
            if (group) {
                event->grouped = 1;
                event->group = *group;
            }
            *out = event->handle;
            return EFI_SUCCESS;
        }
    return EFI_OUT_OF_RESOURCES;
}
static EFI_STATUS create(uint32_t type, EFI_TPL tpl, EFI_EVENT_NOTIFY notify, void *context,
                         EFI_EVENT *out)
{
    if (type == EVT_SIGNAL_EXIT_BOOT_SERVICES)
        return create_ex(EVT_NOTIFY_SIGNAL, tpl, notify, context, &exit_group, out);
    return create_ex(type, tpl, notify, context, 0, out);
}
static EFI_STATUS set_timer(EFI_EVENT handle, EFI_TIMER_DELAY type, uint64_t delay)
{
    struct EVENT *event = find(handle);
    if (!boot_active || !event || !(event->type & EVT_TIMER) || type > TimerRelative ||
        current_tpl >= TPL_HIGH_LEVEL)
        return EFI_INVALID_PARAMETER;
    if (exiting)
        return EFI_UNSUPPORTED;
    if (type == TimerCancel) {
        event->armed = 0;
        return EFI_SUCCESS;
    }
    uint64_t now = time_ns(), duration;
    if (delay > UINT64_MAX / 100)
        return EFI_INVALID_PARAMETER;
    duration = delay * 100;
    if (!duration)
        duration = TICK_NS - now % TICK_NS;
    if (duration > UINT64_MAX - now)
        return EFI_INVALID_PARAMETER;
    event->deadline = now + duration;
    event->interval = type == TimerPeriodic ? (delay ? delay * 100 : TICK_NS) : 0;
    event->armed = 1;
    return EFI_SUCCESS;
}
static EFI_STATUS signal_event(EFI_EVENT handle)
{
    struct EVENT *event = find(handle);
    if (!boot_active || !event)
        return EFI_INVALID_PARAMETER;
    signal(event);
    dispatch();
    return EFI_SUCCESS;
}
static EFI_STATUS close_event(EFI_EVENT handle)
{
    struct EVENT *event = find(handle);
    if (!boot_active || !event || current_tpl >= TPL_HIGH_LEVEL)
        return EFI_INVALID_PARAMETER;
    memset(event, 0, sizeof(*event));
    return EFI_SUCCESS;
}
static EFI_STATUS check_event(EFI_EVENT handle)
{
    if (!boot_active || current_tpl >= TPL_HIGH_LEVEL)
        return EFI_INVALID_PARAMETER;
    events_poll();
    struct EVENT *event = find(handle);
    if (!event || (event->type & EVT_NOTIFY_SIGNAL))
        return EFI_INVALID_PARAMETER;
    if (!event->signaled && (event->type & EVT_NOTIFY_WAIT)) {
        queue(event);
        dispatch();
        event = find(handle);
        if (!event)
            return EFI_INVALID_PARAMETER;
    }
    if (!event->signaled)
        return EFI_NOT_READY;
    event->signaled = 0;
    return EFI_SUCCESS;
}
static EFI_STATUS wait_event(UINTN count, EFI_EVENT *handles, UINTN *index)
{
    if (!boot_active || !count || !handles || !index)
        return EFI_INVALID_PARAMETER;
    if (current_tpl != TPL_APPLICATION)
        return EFI_UNSUPPORTED;
    for (;;) {
        for (UINTN i = 0; i < count; i++) {
            EFI_STATUS status = check_event(handles[i]);
            if (status != EFI_NOT_READY) {
                *index = i;
                return status;
            }
        }
    }
}
void events_clear(EFI_EVENT handle)
{
    struct EVENT *event = find(handle);
    if (event)
        event->signaled = 0;
}
void events_before_exit(void)
{
    if (!before_notified) {
        before_notified = 1;
        events_signal_group(&before_group);
    }
}
void events_exit_boot(void)
{
    exiting = 1;
    for (unsigned i = 0; i < MAX_EVENTS; i++) {
        events[i].armed = 0;
        if (events[i].type & EVT_TIMER)
            events[i].pending = 0;
    }
    events_signal_group(&exit_group);
}
void events_unload_image(EFI_HANDLE owner)
{
    for (unsigned i = 0; i < MAX_EVENTS; i++)
        if (events[i].handle && events[i].owner == owner)
            memset(&events[i], 0, sizeof(events[i]));
}
int events_exiting(void)
{
    return exiting;
}
EFI_TPL events_tpl(void)
{
    return current_tpl;
}
void events_init(void)
{
    memset(events, 0, sizeof(events));
    current_tpl = TPL_APPLICATION;
    exiting = before_notified = 0;
    boot_services.RaiseTPL = raise_tpl;
    boot_services.RestoreTPL = restore_tpl;
    boot_services.CreateEvent = create;
    boot_services.CreateEventEx = create_ex;
    boot_services.SetTimer = set_timer;
    boot_services.SignalEvent = signal_event;
    boot_services.CloseEvent = close_event;
    boot_services.CheckEvent = check_event;
    boot_services.WaitForEvent = wait_event;
}
