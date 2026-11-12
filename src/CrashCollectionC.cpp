// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "CrashCollection.h"
#include "DeviceDiscovery.h"
#include "platform/Exception.h"
#include "qmdc_crash_collection.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <KL/kLogger.h>
#include <list>
#include <memory>
#include <mutex>
#include <string>

struct QmdcCrashCollectionOpaque
{
   explicit QmdcCrashCollectionOpaque(uint64_t handle)
   : device_handle(handle)
   , initialized(false)
   {
      QC::DeviceInfo info;
      info.deviceHandle = handle;
      collection = std::make_unique<QC::CrashCollection>(info);
   }

   uint64_t device_handle;
   std::unique_ptr<QC::CrashCollection> collection;
   bool initialized;
};

namespace {
thread_local std::string g_last_error;
std::mutex g_api_mutex;

struct RuntimeInitialization
{
   int32_t status = QMDC_OK;
   std::string error;
};

int32_t fail(int32_t code, const std::string& message)
{
   g_last_error = message;
   return code;
}

void copy_string(const std::string& value, char* buffer, size_t capacity)
{
   if(buffer == nullptr || capacity == 0) return;
   const size_t count = std::min(value.size(), capacity - 1);
   std::memcpy(buffer, value.data(), count);
   buffer[count] = '\0';
}

void clear_device(QmdcDevice& device)
{
   copy_string("", device.description, device.description_capacity);
   copy_string("", device.serial_number, device.serial_number_capacity);
   copy_string("", device.adb_serial_number, device.adb_serial_number_capacity);
   copy_string("", device.location, device.location_capacity);
   copy_string("", device.edl_chip_id, device.edl_chip_id_capacity);
}

int32_t initialize_runtime()
{
   static std::once_flag init_flag;
   static RuntimeInitialization result;

   std::call_once(init_flag, [] {
      std::error_code error;
      for(const char* directory: {BASE_DIR, LOG_DIR, PTRACE_DIR, TMP_DIR})
      {
         std::filesystem::create_directories(directory, error);
         if(error)
         {
            result.status = QMDC_ERROR;
            result.error = "Failed to create QMDC runtime directory " + std::string(directory) + ": " + error.message();
            return;
         }
      }

      KL::Logger::get_instance().init(LOG_DIR, PTRACE_DIR, 0, KL::MAX_LOG_FILE_SIZE, "");
   });

   if(result.status != QMDC_OK) return fail(result.status, result.error);
   return QMDC_OK;
}
} // namespace

