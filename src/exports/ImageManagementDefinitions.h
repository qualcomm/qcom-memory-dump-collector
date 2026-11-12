// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "Definitions.h"

#include <cstdint>
#include <string>
#include <vector>

namespace QC {
#pragma pack(push, 1)

struct DeviceImageMode
{
   enum type
   {
      DEVICE_IMAGE_MODE_NONE = 0x00000000,
      DEVICE_IMAGE_MODE_SAHARA_DOWNLOAD = 0x00000001,
      DEVICE_IMAGE_MODE_SAHARA_CRASH = 0x00000002,
      DEVICE_IMAGE_MODE_SAHARA_EFS_SYNC = 0x00000004
   };
};

struct StorageCollatedType
{
   enum type
   {
      /** Raw binary data only */
      STORAGE_COLLATED_NONE = 0,
      /** Raw binary data and collated head */
      STORAGE_COLLATED = 1,
      /** Raw binary data and collated head and zipped */
      STORAGE_COLLATED_AND_COMPRESSED = 2
   };
};

typedef struct _StorageOptions__isset
{
   _StorageOptions__isset()
   : collatedType(false)
   {
   }
   bool collatedType : 1;
} _StorageOptions__isset;

typedef struct _MemoryDumpOptions__isset
{
   _MemoryDumpOptions__isset()
   : pathName(false)
   , sectionNameList(false)
   {
   }
   bool pathName : 1;
   bool sectionNameList : 1;
   bool storageOptions : 1;
} _MemoryDumpOptions__isset;

#pragma pack(pop)

// Structs and classes with C++ objects (std::string, std::vector, std::map)
// must use default alignment

class StorageOptions
{
public:
   StorageOptions(StorageCollatedType::type collatedType);
   ~StorageOptions() noexcept;
   _StorageOptions__isset __isset;

   StorageCollatedType::type collatedType;

   void __set_collatedType(const StorageCollatedType::type val);

private:
   friend class MemoryDumpOptions;
   StorageOptions();
};

class MemoryDumpOptions
{
public:
   MemoryDumpOptions(std::string pathName);
   ~MemoryDumpOptions();
   _MemoryDumpOptions__isset __isset;

   std::string pathName;
   /*optional*/ std::vector<std::string> sectionNameList;
   /*optional*/ StorageOptions storageOptions;

   void __set_pathName(const std::string& val);
   void __set_sectionNameList(const std::vector<std::string>& val);
   void __set_storageOptions(const StorageOptions& val);
};

}; // namespace QC
