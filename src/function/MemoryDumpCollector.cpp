// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "function/MemoryDumpCollector.h"

#include "device/Buffer.h"
#include "device/Connection.h"
#include "device/ErrorMessage.h"
#include "device/Exception.h"
#include "report/Thread.h"
#include "util/MemoryHelper.h"
#include "util/StringHelper.h"
#include "util/SystemHelper.h"
#include "util/ThisThread.h"
#include "util/TimeHelper.h"
#include "Version.h"

#include <chrono>
#include <cstdarg>

using namespace Device::Protocol;

static const uint32_t MAX_UINT32_FILE_SIZE = 0xFFFFFFFEU;

namespace Function {

static const uint32_t MAX_SAHARA_READ = 1048576;
const std::filesystem::path MemoryDumpCollector::DDR_STORE_DEFAULT_FILENAME = std::filesystem::path("mdmddr.mbn");

static const size_t SAHARA_STRING_LENGTH = 20;
struct DebugTableEntry
{
   uint32_t m_savePref;
   uint32_t m_memoryBase;
   uint32_t m_length;
   char m_description[SAHARA_STRING_LENGTH];
   char m_fileName[SAHARA_STRING_LENGTH];
};

struct DebugTableEntry64Bit
{
   uint64_t m_savePref;
   uint64_t m_memoryBase;
   uint64_t m_length;
   char m_description[SAHARA_STRING_LENGTH];
   char m_fileName[SAHARA_STRING_LENGTH];
};

// ----------------------------------------------------------------------------
// saharaPrintBuffer
//
/// Prints the given buffer to a string
// ----------------------------------------------------------------------------
std::string saharaPrintBuffer(const Device::SharedByteBufferPtr& pBuffer)
{
   std::ostringstream ss;
   ss << std::hex << std::uppercase;
   const uint8_t* pByte = pBuffer->begin();
   const uint8_t* pEnd =
      std::min(static_cast<const uint8_t*>(pBuffer->end()), static_cast<const uint8_t*>(pByte + 256));
   for(; pByte < pEnd; ++pByte)
   {
      ss << static_cast<int>(*pByte) << " ";
   }

   return ss.str();
}

// ----------------------------------------------------------------------------
// EfsSyncWorker
//
/// Sync remote EFS in the background
// ----------------------------------------------------------------------------
class EfsSyncWorker : public Device::Report::Thread::Work
{
   TOOLS_FORBID_COPY(EfsSyncWorker);

public:
   EfsSyncWorker(const MemoryDumpCollectorPtr& pParent)
   : Device::Report::Thread::Work(pParent->getConnection()->getProtocol()->getDescription() + " EFS Sync Thread")
   , m_pParent(pParent)
   {
   }

   virtual ~EfsSyncWorker()
   {
   }

