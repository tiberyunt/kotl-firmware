/* SPDX-License-Identifier: MIT */
#ifndef DEVICE_PATH_H
#define DEVICE_PATH_H
#include "efi-device-path.h"
#include "storage.h"
EFI_STATUS device_path_validate(const void *,UINTN,UINTN *,UINTN *);
EFI_STATUS device_path_resolve(const void *,EFI_HANDLE *,const void **);
EFI_STATUS device_path_read(const void *,void **,UINTN *,EFI_HANDLE *);
EFI_STATUS device_path_file(const CHAR16 *,void **);
int device_path_partition_matches(const void *);
EFI_STATUS platform_partition_path(const void *);
EFI_STATUS device_path_publish(void);
extern EFI_PARTITION_PATH partition_path;
#endif
