#ifndef SERVICES_H
#define SERVICES_H
#include "platform.h"
extern EFI_SYSTEM_TABLE system_table;
extern EFI_BOOT_SERVICES boot_services;
extern UINTN map_key;
extern int boot_active;
extern EFI_HANDLE running_image;
extern const uint8_t *external_image;
extern UINTN external_image_size;
void variables_init(EFI_RUNTIME_SERVICES *);
void events_init(void);
void events_poll(void);
void events_signal_group(const EFI_GUID *);
void events_clear(EFI_EVENT);
void events_before_exit(void);
void events_exit_boot(void);
void events_unload_image(EFI_HANDLE);
int events_exiting(void);
EFI_TPL events_tpl(void);
void services_init(void);
void protocols_init(void);
EFI_STATUS protocol_publish(EFI_HANDLE, const EFI_GUID *, void *);
void protocol_remove_handle(EFI_HANDLE);
void print_map(void);
EFI_STATUS load_image(uint8_t, EFI_HANDLE, void *, void *, UINTN, EFI_HANDLE *);
EFI_STATUS start_image(EFI_HANDLE, UINTN *, CHAR16 **);
EFI_STATUS exit_image(EFI_HANDLE,EFI_STATUS,UINTN,CHAR16 *);
EFI_STATUS unload_image(EFI_HANDLE);
EFI_STATUS boot_options_run(void);
EFI_STATUS boot_options_set_default(void *,const CHAR16 *);
void image_set_device(EFI_HANDLE, EFI_HANDLE);
#endif