   virtual void onRun()
   {
      try
      {
         Util::createPath(m_pParent->m_efsSyncPath);

         while(!isStopSignaled())
         {
            SaharaPtr pSahara = (m_pParent->m_pConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();
            Sahara::Mode mode = pSahara->getMode();

            TOOLS_ASSERT_OR_THROW(
               Sahara::Mode::MODE_EFS_SYNC == mode,
               Device::Exception(
                  Device::Exception::DEVICE_INVALID_PROTOCOL_HANDLE,
                  "Not in unknown mode in Sahara protocol: " + pSahara->getDescription()
               )
            );

            Device::SharedByteBufferPtr pReadDataBuffer;
            Device::SharedByteBufferPtr pSendBuffer;
            const Sahara::FrameHeader* pHeader;
            while(!isStopSignaled())
            {
               const Sahara::Hello* pHello;

               // Get a Sahara data frame from the device
               pReadDataBuffer = pSahara->getFrame(Device::Protocol::SAHARA_RX_WAIT_INTERVAL);

               // Try to reset Sahara state machine if no response from device
               if(pReadDataBuffer == nullptr)
               {
                  continue;
               }

               m_pParent
                  ->notify(std::make_shared<
                           MemoryDumpCollectorEvent>(MemoryDumpCollectorEvent::INITIALIZE, "Initializing EFS sync"));

               pHeader =
                  Util::buffer_cast<const Sahara::FrameHeader*>(pReadDataBuffer->begin(), pReadDataBuffer->size());

               if(Sahara::CommandId::SAHARA_HELLO == pHeader->m_command &&
                  TOOLS_SIZEOF(Sahara::Hello) == pHeader->m_length)
               {
                  pHello = Util::buffer_cast<const Sahara::Hello*>(pReadDataBuffer->begin(), pReadDataBuffer->size());

                  TOOLS_ASSERT_OR_THROW(
                     Sahara::Mode::MODE_MEMORY_DEBUG == pHello->m_mode,
                     Device::Exception(
                        Device::Exception::DEVICE_INVALID_PACKET,
                        "Incorrect mode for EFS sync in Sahara "
                        "protocol: " +
                           pSahara->getDescription()
                     )
                  );
                  pSahara->setSaharaVersion(pHello->m_versionNumber);
                  // Send a hello response
                  pSendBuffer = pSahara->createCommand<Sahara::HelloResponse>(Sahara::SAHARA_HELLO_RESP);
                  Sahara::HelloResponse* pHelloResp =
                     Util::buffer_cast<Sahara::HelloResponse*>(pSendBuffer->begin(), pSendBuffer->size());
                  pHelloResp->m_versionNumber = pSahara->getSaharaVersion();
                  pHelloResp->m_versionCompatible = Sahara::SUPPORTED_LOWEST_VERSION;
                  pHelloResp->m_status = Sahara::STATUS_SUCCESS;
                  pHelloResp->m_mode = Sahara::Mode::MODE_MEMORY_DEBUG;
                  pHelloResp->m_reserved[0] = 1;
                  pHelloResp->m_reserved[1] = 2;
                  pHelloResp->m_reserved[2] = 3;
                  pHelloResp->m_reserved[3] = 4;
                  pHelloResp->m_reserved[4] = 5;
                  pHelloResp->m_reserved[5] = 6;
                  m_pParent->m_pConnection->sendSync(pSendBuffer);
               }

               // If the tool doesn't respond to the Sahara response from the
               // device within 10 seconds, the device will crash Even if the
               // partition download fails, continue to reset the device to
               // avoid a crash
               try
               {
                  m_pParent->downloadPartitions(m_pParent->m_efsSyncPath);
               }
               TOOLS_CATCH(e, APP_REPORT_EXCEPTION(e));

               // Sahara reset after each sync session
               Device::SharedByteBufferPtr pResetBuffer = pSahara->createCommand<Sahara::Reset>(Sahara::SAHARA_RESET);
               m_pParent->m_pConnection->sendSync(pResetBuffer);

               // Get a Sahara data frame from the device
               pReadDataBuffer = pSahara->getFrame();
               pHeader =
                  Util::buffer_cast<const Sahara::FrameHeader*>(pReadDataBuffer->begin(), pReadDataBuffer->size());
               TOOLS_ASSERT_OR_THROW(
                  Sahara::SAHARA_RESET_RESP == pHeader->m_command &&
                     TOOLS_SIZEOF(Sahara::ResetResponse) == pHeader->m_length,
                  Device::Exception(
                     Device::Exception::DEVICE_INVALID_PACKET,
                     "Incorrect response command in Sahara "
                     "protocol: " +
                        pSahara->getDescription()
                  )
               );
            }
         }
      }
      TOOLS_CATCH(e, APP_REPORT_EXCEPTION(e));
   }

private:
   friend class MemoryDumpCollector;

   Util::CheckedPointer<MemoryDumpCollector> m_pParent;
};

// ----------------------------------------------------------------------------
// MemoryDumpCollectorEvent
//
// ----------------------------------------------------------------------------
MemoryDumpCollectorEvent::MemoryDumpCollectorEvent(EventId eventId, const std::string& description)
: Util::Event()
, m_eventId(eventId)
, m_description(description)
{
}

// ----------------------------------------------------------------------------
// ~MemoryDumpCollectorEvent
//
// ----------------------------------------------------------------------------
MemoryDumpCollectorEvent::~MemoryDumpCollectorEvent()
{
}

// ----------------------------------------------------------------------------
// getEventId
//
// ----------------------------------------------------------------------------
MemoryDumpCollectorEvent::EventId MemoryDumpCollectorEvent::getEventId() const
{
   return m_eventId;
}

// ----------------------------------------------------------------------------
// getDescription
//
// ----------------------------------------------------------------------------
std::string MemoryDumpCollectorEvent::getDescription() const
{
   return m_description;
}

// ----------------------------------------------------------------------------
// DumpInfoFile
//
// ----------------------------------------------------------------------------
DumpInfoFile::DumpInfoFile(std::filesystem::path savePath)
{
   // Create Dump.txt
   m_pDumpInfo = std::make_shared<Device::AccessibleFile>();
   m_pDumpInfo->open(
      savePath / "dump_info.txt",
      std::ios::out | std::ios::trunc // Text mode (no binary flag)
   );
}

// ----------------------------------------------------------------------------
// ~DumpInfoFile
//
// ----------------------------------------------------------------------------
DumpInfoFile::~DumpInfoFile()
{
   m_pDumpInfo->close();
}

// ----------------------------------------------------------------------------
// ReportV
//
// ----------------------------------------------------------------------------
void DumpInfoFile::ReportV(const char* format, ...)
{
   va_list more;
   va_start(more, format);

   // Get the required buffer size
   va_list args_copy;
   va_copy(args_copy, more);
   int size = vsnprintf(nullptr, 0, format, args_copy);
   va_end(args_copy);

   // Format the string
   std::string line(size + 1, '\0');
   vsnprintf(&line[0], line.size(), format, more);
   line.resize(size); // Remove null terminator

   va_end(more);

   if(line.size() > 0)
   {
      m_pDumpInfo->write(reinterpret_cast<const uint8_t*>(line.c_str()), line.size());
      std::string newline = "\r\n";
      m_pDumpInfo->write(reinterpret_cast<const uint8_t*>(newline.c_str()), newline.size());
   }
}

// ----------------------------------------------------------------------------
// MemoryDumpCollector
//
// ----------------------------------------------------------------------------
MemoryDumpCollector::MemoryDumpCollector(const Device::ConnectionPtr& pConnection)
: m_pConnection(pConnection)
, m_pEfsSyncWorkerThread()
, m_pEfsSyncWorker()
{
}

// ----------------------------------------------------------------------------
// ~MemoryDumpCollector
//
// ----------------------------------------------------------------------------
MemoryDumpCollector::~MemoryDumpCollector()
{
   stopRemoteEfsSync();
}

// ----------------------------------------------------------------------------
// collectMemoryDump
//
/// Saves the memory dump to the given path
// ----------------------------------------------------------------------------
void MemoryDumpCollector::collectMemoryDump(
   const std::filesystem::path& savePath,
   const std::vector<std::string>& sectionNames,
   StorageCollatedType storageCollatedType
)
{
   Util::createPath(savePath);

   SaharaPtr pSahara = (m_pConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();
   Sahara::Mode mode = pSahara->getMode();

   TOOLS_ASSERT_OR_THROW(
      Sahara::Mode::MODE_MEMORY_DEBUG == mode,
      Device::Exception(
         Device::Exception::DEVICE_INVALID_PROTOCOL_HANDLE,
         "No memory debug mode in Sahara protocol: " + pSahara->getDescription()
      )
   );

   notify(std::make_shared<
          MemoryDumpCollectorEvent>(MemoryDumpCollectorEvent::INITIALIZE, "Initializing memory dump collection"));

   Device::SharedByteBufferPtr pReadDataBuffer;
   Device::SharedByteBufferPtr pSendBuffer;
   const Sahara::FrameHeader* pHeader;

   // Look for Sahara hello from device
   pReadDataBuffer = pSahara->getFrame();
   if(pReadDataBuffer != nullptr)
   {
      pHeader = Util::buffer_cast<const Sahara::FrameHeader*>(pReadDataBuffer->begin(), pReadDataBuffer->size());

      TOOLS_ASSERT_OR_THROW(
         Sahara::CommandId::SAHARA_HELLO == pHeader->m_command && TOOLS_SIZEOF(Sahara::Hello) == pHeader->m_length,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "No Sahara hello in Sahara protocol: " + pSahara->getDescription()
         )
      );

      const Sahara::Hello* pHello;
      pHello = Util::buffer_cast<const Sahara::Hello*>(pReadDataBuffer->begin(), pReadDataBuffer->size());

      TOOLS_ASSERT_OR_THROW(
         Sahara::Mode::MODE_MEMORY_DEBUG == pHello->m_mode,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Incorrect mode in Sahara protocol: " + pSahara->getDescription()
         )
      );
      pSahara->setSaharaVersion(pHello->m_versionNumber);
   }
   // Send a hello response regardless of previous Sahara protocol stage
   pSendBuffer = pSahara->createCommand<Sahara::HelloResponse>(Sahara::SAHARA_HELLO_RESP);
   Sahara::HelloResponse* pHelloResp =
      Util::buffer_cast<Sahara::HelloResponse*>(pSendBuffer->begin(), pSendBuffer->size());
   pHelloResp->m_versionNumber = pSahara->getSaharaVersion();
   pHelloResp->m_versionCompatible = Sahara::SUPPORTED_LOWEST_VERSION;
   pHelloResp->m_status = Sahara::STATUS_SUCCESS;
   pHelloResp->m_mode = Sahara::Mode::MODE_MEMORY_DEBUG;
   pHelloResp->m_reserved[0] = 1;
   pHelloResp->m_reserved[1] = 2;
   pHelloResp->m_reserved[2] = 3;
   pHelloResp->m_reserved[3] = 4;
   pHelloResp->m_reserved[4] = 5;
   pHelloResp->m_reserved[5] = 6;
   m_pConnection->sendSync(pSendBuffer);

