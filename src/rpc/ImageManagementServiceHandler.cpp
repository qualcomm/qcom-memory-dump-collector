// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "rpc/ImageManagementServiceHandler.h"

#include "communication/CommonIO.h"
#include "device/Buffer.h"
#include "device/Impl.h"
#include "function/ImageTransfer.h"
#include "function/MemoryDumpCollector.h"
#include "protocol/Fwd.h"
#include "protocol/Sahara.h"
#include "report/Thread.h"
#include "tracker/FunctionTracker.h"
#include "util/Event.h"
#include "util/MemoryHelper.h"
#include "util/StringHelper.h"
#include "util/ThisThread.h"

#include <chrono>
DEFINE_ENUM_PARAMETER_LOGGING(DeviceImageMode);

// To throw Exception since TOOLS_CATCH can not throw
#define IMGMGMT_CATCH(action)                                                                                          \
   catch(...)                                                                                                          \
   {                                                                                                                   \
      TOOLS_IGNORE_EXCEPTIONS(action);                                                                                 \
      throw;                                                                                                           \
   }

namespace Rpc {

static constexpr auto CONNECTION_WORKER_PROTOCOL_ENUMERATION_WAIT_INTERVAL = std::chrono::seconds(4);

static constexpr auto CONNECTION_WORKER_READY_WAIT_PERIOD =
   std::chrono::seconds(1) + CONNECTION_WORKER_PROTOCOL_ENUMERATION_WAIT_INTERVAL;

static constexpr auto CONNECTION_WORKER_PROTOCOL_WAIT_INTERVAL = std::chrono::seconds(1);


// ----------------------------------------------------------------------------
// ImageManagementConnectionWorker
//
/// Maintains Diag and QMI connections during re-enumeration
// ----------------------------------------------------------------------------
class ImageManagementConnectionWorker : public Util::StdThreadWrapper::Work
{
   TOOLS_FORBID_COPY(ImageManagementConnectionWorker);

public:
   ImageManagementConnectionWorker(const std::shared_ptr<ImageManagementServiceHandler>& pParent)
   : Util::StdThreadWrapper::Work(
        QC::getClientName() + " Image Management Service Connection Thread",
        Util::StdThreadWrapper::HealthMonitorPriority::Low
     )
   , m_pParent(pParent)
   , m_protocolAddedEvent(true)
   {
   }

   virtual ~ImageManagementConnectionWorker()
   {
   }

   virtual void onRun()
   {
      try
      {
         while(!isStopSignaled())
         {
            FLOG_INFO(
               "ImageManagementConnectionWorker scan - mode: " +
               std::string(std::to_string(m_pParent->m_deviceImageMode))
            );


            Device::Impl::ProtocolList protocols = m_pParent->m_pDevice->getProtocolList();
            Device::Impl::ProtocolList::const_iterator it = protocols.begin();
            Device::Impl::ProtocolList::const_iterator end = protocols.end();
            m_pParent->m_bSaharaProtocolAdded = false;
            for(; !isStopSignaled() && it != end; ++it)
            {
               switch(Device::Manager::getInstance()->getProtocolType((*it)->getHandle()))
               {
                  case Device::ProtocolType::PROT_SAHARA: {
                     m_pParent->m_pSaharaProtocol =
                        ((*it)->getOverrideProtocol()).dynamicCast<Device::Protocol::Sahara>();
                     m_pParent->m_bSaharaProtocolAdded = true;
                     Device::Protocol::Sahara::Mode mode = m_pParent->m_pSaharaProtocol->getMode();
                     if(Device::Protocol::Sahara::Mode::MODE_MEMORY_DEBUG == mode)
                     {
                        m_pParent->m_deviceImageMode = QC::DeviceImageMode::DEVICE_IMAGE_MODE_SAHARA_CRASH;
                     }
                     else if(Device::Protocol::Sahara::Mode::MODE_EFS_SYNC == mode)
                     {
                        m_pParent->m_deviceImageMode = QC::DeviceImageMode::DEVICE_IMAGE_MODE_SAHARA_EFS_SYNC;
                     }
                     else if(Device::Protocol::Sahara::Mode::MODE_IMAGE_TX_PENDING == mode ||
                             Device::Protocol::Sahara::Mode::MODE_IMAGE_TX_COMPLETE == mode)
                     {
                        m_pParent->m_deviceImageMode = QC::DeviceImageMode::DEVICE_IMAGE_MODE_SAHARA_DOWNLOAD;
                     }
                     else
                     {
                        m_pParent->m_deviceImageMode = QC::DeviceImageMode::DEVICE_IMAGE_MODE_NONE;
                     }

                     FLOG_INFO(
                        "ImageManagementConnectionWorker - sahara "
                        "available in mode: " +
                        std::string(std::to_string(m_pParent->m_deviceImageMode))
                     );

                     m_pParent->m_saharaReadyEvent.signal();
                     break;
                  }
               }
            }
            m_pParent->m_connectionWorkerEvent.signal();

            FLOG_INFO(
               "ImageManagementConnectionWorker wait for protocol - mode: " +
               std::string(std::to_string(m_pParent->m_deviceImageMode))
            );

            while(!isStopSignaled())
            {
               if(Util::ThisThread::waitForEvent(&m_protocolAddedEvent, CONNECTION_WORKER_PROTOCOL_WAIT_INTERVAL))
               {
                  m_pParent->m_connectionWorkerEvent.reset(); // reset the event and indicate a detection
                                                              // ongoing
                  // add a wait time here and expect add the protocol to be
                  // enumerated
                  Util::ThisThread::sleep(CONNECTION_WORKER_PROTOCOL_ENUMERATION_WAIT_INTERVAL);
                  m_protocolAddedEvent.reset();
                  break;
               }
            }
         }
      }
      TOOLS_CATCH(e, APP_REPORT_EXCEPTION(e));
   }

private:
   friend class ImageManagementServiceHandler;