extern "C" {

uint32_t qmdc_c_api_version(void)
{
   return QMDC_C_API_VERSION;
}

int32_t qmdc_start_monitoring(void)
{
   try
   {
      std::lock_guard<std::mutex> lock(g_api_mutex);
      if(initialize_runtime() != QMDC_OK) return QMDC_ERROR;
      QC::DeviceDiscovery::startMonitoring();
      g_last_error.clear();
      return QMDC_OK;
   }
   catch(const std::exception& e)
   {
      return fail(QMDC_ERROR, e.what());
   }
   catch(...)
   {
      return fail(QMDC_ERROR, "Unknown error starting device monitoring");
   }
}

int32_t qmdc_stop_monitoring(void)
{
   try
   {
      std::lock_guard<std::mutex> lock(g_api_mutex);
      QC::DeviceDiscovery::stopMonitoring();
      g_last_error.clear();
      return QMDC_OK;
   }
   catch(const std::exception& e)
   {
      return fail(QMDC_ERROR, e.what());
   }
   catch(...)
   {
      return fail(QMDC_ERROR, "Unknown error stopping device monitoring");
   }
}

int32_t qmdc_get_devices(QmdcDevice* devices, size_t capacity, size_t* written, size_t* required)
{
   if(written == nullptr || required == nullptr || (capacity != 0 && devices == nullptr))
      return fail(QMDC_INVALID_ARGUMENT, "Invalid device buffer arguments");

   try
   {
      std::lock_guard<std::mutex> lock(g_api_mutex);
      if(initialize_runtime() != QMDC_OK) return QMDC_ERROR;
      const std::list<QC::DeviceInfo> found = QC::DeviceDiscovery::getDeviceList();
      *required = found.size();
      *written = 0;
      for(const QC::DeviceInfo& source: found)
      {
         if(*written == capacity) break;
         QmdcDevice& target = devices[*written];
         target.device_handle = source.deviceHandle;
         clear_device(target);
         copy_string(source.description, target.description, target.description_capacity);
         copy_string(source.serialNumber, target.serial_number, target.serial_number_capacity);
         copy_string(source.adbSerialNumber, target.adb_serial_number, target.adb_serial_number_capacity);
         copy_string(source.location, target.location, target.location_capacity);
         copy_string(source.edlChipId, target.edl_chip_id, target.edl_chip_id_capacity);
         ++*written;
      }
      g_last_error.clear();
      return (*written < *required) ? QMDC_ERROR : QMDC_OK;
   }
   catch(const std::exception& e)
   {
      return fail(QMDC_ERROR, e.what());
   }
   catch(...)
   {
      return fail(QMDC_ERROR, "Unknown error enumerating devices");
   }
}

QmdcCrashCollectionHandle qmdc_crash_collection_create(uint64_t device_handle)
{
   if(device_handle == 0)
   {
      fail(QMDC_INVALID_DEVICE, "Invalid device handle");
      return nullptr;
   }
   try
   {
      if(initialize_runtime() != QMDC_OK) return nullptr;
      g_last_error.clear();
      return new QmdcCrashCollectionOpaque(device_handle);
   }
   catch(const std::exception& e)
   {
      fail(QMDC_ERROR, e.what());
      return nullptr;
   }
   catch(...)
   {
      fail(QMDC_ERROR, "Unknown error creating crash collection");
      return nullptr;
   }
}

void qmdc_crash_collection_destroy(QmdcCrashCollectionHandle handle)
{
   delete handle;
}

int32_t qmdc_crash_collection_collect(QmdcCrashCollectionHandle handle, const char* output_path)
{
   if(handle == nullptr || output_path == nullptr || output_path[0] == '\0')
      return fail(QMDC_INVALID_ARGUMENT, "A collection handle and output path are required");

   try
   {
      if(initialize_runtime() != QMDC_OK) return QMDC_ERROR;
      std::lock_guard<std::mutex> lock(g_api_mutex);
      QC::ErrorType result = handle->collection->initializeService();
      if(result.errorCode != QC::ErrorCode::DEVICE_NO_ERROR)
         return fail(result.errorCode, "QMDC initializeService failed: " + result.errorString);
      handle->initialized = true;

      QC::MemoryDumpOptions options{std::string(output_path)};
      result = handle->collection->collectMemoryDumpWithOptions(options);
      if(result.errorCode == QC::ErrorCode::DEVICE_NO_ERROR) result = handle->collection->resetDevice(0);
      QC::DeviceDiscovery::stopMonitoring();
      handle->collection->destroyService();
      handle->initialized = false;
      if(result.errorCode != QC::ErrorCode::DEVICE_NO_ERROR)
         return fail(
            result.errorCode,
            "QMDC crash collection/reset failed for output path '" + std::string(output_path) +
               "': " + result.errorString
         );
      g_last_error.clear();
      return QMDC_OK;
   }
   catch(const std::exception& e)
   {
      if(handle->initialized)
      {
         TOOLS_IGNORE_EXCEPTIONS(QC::DeviceDiscovery::stopMonitoring());
         TOOLS_IGNORE_EXCEPTIONS(handle->collection->destroyService());
         handle->initialized = false;
      }
      return fail(QMDC_ERROR, e.what());
   }
   catch(...)
   {
      if(handle->initialized)
      {
         TOOLS_IGNORE_EXCEPTIONS(QC::DeviceDiscovery::stopMonitoring());
         TOOLS_IGNORE_EXCEPTIONS(handle->collection->destroyService());
         handle->initialized = false;
      }
      return fail(QMDC_ERROR, "Unknown error collecting memory dump");
   }
}

int32_t qmdc_last_error(char* buffer, size_t capacity, size_t* required_size)
{
   if(required_size == nullptr || (capacity != 0 && buffer == nullptr)) return QMDC_INVALID_ARGUMENT;
   *required_size = g_last_error.size() + 1;
   copy_string(g_last_error, buffer, capacity);
   return QMDC_OK;
}
}