   downloadPartitions(savePath, sectionNames, storageCollatedType);
}

// ----------------------------------------------------------------------------
// startRemoteEfsSync
//
/// Start processing remote EFS updates
// ----------------------------------------------------------------------------
void MemoryDumpCollector::startRemoteEfsSync(const std::filesystem::path& savePath)
{
   stopRemoteEfsSync();

   m_efsSyncPath = savePath;
   m_pEfsSyncWorker = std::make_shared<EfsSyncWorker>(shared_from_this());
   m_pEfsSyncWorkerThread = std::make_shared<Util::StdThreadWrapper>(m_pEfsSyncWorker);
   m_pEfsSyncWorkerThread->start();
}

// ----------------------------------------------------------------------------
// stopRemoteEfsSync
//
/// Stop processing remote EFS updates
// ----------------------------------------------------------------------------
void MemoryDumpCollector::stopRemoteEfsSync()
{
   if(m_pEfsSyncWorkerThread != nullptr)
   {
      m_pEfsSyncWorkerThread->stop();
      m_pEfsSyncWorkerThread->waitForStop();
      m_pEfsSyncWorkerThread = NULL;
      m_pEfsSyncWorker = NULL;
   }
}

// ----------------------------------------------------------------------------
// downloadMemoryRegion
//
/// Downloads a chunk
// ----------------------------------------------------------------------------
void MemoryDumpCollector::downloadMemoryRegion(
   const Device::AccessibleFilePtr& pDumpFile,
   const std::filesystem::path& outputFile,
   uint64_t memoryAddress,
   uint64_t memoryLength,
   bool b64Bit,
   uint64_t totalSize,
   uint64_t& currentSize
)
{
   SaharaPtr pSahara = (m_pConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();

   FLOG_INFO("Saving memory dump file: " + outputFile.filename().string());

   notify(std::make_shared<MemoryDumpCollectorEvent>(
      MemoryDumpCollectorEvent::DOWNLOADING_PARTITION,
      "Downloading partition: " + std::string(outputFile.filename().string().c_str())
   ));

   auto percentageReportTime = std::chrono::system_clock::now();
   uint64_t percentage = currentSize * 100 / totalSize;

   // Non-collated mode: create individual files for each partition
   // Collated mode: write all partitions to the single pDumpFile
   bool isNonCollatedMode = (pDumpFile == nullptr);

   Device::AccessibleFilePtr dumpFile = std::make_shared<Device::AccessibleFile>();
   if(isNonCollatedMode)
   {
      dumpFile->open(outputFile, std::ios::out | std::ios::trunc | std::ios::binary);
   }

   uint64_t bytesRead = 0;
   while(bytesRead < memoryLength)
   {
      uint64_t bytesToRead = std::min<uint64_t>(memoryLength - bytesRead, MAX_SAHARA_READ);
      if(b64Bit)
      {
         Device::SharedByteBufferPtr pSendBuffer =
            pSahara->createCommand<Sahara::MemoryRead64Bit>(Sahara::SAHARA_64_BIT_MEMORY_READ);
         Sahara::MemoryRead64Bit* pMemoryRead =
            Util::buffer_cast<Sahara::MemoryRead64Bit*>(pSendBuffer->begin(), pSendBuffer->size());
         pMemoryRead->m_memoryAddress = memoryAddress;
         pMemoryRead->m_memoryLength = bytesToRead;
         m_pConnection->sendSync(pSendBuffer);
      }
      else
      {
         Device::SharedByteBufferPtr pSendBuffer = pSahara->createCommand<Sahara::MemoryRead>(Sahara::SAHARA_MEMORY_READ
         );
         Sahara::MemoryRead* pMemoryRead =
            Util::buffer_cast<Sahara::MemoryRead*>(pSendBuffer->begin(), pSendBuffer->size());
         pMemoryRead->m_memoryAddress = static_cast<uint32_t>(memoryAddress);
         pMemoryRead->m_memoryLength = static_cast<uint32_t>(bytesToRead);
         m_pConnection->sendSync(pSendBuffer);
      }

      uint64_t receivedData = 0;
      Device::SharedByteBufferPtr pPreviousPartial;
      uint64_t receivedPayload = 0;
      while(receivedData < bytesToRead)
      {
         Device::SharedByteBufferPtr pPartial = pSahara->getFrame();
         if(pPartial == nullptr)
         {
            FLOG_ERROR(
               pSahara->getDescription() + " - Total required data size in byte:" + std::to_string(memoryLength)
            );
            FLOG_ERROR(
               pSahara->getDescription() +
               " - Total received data size in byte:" + std::to_string(bytesRead + receivedData)
            );
            FLOG_ERROR(
               pSahara->getDescription() + " - Payload received after last command:" + std::to_string(receivedPayload)
            );
            if(pPreviousPartial != nullptr)
            {
               // Recording last payload here once failed
               FLOG_ERROR(
                  pSahara->getDescription() + " - Last received payload size (" +
                  std::to_string(pPreviousPartial->size()) + " bytes): " + saharaPrintBuffer(pPreviousPartial)
               );
            }

            TOOLS_THROW(Device::Exception(
               Device::Exception::DEVICE_RESPONSE_ERROR,
               Device::Exception::getErrorJson(
                  ERR_MEMDUMP_DOWNLOAD_FAILURE(std::string(outputFile.filename().string().c_str())),
                  SUGG_MEMDUMP_DOWNLOAD_FAILURE,
                  std::string(POC(TARGET)) + " or " + std::string(POC(BOOT))
               )
            ));
         };
         pPreviousPartial = pPartial;
         receivedData += pPartial->size();
         ++receivedPayload;
         if(isNonCollatedMode)
         {
            dumpFile->write(pPartial->begin(), pPartial->size());
         }
         else
         {
            pDumpFile->write(pPartial->begin(), pPartial->size());
         }
      }
      pPreviousPartial->clear();
      memoryAddress += bytesToRead;
      bytesRead += bytesToRead;
      currentSize += bytesToRead;
      uint64_t newPercentage = currentSize * 100 / totalSize;
      if(newPercentage != percentage)
      {
         // Only update for full percentages to minimize callbacks
         percentage = newPercentage;
         if(std::chrono::system_clock::now() - percentageReportTime > std::chrono::seconds(1))
         {
            notify(std::make_shared<MemoryDumpCollectorEvent>(
               MemoryDumpCollectorEvent::DOWNLOADING_PROGRESS_REPORT,
               "Percentage completed: " + std::to_string(newPercentage)
            ));
            percentageReportTime = std::chrono::system_clock::now();
         }
      }
   }

   if(isNonCollatedMode)
   {
      dumpFile->close();
   }

   notify(std::make_shared<MemoryDumpCollectorEvent>(
      MemoryDumpCollectorEvent::DOWNLOADING_PARTITION_COMPLETE,
      "Partition completed: " + std::string(outputFile.filename().string().c_str())
   ));
}

// ----------------------------------------------------------------------------
// downloadPartitions
//
/// Download available partition images from device
// ----------------------------------------------------------------------------
void MemoryDumpCollector::downloadPartitions(
   const std::filesystem::path& savePath,
   const std::vector<std::string>& sectionNames,
   StorageCollatedType storageCollatedType
)
{
   if(StorageCollatedType::STORAGE_COLLATED_AND_COMPRESSED == storageCollatedType)
   {
      TOOLS_THROW(ToolException("Compression feature not supported"));
   }
   SaharaPtr pSahara = (m_pConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();

   Device::SharedByteBufferPtr pReadDataBuffer;
   Device::SharedByteBufferPtr pSendBuffer;
   const Sahara::FrameHeader* pHeader;

   // Get partition table information
   pReadDataBuffer = pSahara->getFrame();
   pHeader = Util::buffer_cast<const Sahara::FrameHeader*>(pReadDataBuffer->begin(), pReadDataBuffer->size());

   bool b64Bit = false;
   uint64_t tableAddress;
   uint64_t tableLength;
   size_t nPartitions = 0;
   uint64_t totalSize = 0;
   switch(pHeader->m_command)
   {
      case Sahara::SAHARA_MEMORY_DEBUG: {
         const Sahara::MemoryDebug* pMemoryDebug =
            Util::buffer_cast<const Sahara::MemoryDebug*>(pReadDataBuffer->begin(), pReadDataBuffer->size());
         tableAddress = pMemoryDebug->m_memoryTableAddress;
         tableLength = pMemoryDebug->m_memoryTableLength;
         nPartitions = static_cast<size_t>(tableLength / TOOLS_SIZEOF(DebugTableEntry));
      }
      break;
      case Sahara::SAHARA_64_BIT_MEMORY_DEBUG: {
         const Sahara::MemoryDebug64Bit* pMemoryDebug =
            Util::buffer_cast<const Sahara::MemoryDebug64Bit*>(pReadDataBuffer->begin(), pReadDataBuffer->size());
         tableAddress = pMemoryDebug->m_memoryTableAddress;
         tableLength = pMemoryDebug->m_memoryTableLength;
         b64Bit = true;
         nPartitions = static_cast<size_t>(tableLength / TOOLS_SIZEOF(DebugTableEntry64Bit));
      }
      break;
      default:
         TOOLS_THROW(Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Memory Debug not received from Sahara "
            "protocol: " +
               pSahara->getDescription()
         ));
   }

   // Get the partition table
   notify(std::make_shared<MemoryDumpCollectorEvent>(
      MemoryDumpCollectorEvent::DOWNLOADING_TABLE,
      "Downloading partions table, partition count: " + std::to_string(nPartitions)
   ));

   if(b64Bit)
   {
      pSendBuffer = pSahara->createCommand<Sahara::MemoryRead64Bit>(Sahara::SAHARA_64_BIT_MEMORY_READ);
      Sahara::MemoryRead64Bit* pMemoryRead =
         Util::buffer_cast<Sahara::MemoryRead64Bit*>(pSendBuffer->begin(), pSendBuffer->size());
      pMemoryRead->m_memoryAddress = tableAddress;
      pMemoryRead->m_memoryLength = tableLength;
      m_pConnection->sendSync(pSendBuffer);
   }
   else
   {
      pSendBuffer = pSahara->createCommand<Sahara::MemoryRead>(Sahara::SAHARA_MEMORY_READ);
      Sahara::MemoryRead* pMemoryRead =
         Util::buffer_cast<Sahara::MemoryRead*>(pSendBuffer->begin(), pSendBuffer->size());
      pMemoryRead->m_memoryAddress = static_cast<uint32_t>(tableAddress);
      pMemoryRead->m_memoryLength = static_cast<uint32_t>(tableLength);
      m_pConnection->sendSync(pSendBuffer);
   }

   Device::SharedByteBufferPtr pTable = Device::Buffer::createBuffer(static_cast<size_t>(tableLength));
   pTable->resize(0);
   TOOLS_ASSERT_OR_THROW(
      static_cast<uint64_t>(MAX_UINT32_FILE_SIZE) > tableLength,
      Device::Exception(
         Device::Exception::DEVICE_INVALID_PARAMETERS,
         "Invalid length for region table (" + std::to_string(tableLength) + ")" + pSahara->getDescription()
      )
   );
   pTable->reserve(static_cast<size_t>(tableLength));
   while(pTable->size() < tableLength)
   {
      Device::SharedByteBufferPtr pPartial = pSahara->getFrame();
      pTable->append(pPartial->begin(), pPartial->size());
   }

   // Create Dump.txt
   std::shared_ptr<DumpInfoFile> m_pDumpInfo = std::make_shared<DumpInfoFile>(savePath);
   m_pDumpInfo->ReportV(Util::format_time_point(std::chrono::time_point_cast<
                                                   Util::tick_duration>(std::chrono::system_clock::now()))
                           .c_str());
   std::string hostName = Util::getComputerName().c_str();
   std::string userName = Util::getUserName().c_str();
   m_pDumpInfo->ReportV("Host: %s", hostName.c_str());
   m_pDumpInfo->ReportV("User: %s", userName.c_str());

   std::ostringstream version;
   version << std::setw(2) << std::setfill('0') << MAJOR_VERSION << "." << std::setw(2) << std::setfill('0')
           << MINOR_VERSION << "." << PATCH_VERSION;


   // Filter partition table
   std::vector<std::string> sectionNamesWithoutWhitespace = {};
   for(auto it = sectionNames.begin(); it != sectionNames.end(); ++it)
   {
      sectionNamesWithoutWhitespace.push_back(Util::trimCopy(*it));
   }

   std::list<DebugTableEntry64Bit> partitionList;
   if(b64Bit)
   {
      const DebugTableEntry64Bit* pEntry =
         Util::buffer_cast<const DebugTableEntry64Bit*>(pTable->begin(), pTable->size());

      while(reinterpret_cast<const uint8_t*>(pEntry) < pTable->end())
      {
         std::string sectionName(pEntry->m_description);
         Util::toUpper(sectionName);
         Util::trim(sectionName);

         if(sectionNamesWithoutWhitespace.empty() ||
            std::find(sectionNamesWithoutWhitespace.begin(), sectionNamesWithoutWhitespace.end(), sectionName) !=
               sectionNamesWithoutWhitespace.end())
         {
            partitionList.push_back(*pEntry);
         }
         totalSize += pEntry->m_length;
         ++pEntry;
      }
   }
   else
   {
      const DebugTableEntry* pEntry = Util::buffer_cast<const DebugTableEntry*>(pTable->begin(), pTable->size());

      while(reinterpret_cast<const uint8_t*>(pEntry) < pTable->end())
      {
         std::string sectionName(pEntry->m_description);
         Util::toUpper(sectionName);
         Util::trim(sectionName);

         if(sectionNamesWithoutWhitespace.empty() ||
            std::find(sectionNamesWithoutWhitespace.begin(), sectionNamesWithoutWhitespace.end(), sectionName) !=
               sectionNamesWithoutWhitespace.end())
         {
            DebugTableEntry64Bit entry64bit;
            entry64bit.m_savePref = pEntry->m_savePref;
            entry64bit.m_memoryBase = pEntry->m_memoryBase;
            entry64bit.m_length = pEntry->m_length;
            memcpy(entry64bit.m_description, pEntry->m_description, SAHARA_STRING_LENGTH);
            memcpy(entry64bit.m_fileName, pEntry->m_fileName, SAHARA_STRING_LENGTH);
            partitionList.push_back(entry64bit);
         }
         totalSize += pEntry->m_length;
         ++pEntry;
      }
   }

   TOOLS_ASSERT_OR_THROW(
      totalSize > 0,
      Device::Exception(
         Device::Exception::DEVICE_INVALID_PARAMETERS,
         "Invalid total size_t " + std::to_string(tableLength) + " " + pSahara->getDescription()
      )
   );

   // Collated storage handling
   // See Windows Offline Crash Dump Specification 4.5.1
   std::string collatedFilename = "rawdump.bin";

   size_t destOffsetDumpReason = 0, sourceOffsetDumpReason = 0;
   size_t destOffsetCpuContext = 0, sourceOffsetCpuContext = 0;

   Device::AccessibleFilePtr pDumpFile = nullptr;
   if(StorageCollatedType::STORAGE_COLLATED_NONE != storageCollatedType)
   {
      pDumpFile = std::make_shared<Device::AccessibleFile>();
      // Create collated dump file if predefined file name does not exist.
      if(collatedFilename.empty())
      {
         auto createTime = std::chrono::system_clock::now();
         auto time_t_now = std::chrono::system_clock::to_time_t(createTime);
         std::tm tm_local = *std::localtime(&time_t_now);
         std::string dateString =
            "_" + std::to_string(tm_local.tm_year + 1900) + "_" + std::to_string(tm_local.tm_mon + 1) + "_" +
            std::to_string(tm_local.tm_mday) + "_" + std::to_string(tm_local.tm_hour) + "_" +
            std::to_string(tm_local.tm_min) + "_" + std::to_string(tm_local.tm_sec);
         collatedFilename = "rawdump_SN_" + m_pConnection->getProtocol()->getSerialNumber() + dateString + ".bin";
      }

      FLOG_INFO("Generate collated dump: " + collatedFilename);

      pDumpFile->open(
         savePath / std::string(collatedFilename),
         std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary
      );

      // Save Raw Dump header
      RawDumpHeader rawDumpHeader;
      rawDumpHeader.signature[0] = 0x52;
      rawDumpHeader.signature[1] = 0x61;
      rawDumpHeader.signature[2] = 0x77;
      rawDumpHeader.signature[3] = 0x5F;
      rawDumpHeader.signature[4] = 0x44;
      rawDumpHeader.signature[5] = 0x6D;
      rawDumpHeader.signature[6] = 0x70;
      rawDumpHeader.signature[7] = 0x21;
      rawDumpHeader.version = 0x00001012;
      rawDumpHeader.flags = 0x00000001;
      memset(rawDumpHeader.osData, 0, 8);
      rawDumpHeader.cpuContext = 0;
      rawDumpHeader.resetTrigger = 0;
      rawDumpHeader.sectionCount = static_cast<
         uint32_t>(sectionNamesWithoutWhitespace.empty() ? nPartitions : sectionNamesWithoutWhitespace.size());
      rawDumpHeader.dumpSize =
         totalSize + TOOLS_SIZEOF(RawDumpHeader) + rawDumpHeader.sectionCount * TOOLS_SIZEOF(RawDumpSectionHeader);
      rawDumpHeader.totalDumpSizeRequired = rawDumpHeader.dumpSize;

      FLOG_INFO(
         "Dump section count: " + std::to_string(rawDumpHeader.sectionCount) +
         " size_t: " + std::to_string(rawDumpHeader.dumpSize)
      );

      pDumpFile->write(reinterpret_cast<const uint8_t*>(&rawDumpHeader), TOOLS_SIZEOF(RawDumpHeader));

      // Save Raw Dump Section Headers
      RawDumpSectionHeader rawDumpSectionHeader;
      rawDumpSectionHeader.size = 0;
      rawDumpSectionHeader.offset =
         TOOLS_SIZEOF(RawDumpHeader) + rawDumpHeader.sectionCount * TOOLS_SIZEOF(RawDumpSectionHeader);
      for(std::list<DebugTableEntry64Bit>::iterator it = partitionList.begin(); it != partitionList.end(); ++it)
      {
         rawDumpSectionHeader.flags = 0x00000001;
         rawDumpSectionHeader.version = 0x00001000;
         rawDumpSectionHeader.offset += rawDumpSectionHeader.size;
         rawDumpSectionHeader.size = it->m_length;
         rawDumpSectionHeader.type = RAW_PARTITION_DUMP_SV_TYPE;
         rawDumpSectionHeader.typeSpecificInformation.rawDumpSvSpecific.typeSpecificInformation.baseAddress = 0;

         /** Qualcomm specific requirement **/

         std::string description(it->m_description);
         std::string name(it->m_fileName);

         FLOG_INFO(
            "Collated Dump Image " + name + " - " + description + " offset: " +
            std::to_string(rawDumpSectionHeader.offset) + " size: " + std::to_string(rawDumpSectionHeader.size)
         );

         // RAW_PARTITION_DUMP_DUMP_REASON_TYPE
         //    Based on the dump file name "DMP_RESN.BIN", section type is set
         //    to RAW_PARTITION_DUMP_DUMP_REASON_TYPE. We obtain reset reason
         //    from the following sources :
         //       PMIC reset reason using PMIC API pm_pon_get_poff_reason.
         //       TZ OEM reset reason from shared IMEM :
         //          Shared IMEM Base(0x14680000) +
         //          TZ_OEM_RESET_REASON_IMEM_OFFSET(0x7A4)
         //       TME reset reason from shared IMEM :
         //          Shared IMEM Base(0x14680000) +
         //          TME_RESET_REASON_IMEM_OFFSET(0x800)
         //    Above obtained data is added as "DMP_RESN.BIN" region and dumped
         //    during crashdump and then copied to raw_dump_dump_reason_t in
         //    section_info
         if(name.find("DMP_RESN.BIN") == 0)
         {
            FLOG_INFO("Generating header of "
                      "RAW_PARTITION_DUMP_DUMP_REASON_TYPE");
            rawDumpSectionHeader.type = RAW_PARTITION_DUMP_DUMP_REASON_TYPE;

            // Need to copy content of DMP_RESN.BIN region to header (16 bytes)
            // later
            if(it->m_length != TOOLS_SIZEOF(RawDumpDumpReason))
            {
               FLOG_INFO("Invalid Dump Reason Length: " + std::to_string(it->m_length));
            }
            else
            {
               destOffsetDumpReason =
                  TOOLS_SIZEOF(RawDumpHeader) +
                  std::distance(partitionList.begin(), it) * TOOLS_SIZEOF(rawDumpSectionHeader) +
                  offsetof(RawDumpSectionHeader, typeSpecificInformation);
               sourceOffsetDumpReason = rawDumpSectionHeader.offset;
               FLOG_INFO(
                  "To be patched " + name + " from offset: " + std::to_string(sourceOffsetDumpReason) +
                  " to offset: " + std::to_string(destOffsetDumpReason)
               );
            }
         }
         // RAW_PARTITION_DUMP_CPU_CXT_TYPE
         //    Based on the dump file name "CPU_CTXT.BIN", section type is set
         //    to RAW_PARTITION_DUMP_CPU_CXT_TYPE, and the section version is
         //    set to 0x00001001 as per the specification. During crash dump,
         //    XBL will parse cpu context information from shared IMEM.If magic
         //    exists for CPU context info, it will add below regions and dump:
         //       CPU_CTXT.BIN(CPU Context Info) for below base and size
         //       obtained from shared IMEM
         //          cpu_context_base
         //          cpu_context_size
         //       CXT_INFO.BIN(CPU Context Type Specific Info) for below base
         //       and size obtained from shared IMEM
         //          cpu_context_type_specific_info
         //          cpu_context_type_specific_info_size
         else if(name.find("CPU_CTXT.BIN") == 0)
         {
            FLOG_INFO("Generating header of RAW_PARTITION_DUMP_CPU_CXT_TYPE");

            rawDumpSectionHeader.version = 0x00001001;
            rawDumpSectionHeader.type = RAW_PARTITION_DUMP_CPU_CXT_TYPE;

            // CPU context section info is obtained from "CXT_INFO.BIN" and
            // copied to the section info. Only save the destination offset
            // here. Source offset will come from "CXT_INFO.BIN"
            destOffsetCpuContext =
               TOOLS_SIZEOF(RawDumpHeader) +
               std::distance(partitionList.begin(), it) * TOOLS_SIZEOF(rawDumpSectionHeader) +
               offsetof(RawDumpSectionHeader, typeSpecificInformation);
            FLOG_INFO(
               "To be patched " + name + " from unknown offset to offset: " + std::to_string(destOffsetCpuContext)
            );
         }
         else if(name.find("CXT_INFO.BIN") == 0)
         {
            // CPU context section info is obtained from "CXT_INFO.BIN" and
            // copied to the section info of "CPU_CTXT.BIN".
            if(it->m_length != TOOLS_SIZEOF(RawDumpSvSpecific))
            {
               FLOG_INFO("Invalid Context INFO Length: " + std::to_string(it->m_length));
            }
            else
            {
               sourceOffsetCpuContext = rawDumpSectionHeader.offset;
               FLOG_INFO(
                  "Save binary offset for "
                  "RAW_PARTITION_DUMP_CPU_CXT_TYPE: " +
                  std::to_string(sourceOffsetCpuContext)
               );
            }
         }
         // RAW_PARTITION_DUMP_DDR_TYPE
         // For sections larger than 128MB, section type is set to
         // RAW_PARTION_DUMP_DDR_TYPE. Base address is updated in the section
         // info.
         else if(it->m_length > 128 * 1024 * 1024)
         {
            FLOG_INFO(
               "Generating header of RAW_PARTITION_DUMP_DDR_TYPE, "
               "length: " +
               std::to_string(it->m_length)
            );
            rawDumpSectionHeader.type = RAW_PARTITION_DUMP_DDR_TYPE;
            rawDumpSectionHeader.typeSpecificInformation.rawDumpDdrRange.baseAddress = it->m_memoryBase;
         }
         // RAW_PARTITION_DUMP_SV_TYPE
         //    If the section size is less than 4KB, section type is set to
         //    RAW_PARTITION_DUMP_SV_TYPE. If the section size is between 4KB
         //    and 128MB, section type is set to RAW_PARTITION_DUMP_SV_TYPE and
         //    base address is updated in the section info.
         else if(it->m_length < 4 * 1024)
         {
            FLOG_INFO(
               "Generating header of RAW_PARTITION_DUMP_SV_TYPE, "
               "length: " +
               std::to_string(it->m_length)
            );
            rawDumpSectionHeader.type = RAW_PARTITION_DUMP_SV_TYPE;
            memset(
               rawDumpSectionHeader.typeSpecificInformation.rawDumpSvSpecific.typeSpecificInformation.svSpecific,
               0,
               16
            );
         }
         else
         {
            FLOG_INFO(
               "Generating header of RAW_PARTITION_DUMP_SV_TYPE, "
               "length: " +
               std::to_string(it->m_length)
            );
            rawDumpSectionHeader.type = RAW_PARTITION_DUMP_SV_TYPE;
            rawDumpSectionHeader.typeSpecificInformation.rawDumpSvSpecific.typeSpecificInformation.baseAddress =
               it->m_memoryBase;
         }

         memcpy(rawDumpSectionHeader.name, it->m_fileName, 20);
         pDumpFile->write(reinterpret_cast<const uint8_t*>(&rawDumpSectionHeader), TOOLS_SIZEOF(rawDumpSectionHeader));
      }
   }

   // Download the partitions
   m_pDumpInfo->ReportV("QMDC %s", version.str().c_str());
   m_pDumpInfo->ReportV("pref               base     length               "
                        "region            file name");
   m_pDumpInfo->ReportV("------------------------------------------------------"
                        "----------------------");

   // Download the partitions
   uint64_t currentSize = 0;
   uint64_t index = 0;
   std::list<DebugTableEntry64Bit>::iterator it = partitionList.begin();
   while(it != partitionList.end())
   {
      std::string sectionName(it->m_description);
      Util::toUpper(sectionName);
      Util::trim(sectionName);
      std::ostringstream section;
      section << std::dec << std::setw(4) << std::setfill(' ') << it->m_savePref << " 0x" << std::hex << std::setw(16)
              << std::setfill('0') << it->m_memoryBase << " " << std::dec << std::setw(16) << std::setfill(' ')
              << it->m_length << " " << std::setw(20) << std::setfill(' ') << it->m_description << " " << std::setw(20)
              << std::setfill(' ') << it->m_fileName << " ";

      m_pDumpInfo->ReportV(section.str().c_str());
      std::string fileName = it->m_fileName;
      Util::removeChar(fileName, '\\');
      Util::removeChar(fileName, '/');
      downloadMemoryRegion(
         pDumpFile,
         savePath / std::string(fileName),
         it->m_memoryBase,
         it->m_length,
         b64Bit,
         totalSize,
         currentSize
      );
      ++it;
      ++index;
   }
   if(StorageCollatedType::STORAGE_COLLATED_NONE != storageCollatedType)
   {
      // Patch file headers
      if(sourceOffsetDumpReason != 0 && destOffsetDumpReason != 0)
      {
         FLOG_INFO(
            "Patch header of RAW_PARTITION_DUMP_DUMP_REASON_TYPE from "
            "offset: " +
            std::to_string(sourceOffsetDumpReason) + " to offset: " + std::to_string(destOffsetDumpReason)
         );

         uint8_t tempBuffer[TOOLS_SIZEOF(RawDumpDumpReason)];
         try
         {
            pDumpFile->seek(sourceOffsetDumpReason, std::ios::beg);
            pDumpFile->read(tempBuffer, TOOLS_SIZEOF(RawDumpDumpReason));
            pDumpFile->seek(destOffsetDumpReason, std::ios::beg);
            pDumpFile->write(tempBuffer, TOOLS_SIZEOF(RawDumpDumpReason));
         }
         TOOLS_CATCH(e, APP_REPORT_EXCEPTION(e));
      }

      if(sourceOffsetCpuContext != 0 && destOffsetCpuContext != 0)
      {
         FLOG_INFO(
            "Patch header of RAW_PARTITION_DUMP_CPU_CXT_TYPE from "
            "offset " +
            std::to_string(sourceOffsetCpuContext) + " to offset: " + std::to_string(destOffsetCpuContext)
         );
         uint8_t tempBuffer[TOOLS_SIZEOF(RawDumpCpuContext)];
         try
         {
            pDumpFile->seek(sourceOffsetCpuContext, std::ios::beg);
            pDumpFile->read(tempBuffer, TOOLS_SIZEOF(RawDumpCpuContext));
            pDumpFile->seek(destOffsetCpuContext, std::ios::beg);
            pDumpFile->write(tempBuffer, TOOLS_SIZEOF(RawDumpCpuContext));
         }
         TOOLS_CATCH(e, APP_REPORT_EXCEPTION(e));
      }

      pDumpFile->close();
      Util::ThisThread::sleep(std::chrono::seconds(1));
      // If requested compress dump file to save disc space
      if(StorageCollatedType::STORAGE_COLLATED_AND_COMPRESSED == storageCollatedType)
      {
         TOOLS_THROW(ToolException("Compression feature not supported"));
      }
   }
   m_pDumpInfo->ReportV(
      Util::format_time_point_local(std::chrono::time_point_cast<Util::tick_duration>(std::chrono::system_clock::now()))
         .c_str()
   );
}

// ----------------------------------------------------------------------------
// reset
//
/// Resets the device out of Sahara mode
// ----------------------------------------------------------------------------
void MemoryDumpCollector::reset()
{
   SaharaPtr pSahara = (m_pConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();

   // Send the reset command
   Device::SharedByteBufferPtr pResetBuffer = pSahara->createCommand<Sahara::Reset>(Sahara::SAHARA_RESET);

   m_pConnection->sendSync(pResetBuffer);

   notify(std::make_shared<MemoryDumpCollectorEvent>(MemoryDumpCollectorEvent::RESETTING, "Resetting the device"));

   // Get the reset response but do not expect it will arrive
   Util::ThisThread::sleep(std::chrono::seconds(1));
}

// ----------------------------------------------------------------------------
// getConnection
//
/// @returns The connection for which the memory collection is happening
// ----------------------------------------------------------------------------
Device::ConnectionPtr MemoryDumpCollector::getConnection()
{
   return m_pConnection;
}

// ----------------------------------------------------------------------------
// collectDDRData
//
/// Saves the DDR data to the given path on the PC. This needs to be called
/// between ImageTransfer if target initiated cmd mode, typically between image
/// transfer sequence. Target initiated cmd mode means host needs to execute the
/// DDR store command sequence cmd id 8 and cmd id 9 (achieved in this API). So
/// hello with command mode state must be received and hello resp must be sent
/// before calling this API.
// ----------------------------------------------------------------------------
void MemoryDumpCollector::collectDdrData(const std::filesystem::path& savePath)
{
   Util::createPath(savePath);

   try
   {
      SaharaPtr pSahara = (m_pConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();
      Sahara::Mode mode = pSahara->getMode();

      TOOLS_ASSERT_OR_THROW(
         Sahara::Mode::MODE_COMMAND == mode,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PROTOCOL_HANDLE,
            "No command mode in Sahara protocol: " + pSahara->getDescription()
         )
      );

      // Send a hello response
      Device::SharedByteBufferPtr pHelloResponseBuffer =
         pSahara->createCommand<Sahara::HelloResponse>(Sahara::SAHARA_HELLO_RESP);
      Sahara::HelloResponse* pHelloResp =
         Util::buffer_cast<Sahara::HelloResponse*>(pHelloResponseBuffer->begin(), pHelloResponseBuffer->size());
      pHelloResp->m_versionNumber = pSahara->getSaharaVersion();
      pHelloResp->m_versionCompatible = Sahara::SUPPORTED_LOWEST_VERSION;
      pHelloResp->m_status = Sahara::STATUS_SUCCESS;
      pHelloResp->m_mode = mode;
      pHelloResp->m_reserved[0] = 1;
      pHelloResp->m_reserved[1] = 2;
      pHelloResp->m_reserved[2] = 3;
      pHelloResp->m_reserved[3] = 4;
      pHelloResp->m_reserved[4] = 5;
      pHelloResp->m_reserved[5] = 6;
      m_pConnection->sendSync(pHelloResponseBuffer);

      notify(std::make_shared<
             MemoryDumpCollectorEvent>(MemoryDumpCollectorEvent::DOWNLOADING_DDR_DATA, "Start DDR data collection"));

      // If target initiated cmd mode in between image transfer via receiving
      // hello with cmd mode, then caller needs to send hello resp with cmd
      // mode, then call this API to download/store DDR data

      // Get a command ready packet
      Device::SharedByteBufferPtr pCmdReadyBuffer = pSahara->getFrame();
      const Sahara::CommandReady* pCmdReady =
         Util::buffer_cast<const Sahara::CommandReady*>(pCmdReadyBuffer->begin(), pCmdReadyBuffer->size());
      TOOLS_ASSERT_OR_THROW(
         Sahara::SAHARA_COMMAND_READY == pCmdReady->m_header.m_command &&
            TOOLS_SIZEOF(Sahara::CommandReady) == pCmdReady->m_header.m_length,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Command Ready not received from Sahara protocol: " + pSahara->getDescription()
         )
      );


      // Send a cmd exec packet with cmd id 0x08
      Device::SharedByteBufferPtr pSendBuffer =
         pSahara->createCommand<Sahara::CommandExecute>(Sahara::SAHARA_COMMAND_EXECUTE);
      Sahara::CommandExecute* pCmdExecute =
         Util::buffer_cast<Sahara::CommandExecute*>(pSendBuffer->begin(), pSendBuffer->size());
      pCmdExecute->m_clientCommand = Sahara::CMD_GET_CMDID_LIST;
      m_pConnection->sendSync(pSendBuffer);


      // Get cmd exec resp packet
      Device::SharedByteBufferPtr pCmdExecRespBuffer = pSahara->getFrame();
      const Sahara::CommandExecuteResp* pCmdExecResp =
         Util::buffer_cast<const Sahara::CommandExecuteResp*>(pCmdExecRespBuffer->begin(), pCmdExecRespBuffer->size());
      TOOLS_ASSERT_OR_THROW(
         Sahara::SAHARA_COMMAND_EXECUTE_RESP == pCmdExecResp->m_header.m_command &&
            Sahara::CMD_GET_CMDID_LIST == pCmdExecResp->m_clientCommand &&
            TOOLS_SIZEOF(Sahara::CommandExecuteResp) == pCmdExecResp->m_header.m_length,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Command Exec Resp not received from Sahara protocol: " + pSahara->getDescription()
         )
      );


      bool bFound = false; // whether found cmd id 0x09 or not in list of cmd ids

      // Send cmd exec get data packet if length field > 0 in cmd exec resp
      // packet
      if(pCmdExecResp->m_responseLength > 0)
      {
         pSendBuffer = pSahara->createCommand<Sahara::CommandExecuteData>(Sahara::SAHARA_COMMAND_EXECUTE_DATA);
         Sahara::CommandExecuteData* pCmdExecuteData =
            Util::buffer_cast<Sahara::CommandExecuteData*>(pSendBuffer->begin(), pSendBuffer->size());
         pCmdExecuteData->m_clientCommand = Sahara::CMD_GET_CMDID_LIST;
         m_pConnection->sendSync(pSendBuffer);


         // get RAW data from target containing list of CMD Ids (Each CmdId is 4
         // bytes)
         Device::SharedByteBufferPtr pListOfCmdIds =
            Device::Buffer::createBuffer(static_cast<size_t>(pCmdExecResp->m_responseLength));

         pListOfCmdIds->resize(0);
         while(pListOfCmdIds->size() < pCmdExecResp->m_responseLength)
         {
            Device::SharedByteBufferPtr pPartial = pSahara->getFrame();
            pListOfCmdIds->append(pPartial->begin(), pPartial->size());
         }


         // Check if the pCmdExecRespBuffer has cmd id = 0x09 among list of cmd
         // IDs it contains
         Device::SharedByteBuffer::Iterator it;
         for(it = pListOfCmdIds->begin(); it < pListOfCmdIds->end(); it += sizeof(uint32_t))
         {
            const uint32_t* pCmdId = Util::buffer_cast<const uint32_t*>(it, sizeof(uint32_t));
            if(Sahara::CMD_GET_TRAINING_DATA == *pCmdId)
            {
               bFound = true;
               break;
            }
         }

         TOOLS_ASSERT_OR_THROW(
            bFound,
            Device::Exception(
               Device::Exception::DEVICE_RESPONSE_ERROR,
               "Command Id 0x09 not present in list of CMDIds returned by device Sahara protocol, hence DDR data cannot be downloaded \
            from target: " +
                  pSahara->getDescription()
            )
         );
      }


      if(bFound)
      {
         // Send cmd exec with cmd id 0x09
         pSendBuffer = pSahara->createCommand<Sahara::CommandExecute>(Sahara::SAHARA_COMMAND_EXECUTE);
         Sahara::CommandExecute* pCmdExecute9 =
            Util::buffer_cast<Sahara::CommandExecute*>(pSendBuffer->begin(), pSendBuffer->size());
         pCmdExecute9->m_clientCommand = Sahara::CMD_GET_TRAINING_DATA;
         m_pConnection->sendSync(pSendBuffer);


         // Get cmd exec resp packet
         Device::SharedByteBufferPtr pCmdExecRespBuffer9 = pSahara->getFrame();
         const Sahara::CommandExecuteResp* pCmdExecResp9 = Util::buffer_cast<
            const Sahara::CommandExecuteResp*>(pCmdExecRespBuffer9->begin(), pCmdExecRespBuffer9->size());
         TOOLS_ASSERT_OR_THROW(
            Sahara::SAHARA_COMMAND_EXECUTE_RESP == pCmdExecResp9->m_header.m_command &&
               Sahara::CMD_GET_TRAINING_DATA == pCmdExecResp9->m_clientCommand &&
               TOOLS_SIZEOF(Sahara::CommandExecuteResp) == pCmdExecResp9->m_header.m_length,
            Device::Exception(
               Device::Exception::DEVICE_INVALID_PACKET,
               "Command Exec Resp not received from Sahara "
               "protocol: " +
                  pSahara->getDescription()
            )
         );


         // Send cmd exec get data packet if length field > 0 in previous cmd
         // exec resp packet
         if(pCmdExecResp9->m_responseLength > 0)
         {
            pSendBuffer = pSahara->createCommand<Sahara::CommandExecuteData>(Sahara::SAHARA_COMMAND_EXECUTE_DATA);
            Sahara::CommandExecuteData* pCmdExecuteData9 =
               Util::buffer_cast<Sahara::CommandExecuteData*>(pSendBuffer->begin(), pSendBuffer->size());
            pCmdExecuteData9->m_clientCommand = Sahara::CMD_GET_TRAINING_DATA;
            m_pConnection->sendSync(pSendBuffer);
         }


         // Get the DDR raw data
         Device::SharedByteBufferPtr pDdr =
            Device::Buffer::createBuffer(static_cast<size_t>(pCmdExecResp9->m_responseLength));
         TOOLS_ASSERT_OR_THROW(
            static_cast<uint64_t>(MAX_UINT32_FILE_SIZE) > pCmdExecResp9->m_responseLength,
            Device::Exception(
               Device::Exception::DEVICE_INVALID_PARAMETERS,
               "Invalid length for region table (" + std::to_string(pCmdExecResp9->m_responseLength) + ")" +
                  pSahara->getDescription()
            )
         );
         pDdr->resize(0);
         while(pDdr->size() < pCmdExecResp9->m_responseLength)
         {
            Device::SharedByteBufferPtr pPartial = pSahara->getFrame();
            pDdr->append(pPartial->begin(), pPartial->size());
         }

         Device::AccessibleFilePtr ddrFile = std::make_shared<Device::AccessibleFile>();

         ddrFile->open(savePath / DDR_STORE_DEFAULT_FILENAME, std::ios::out | std::ios::trunc | std::ios::binary);

         ddrFile->write(pDdr->begin(), pDdr->size());
         ddrFile->close();


         // Semd a cmd switch mode to image_tx_pending to continue rest of image
         // loading

         pSendBuffer = pSahara->createCommand<Sahara::CommandSwitchMode>(Sahara::SAHARA_COMMAND_SWITCH_MODE);
         Sahara::CommandSwitchMode* pCmdSwitchMode =
            Util::buffer_cast<Sahara::CommandSwitchMode*>(pSendBuffer->begin(), pSendBuffer->size());
         pCmdSwitchMode->m_mode = Sahara::MODE_IMAGE_TX_PENDING;
         m_pConnection->sendSync(pSendBuffer);

         // target should send hello next with image_tx_pending to continue with
         // rest of image loading
      }
   }
   catch(...)
   {
      // If there was a failure, try to reset
      // if (bReset)
      {
         TOOLS_IGNORE_EXCEPTIONS(reset());
      }

      throw;
   }
}


} // namespace Function
