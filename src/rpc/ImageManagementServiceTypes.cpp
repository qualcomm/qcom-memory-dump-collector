// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "ImageManagementDefinitions.h"

namespace QC {

StorageOptions::~StorageOptions() noexcept
{
}
void StorageOptions::__set_collatedType(const StorageCollatedType::type val)
{
   this->collatedType = val;
}
StorageOptions::StorageOptions(StorageCollatedType::type collatedType)
{
   this->collatedType = collatedType;
   __isset.collatedType = true;
}
StorageOptions::StorageOptions()
{
}


MemoryDumpOptions::~MemoryDumpOptions() noexcept
{
}
void MemoryDumpOptions::__set_pathName(const std::string& val)
{
   this->pathName = val;
}
void MemoryDumpOptions::__set_sectionNameList(const std::vector<std::string>& val)
{
   this->sectionNameList = val;
   __isset.sectionNameList = true;
}
void MemoryDumpOptions::__set_storageOptions(const StorageOptions& val)
{
   this->storageOptions = val;
   __isset.storageOptions = true;
}
MemoryDumpOptions::MemoryDumpOptions(std::string pathName)
{
   this->pathName = pathName;
   __isset.pathName = true;
}

} // namespace QC
