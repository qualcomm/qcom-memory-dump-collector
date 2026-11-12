// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once

#include "qmdc_export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QMDC_C_API_VERSION 1u

typedef enum QmdcStatus
{
   QMDC_OK = 0,
   QMDC_ERROR = 1,
   QMDC_INVALID_ARGUMENT = 2,
   QMDC_NOT_INITIALIZED = 18,
   QMDC_INVALID_DEVICE = 4
} QmdcStatus;

typedef struct QmdcDevice
{
   uint64_t device_handle;
   char* description;
   size_t description_capacity;
   char* serial_number;
   size_t serial_number_capacity;
   char* adb_serial_number;
   size_t adb_serial_number_capacity;
   char* location;
   size_t location_capacity;
   char* edl_chip_id;
   size_t edl_chip_id_capacity;
} QmdcDevice;

typedef struct QmdcCrashCollectionOpaque* QmdcCrashCollectionHandle;

QMDC_API uint32_t qmdc_c_api_version(void);

QMDC_API int32_t qmdc_start_monitoring(void);
QMDC_API int32_t qmdc_stop_monitoring(void);
QMDC_API int32_t qmdc_get_devices(QmdcDevice* devices, size_t capacity, size_t* written, size_t* required);

QMDC_API QmdcCrashCollectionHandle qmdc_crash_collection_create(uint64_t device_handle);
QMDC_API void qmdc_crash_collection_destroy(QmdcCrashCollectionHandle handle);
QMDC_API int32_t qmdc_crash_collection_collect(QmdcCrashCollectionHandle handle, const char* output_path);

QMDC_API int32_t qmdc_last_error(char* buffer, size_t capacity, size_t* required_size);

#ifdef __cplusplus
}
#endif