   Util::CheckedPointer<ImageManagementServiceHandler> m_pParent;
   Util::Event m_protocolAddedEvent; ///< Event from device manager indicating
                                     ///< the arrival of a protocol
};

// ----------------------------------------------------------------------------
// ImageManagementServiceHandler
//
// ----------------------------------------------------------------------------
ImageManagementServiceHandler::ImageManagementServiceHandler(std::string serviceName)
: Rpc::ServiceHandlerBase(serviceName)
, m_pSaharaProtocol()
, m_pSaharaConnection()
, m_connectionWorkerEvent(true, false)
, m_saharaReadyEvent(true)
, m_deviceReconnectedEvent()
, m_deviceImageMode(QC::DeviceImageMode::DEVICE_IMAGE_MODE_NONE)
, m_bSaharaProtocolAdded(false)
, m_serialNumber(0xFFFFFFFFU)
, m_msmHwId(0xFFFFFFFFU)
, m_mutex()
{
   m_pDeviceManagerHandler = Rpc::DeviceManagerHandler::getInstance();
   m_pPublisher = m_pDeviceManagerHandler->getPublisher();
}

// ----------------------------------------------------------------------------
// ~ImageManagementServiceHandler
//
// ----------------------------------------------------------------------------
ImageManagementServiceHandler::~ImageManagementServiceHandler()
{
   TOOLS_IGNORE_EXCEPTIONS(doDestroy());
}

// ----------------------------------------------------------------------------
// isServiceLocked
//
// ----------------------------------------------------------------------------
bool ImageManagementServiceHandler::isServiceLocked()
{
   // LOG_TRACE(
   //     m_pLogger,
   //     "Checking if ImageManagementService is locked by any clients."
   //);
   //
   // QC::ServiceLockInfo lockInfo;
   // std::vector<QC::ServiceLockInfo> activeLocks;

   // lockInfo.__set_serviceName(QC::g_ImageManagementService_constants.IMAGE_MANAGEMENT_SERVICE_NAME);
   // lockInfo.__set_deviceHandle(m_pDevice->getHandle());
   // m_pDeviceManagerHandler->getServiceManager()->getServiceLockInfo(activeLocks,
   // lockInfo);

   // if (activeLocks.size() > 0)
   //{
   //     LOG_INFO(
   //         m_pLogger,
   //         "Active service lock count=" + std::to_string(activeLocks.size())
   //     );
   // }
   // return (0 < activeLocks.size());
   return false;
}

// ----------------------------------------------------------------------------
// initializeService
//
/// Opens the connection to the device.
// ----------------------------------------------------------------------------
QC::ErrorCode::type ImageManagementServiceHandler::initializeService(Device::ImplPtr pDevice)
{
   m_pDevice = pDevice;
   return initializeService();
}

QC::ErrorCode::type ImageManagementServiceHandler::initializeService()
{
   DEVICE_RPC_TRY_UNINITIALIZED(TOOLS_VOID)
   {
      setInitialized();

      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      m_pImageManagementConnectionWorker = std::make_shared<ImageManagementConnectionWorker>(shared_from_this());
      m_pImageManagementConnectionWorkerThread =
         std::make_shared<Util::StdThreadWrapper>(m_pImageManagementConnectionWorker);
      m_pImageManagementConnectionWorkerThread->start();
      Device::Manager::getInstance()
         ->subscribeForAsyncEvents(this, &ImageManagementServiceHandler::onProtocolAddedChange);
      Device::Manager::getInstance()
         ->subscribeForAsyncEvents(this, &ImageManagementServiceHandler::onProtocolRemovedChange);
   }
   DEVICE_RPC_CATCH;

   return static_cast<QC::ErrorCode::type>(__functionError);
}

// ----------------------------------------------------------------------------
// destroyService
//
/// Closes this service and shuts it down on the RPC server
// ----------------------------------------------------------------------------
QC::ErrorCode::type ImageManagementServiceHandler::destroyService()
{
   DEVICE_RPC_TRY_UNINITIALIZED(TOOLS_VOID)
   {
      doDestroy();
   }
   DEVICE_RPC_CATCH;
   return static_cast<QC::ErrorCode::type>(__functionError);
}

// ----------------------------------------------------------------------------
// getDeviceImageMode
//
/// Returns current image management scheme
// ----------------------------------------------------------------------------
QC::DeviceImageMode::type ImageManagementServiceHandler::getDeviceImageMode()
{
   DEVICE_RPC_TRY(TOOLS_VOID)
   {
   }
   DEVICE_RPC_CATCH;
   return m_deviceImageMode;
}

// ----------------------------------------------------------------------------
// collectMemoryDump
//
/// Attempts to retrieve a memory dump from device
// ----------------------------------------------------------------------------
QC::ErrorCode::type ImageManagementServiceHandler::collectMemoryDump(const std::string& pathName)
{
   DEVICE_RPC_TRY(PARAMETER(std::string(pathName)))
   {
      if(isServiceLocked())
      {
         return QC::ErrorCode::DEVICE_SERVICE_LOCKED;
      }
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      Util::ThisThread::waitForEvent(&m_connectionWorkerEvent, CONNECTION_WORKER_READY_WAIT_PERIOD);
      TOOLS_ASSERT_OR_THROW(
         QC::DeviceImageMode::DEVICE_IMAGE_MODE_SAHARA_CRASH == m_deviceImageMode,
         Device::Exception(Device::Exception::DEVICE_PROTOCOL_INVALID, "Memory dump mode not available")
      );

      doSaharaConnect();

      std::filesystem::path originalPath = std::filesystem::path(pathName);
      std::filesystem::path outputPath = originalPath;
      TOOLS_IGNORE_EXCEPTIONS(Util::createPath(outputPath.parent_path()););
      if(!std::filesystem::exists(outputPath))
      {
         outputPath = Util::createTempFileName(Device::Manager::getInstance()->getTempDirectory());
         Util::createPath(outputPath.parent_path());
      }
      Function::MemoryDumpCollectorPtr pMemoryDump = std::make_shared<Function::MemoryDumpCollector>(m_pSaharaConnection
      );

      TOOLS_ASSUMING(pMemoryDump->subscribe(this, &ImageManagementServiceHandler::onMemoryDumpCollectorEvent));
      try
      {
         pMemoryDump->collectMemoryDump(outputPath);

         if(outputPath != originalPath)
         {
            Device::Manager::getInstance()->saveFile(outputPath, originalPath);
         }
      }
      IMGMGMT_CATCH(pMemoryDump->unsubscribe(this, &ImageManagementServiceHandler::onMemoryDumpCollectorEvent););
      pMemoryDump->unsubscribe(this, &ImageManagementServiceHandler::onMemoryDumpCollectorEvent);
   }
   DEVICE_RPC_CATCH;
   return static_cast<QC::ErrorCode::type>(__functionError);
}

// ----------------------------------------------------------------------------
// collectMemoryDumpForSectionNames
//
/// Attempts to retrieve a memory dump from device for certain sections
// ----------------------------------------------------------------------------
QC::ErrorCode::type ImageManagementServiceHandler::collectMemoryDumpWithOptions(const QC::MemoryDumpOptions& options)
{
   DEVICE_RPC_TRY(TOOLS_VOID)
   {
      if(isServiceLocked())
      {
         return QC::ErrorCode::DEVICE_SERVICE_LOCKED;
      }
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      Util::ThisThread::waitForEvent(&m_connectionWorkerEvent, CONNECTION_WORKER_READY_WAIT_PERIOD);
      TOOLS_ASSERT_OR_THROW(
         QC::DeviceImageMode::DEVICE_IMAGE_MODE_SAHARA_CRASH == m_deviceImageMode,
         Device::Exception(Device::Exception::DEVICE_PROTOCOL_INVALID, "Memory dump mode not available")
      );
      TOOLS_ASSERT_OR_THROW(
         options.__isset.pathName && !options.pathName.empty(),
         Device::Exception(Device::Exception::DEVICE_INVALID_PARAMETERS, "Pathname not available")
      );
      TOOLS_ASSERT_OR_THROW(
         !options.__isset.sectionNameList || !options.sectionNameList.empty(),
         Device::Exception(Device::Exception::DEVICE_INVALID_PARAMETERS, "SectionNameList not available")
      );

      doSaharaConnect();

      std::filesystem::path originalPath = std::filesystem::path(options.pathName);
      std::filesystem::path outputPath = originalPath;
      TOOLS_IGNORE_EXCEPTIONS(Util::createPath(outputPath.parent_path()););
      if(!std::filesystem::exists(outputPath))
      {
         outputPath = Util::createTempFileName(Device::Manager::getInstance()->getTempDirectory());
         Util::createPath(outputPath.parent_path());
      }
      Function::MemoryDumpCollectorPtr pMemoryDump = std::make_shared<Function::MemoryDumpCollector>(m_pSaharaConnection
      );

      TOOLS_ASSUMING(pMemoryDump->subscribe(this, &ImageManagementServiceHandler::onMemoryDumpCollectorEvent));
      try
      {
         std::vector<std::string> sectionNameList;
         if(options.__isset.sectionNameList)
         {
            std::vector<std::string>::const_iterator it = options.sectionNameList.begin();
            std::vector<std::string>::const_iterator end = options.sectionNameList.end();
            for(; it != end; ++it)
            {
               std::string sectionName(*it);
               Util::toUpper(sectionName);
               sectionNameList.push_back(sectionName);
            }
         }
         pMemoryDump->collectMemoryDump(
            outputPath,
            sectionNameList,
            (options.__isset.storageOptions && options.storageOptions.__isset.collatedType)
               ? Function::MemoryDumpCollector::StorageCollatedType(options.storageOptions.collatedType)
               : Function::MemoryDumpCollector::STORAGE_COLLATED_NONE
         );

         if(outputPath != originalPath)
         {
            Device::Manager::getInstance()->saveFile(outputPath, originalPath);
         }
      }
      IMGMGMT_CATCH(pMemoryDump->unsubscribe(this, &ImageManagementServiceHandler::onMemoryDumpCollectorEvent););
      pMemoryDump->unsubscribe(this, &ImageManagementServiceHandler::onMemoryDumpCollectorEvent);
   }
   DEVICE_RPC_CATCH;
   return static_cast<QC::ErrorCode::type>(__functionError);
}

// ----------------------------------------------------------------------------
// resetDevice
//
/// Resets device and wait until device is re-enumerated or timeout
// ----------------------------------------------------------------------------
QC::ErrorCode::type ImageManagementServiceHandler::resetDevice(const int32_t timeout, bool skipSahara)
{
   DEVICE_RPC_TRY(PARAMETER(timeout))
   {
      if(isServiceLocked())
      {
         return QC::ErrorCode::DEVICE_SERVICE_LOCKED;
      }
      {
         std::lock_guard<std::recursive_mutex> lock(m_mutex);
         Util::ThisThread::waitForEvent(&m_connectionWorkerEvent, CONNECTION_WORKER_READY_WAIT_PERIOD);
         Device::Manager::getInstance()
            ->subscribeForAsyncEvents(this, &ImageManagementServiceHandler::onDeviceConnected);

         if(!skipSahara)
         {
            doSaharaConnect();
            // Sahara only reset under DEVICE_IMAGE_MODE_SAHARA_CRASH mode
            TOOLS_ASSERT_OR_THROW(
               (m_deviceImageMode == QC::DeviceImageMode::DEVICE_IMAGE_MODE_SAHARA_CRASH),
               Device::Exception(
                  Device::Exception::DEVICE_PROTOCOL_INVALID,
                  "Unsupported device image mode : " + std::to_string(m_deviceImageMode)
               )
            );
            // Send Sahara reset command directly
            Device::Protocol::SaharaPtr pSahara =
               (m_pSaharaConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();
            Device::SharedByteBufferPtr pResetBuffer =
               pSahara->createCommand<Device::Protocol::Sahara::Reset>(Device::Protocol::Sahara::SAHARA_RESET);
            m_pSaharaConnection->sendSync(pResetBuffer);
            Util::ThisThread::sleep(std::chrono::seconds(1));
         }
         TOOLS_IGNORE_EXCEPTIONS(doSaharaCleanup());
         m_saharaReadyEvent.reset();
         m_pImageManagementConnectionWorker->m_protocolAddedEvent.signal();
      }

      if(timeout > 0)
      {
         Util::ThisThread::
            waitForEvent(&m_deviceReconnectedEvent, std::chrono::milliseconds(static_cast<uint32_t>(timeout)));
      }
      // Device sonmetimes may not re-enumerate after reset sequence, spoof a
      // protocol added signal to trigger sahara mode scan
      m_pImageManagementConnectionWorker->m_protocolAddedEvent.signal();
   }
   DEVICE_RPC_CATCH;
   TOOLS_IGNORE_EXCEPTIONS(Device::Manager::getInstance()
                              ->unsubscribeAsyncEvents(this, &ImageManagementServiceHandler::onDeviceConnected));
   return static_cast<QC::ErrorCode::type>(__functionError);
}

// ----------------------------------------------------------------------------
// onProtocolAddedChange
//
/// Notify ImageManagementConnectionWorker that a protocol is added
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::onProtocolAddedChange(Device::ProtocolAddedEvent* pEvent)
{
   FLOG_INFO("onProtocolAddedChange handle: " + std::string(std::to_string(pEvent->getProtocol()->getHandle())));

   // Don't care the prtotcol type, any interface change, will request a
   // re-enumerate of protocol
   if(pEvent->getDevice() == m_pDevice)
   {
      m_pImageManagementConnectionWorker->m_protocolAddedEvent.signal();
   }
}

// ----------------------------------------------------------------------------
// onProtocolRemovedChange
//
/// Notify ImageManagementConnectionWorker that a protocol is removed
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::onProtocolRemovedChange(Device::ProtocolRemovedEvent* pEvent)
{
   if(pEvent->getDevice() == m_pDevice)
   {
      // On removal also re-enumerate
      if((pEvent->getProtocol()->getOverrideProtocol()).dynamicCast<Device::Protocol::Sahara>() != nullptr)
      {
         TOOLS_IGNORE_EXCEPTIONS(doSaharaCleanup());
         m_saharaReadyEvent.reset();
      }
      m_pImageManagementConnectionWorker->m_protocolAddedEvent.signal();
      m_serialNumber = 0xFFFFFFFFU;
      m_msmHwId = 0xFFFFFFFFU;
   }
}

// ----------------------------------------------------------------------------
// onMemoryDumpCollectorEvent
//
/// Pushes the event to the RPC client
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::onMemoryDumpCollectorEvent(Function::MemoryDumpCollectorEvent* pEvent)
{
   sendEvent(pEvent->getEventId(), pEvent->getDescription());

   // send event to all QUTS clients
   Device::Manager::getInstance()->sendImageManagementServiceEvent(
      QC::getName(),
      m_pDevice->getHandle(),
      m_pSaharaConnection->getProtocol()->getHandle(),
      pEvent->getEventId(),
      pEvent->getDescription()
   );
}

// ----------------------------------------------------------------------------
// onImageTransferEvent
//
/// Pushes the event to the RPC client
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::onImageTransferEvent(Function::ImageTransferEvent* pEvent)
{
   sendEvent(pEvent->getEventId(), pEvent->getDescription());
   if(m_pDevice == nullptr || m_pSaharaConnection == nullptr)
   {
      return;
   }
   // send event to all clients
   Device::Manager::getInstance()->sendImageManagementServiceEvent(
      QC::getName(),
      m_pDevice->getHandle(),
      m_pSaharaConnection->getProtocol()->getHandle(),
      pEvent->getEventId(),
      pEvent->getDescription()
   );
}

// ----------------------------------------------------------------------------
// onDeviceConnected
//
/// Calls the device connect signal to the UTS client
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::onDeviceConnected(Device::DeviceConnectEvent* pEvent)
{
   if(pEvent->getDevice() == m_pDevice)
   {
      m_deviceReconnectedEvent.signal();
   }
}

// ----------------------------------------------------------------------------
// doSaharaConnect
//
/// Creates sahara connections
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::doSaharaConnect()
{
   if(m_pSaharaConnection != nullptr)
   {
      m_pDeviceManagerHandler->closeConnection(m_pSaharaConnection);
      m_pSaharaConnection = NULL;
   }

   Util::ThisThread::sleep(std::chrono::seconds(1));
   TOOLS_ASSERT_OR_THROW(
      m_pSaharaProtocol != nullptr,
      Device::Exception(Device::Exception::DEVICE_PROTOCOL_INVALID, "Sahara protocol not available")
   );
   m_pSaharaConnection = m_pDeviceManagerHandler->createConnection(
      m_pSaharaProtocol,
      Device::Protocol::Base::Access::READ_WRITE,
      Device::Protocol::Base::Share::NONE
   );
   TOOLS_ASSERT_OR_THROW(
      m_pSaharaConnection != nullptr,
      Device::Exception(Device::Exception::DEVICE_PROTOCOL_INVALID, "Sahara connection not available")
   );

   m_pSaharaConnection->connect();
}

// ----------------------------------------------------------------------------
// doSaharaCleanup
//
/// Clean up Sahara
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::doSaharaCleanup()
{
   std::lock_guard<std::recursive_mutex> lock(m_mutex);

   FLOG_INFO(QC::getName() + ": Cleanup Sahara");
   if(m_pSaharaConnection != nullptr)
   {
      TOOLS_IGNORE_EXCEPTIONS(m_pDeviceManagerHandler->closeConnection(m_pSaharaConnection));
      m_pSaharaConnection = NULL;
   }
   m_pSaharaProtocol = NULL;

   m_deviceImageMode = QC::DeviceImageMode::DEVICE_IMAGE_MODE_NONE;
   FLOG_INFO(QC::getName() + ": Cleanup Sahara Done");
}

// ----------------------------------------------------------------------------
// doDestroy
//
/// Cleans up subscriptions and threads
// ----------------------------------------------------------------------------
void ImageManagementServiceHandler::doDestroy()
{
   TOOLS_IGNORE_EXCEPTIONS(Device::Manager::getInstance()
                              ->unsubscribeAsyncEvents(this, &ImageManagementServiceHandler::onProtocolAddedChange));
   TOOLS_IGNORE_EXCEPTIONS(Device::Manager::getInstance()
                              ->unsubscribeAsyncEvents(this, &ImageManagementServiceHandler::onProtocolRemovedChange));

   std::lock_guard<std::recursive_mutex> lock(m_mutex);

   if(m_pImageManagementConnectionWorkerThread != nullptr)
   {
      m_pImageManagementConnectionWorkerThread->stop();
      m_pImageManagementConnectionWorker->m_protocolAddedEvent.signal();
      m_pImageManagementConnectionWorkerThread->waitForStop();
      m_pImageManagementConnectionWorkerThread = NULL;
      m_pImageManagementConnectionWorker = NULL;
   }

   TOOLS_IGNORE_EXCEPTIONS(doSaharaCleanup());

   m_bInitialized = false;
}

} // namespace Rpc
