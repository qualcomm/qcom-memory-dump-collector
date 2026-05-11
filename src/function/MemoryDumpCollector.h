// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "device/Fwd.h"
#include "function/Fwd.h"
#include "protocol/Sahara.h"
#include "util/AppEvent.h"
#include "util/Event.h"
#include "util/ThreadHelper.h"

#include <filesystem>
namespace Function {

// ----------------------------------------------------------------------------
// MemoryDumpCollectorEvent
//
/// Notifies differnt stages of the dump collection
// ----------------------------------------------------------------------------
class MemoryDumpCollectorEvent : public Util::Event
{
   TOOLS_FORBID_COPY(MemoryDumpCollectorEvent);

public:
   enum EventId
   {
      INITIALIZE = 0,
      DOWNLOADING_TABLE,
      DOWNLOADING_PARTITION,
      DOWNLOADING_PARTITION_COMPLETE,
      DOWNLOADING_PROGRESS_REPORT,
      DOWNLOADING_DDR_DATA,
      RESETTING
   };

   MemoryDumpCollectorEvent(EventId eventId, const std::string& description);
   virtual ~MemoryDumpCollectorEvent();

   EventId getEventId() const;
   std::string getDescription() const;

private:
   EventId m_eventId;
   std::string m_description;
};

class EfsSyncWorker;

class DumpInfoFile
{
   TOOLS_FORBID_COPY(DumpInfoFile);

public:
   DumpInfoFile(std::filesystem::path savePath);
   ~DumpInfoFile();
   void ReportV(const char* format, ...);

private:
   Device::AccessibleFilePtr m_pDumpInfo;
};
// ----------------------------------------------------------------------------
// MemoryDumpCollector
//
/// Collects a memory dump over the Sahara protocol
// ----------------------------------------------------------------------------
class MemoryDumpCollector
: public Util::EventPublisher
, public std::enable_shared_from_this<MemoryDumpCollector>
{
   TOOLS_FORBID_COPY(MemoryDumpCollector);

public:
#pragma pack(push, 1)
   enum StorageCollatedType
   {
      STORAGE_COLLATED_NONE = 0,
      STORAGE_COLLATED = 1,
      STORAGE_COLLATED_AND_COMPRESSED = 2
   };

   enum BootRawPartitionDumpSectionType
   {
      RAW_PARTITION_DUMP_RESERVED = 0,
      RAW_PARTITION_DUMP_DDR_TYPE = 1,         // Device memory dump
      RAW_PARTITION_DUMP_CPU_CXT_TYPE = 2,     // CPU context
      RAW_PARTITION_DUMP_SV_TYPE = 3,          // Silicon Vendor specific data
      RAW_PARTITION_DUMP_DUMP_REASON_TYPE = 4, // Reset reason
      RAW_PARTITION_DUMP_MAX = 0x7FFFFFFF
   };

   struct RawDumpDdrRange
   {
      uint64_t baseAddress;
   };

   struct RawDumpCpuContext
   {
      uint16_t achitecture;
      uint32_t coreCount;
      uint32_t contextSize;
   };

   struct RawDumpSvSpecific
   {
      union TypeSpecificInformation
      {
         uint8_t svSpecific[16];
         uint64_t baseAddress; // Qualcomm specific usage
      } typeSpecificInformation;
   };

   struct RawDumpDumpReason
   {
      uint32_t parameter1;
      uint32_t parameter2;
      uint32_t parameter3;
      uint32_t parameter4;
   };

   struct RawDumpSectionHeader
   {
      uint32_t flags;
      uint32_t version;
      BootRawPartitionDumpSectionType type;
      uint64_t offset;
      uint64_t size;
      union TypeSpecificInformation
      {
         RawDumpDdrRange rawDumpDdrRange;
         RawDumpCpuContext rawDumpCpuContext;
         RawDumpSvSpecific rawDumpSvSpecific;
         RawDumpDumpReason rawDumpDumpReason;
      } typeSpecificInformation;
      uint8_t name[20];
   };

   struct RawDumpHeader
   {
      uint8_t signature[8];
      uint32_t version;
      uint32_t flags;
      uint8_t osData[8];
      uint64_t cpuContext;
      uint32_t resetTrigger;
      uint64_t dumpSize;
      uint64_t totalDumpSizeRequired;
      uint32_t sectionCount;
      // RawDumpSectionHeader* sectionTable;
   };
#pragma pack(pop)

   static const std::filesystem::path DDR_STORE_DEFAULT_FILENAME;

   MemoryDumpCollector(const Device::ConnectionPtr& pConnection);
   virtual ~MemoryDumpCollector();

   void collectMemoryDump(
      const std::filesystem::path& savePath,
      const std::vector<std::string>& sectionNames = std::vector<std::string>(),
      StorageCollatedType storageCollatedType = STORAGE_COLLATED_NONE
   );

   void startRemoteEfsSync(const std::filesystem::path& savePath);
   void stopRemoteEfsSync();

   void collectDdrData(const std::filesystem::path& savePath);

   void reset();

   Device::ConnectionPtr getConnection();

private:
   friend class EfsSyncWorker;
   std::shared_ptr<EfsSyncWorker> m_pEfsSyncWorker;
   std::shared_ptr<Util::StdThreadWrapper> m_pEfsSyncWorkerThread;

protected:
   void downloadMemoryRegion(
      const Device::AccessibleFilePtr& pDumpFile,
      const std::filesystem::path& outputFile,
      uint64_t memoryAddress,
      uint64_t memoryLength,
      bool b64Bit,
      uint64_t totalSize,
      uint64_t& currentSize
   );

   void downloadPartitions(
      const std::filesystem::path& savePath,
      const std::vector<std::string>& sectionNames = std::vector<std::string>(),
      StorageCollatedType storageCollatedType = STORAGE_COLLATED_NONE
   );

   Device::ConnectionPtr m_pConnection; ///< Connection to Sahara
   std::filesystem::path m_efsSyncPath;
};

} // namespace Function
