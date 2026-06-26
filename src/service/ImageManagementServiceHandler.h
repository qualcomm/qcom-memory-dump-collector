// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "device/Exception.h"
#include "device/Fwd.h"
#include "device/Manager.h"
#include "function/Fwd.h"
#include "ImageManagementDefinitions.h"
#include "service/DeviceManagerHandler.h"
#include "service/Service.h"
#include "util/Event.h"
#include "util/ThreadHelper.h"

#include <mutex>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4512)
#endif

namespace Rpc {


class ImageManagementConnectionWorker;
class StdThreadWrapper;

// ----------------------------------------------------------------------------
// SaharaServiceHandler
//
/// Implements the functions for the Sahara RPC server
// ----------------------------------------------------------------------------
class ImageManagementServiceHandler
: public Rpc::ServiceHandlerBase
, public std::enable_shared_from_this<ImageManagementServiceHandler>
{
   TOOLS_FORBID_COPY(ImageManagementServiceHandler);

public:
   ImageManagementServiceHandler(std::string serviceName);
   virtual ~ImageManagementServiceHandler();

   virtual QC::ErrorCode::type initializeService();
   virtual QC::ErrorCode::type initializeService(Device::ImplPtr pDevice);
   virtual QC::ErrorCode::type destroyService();

   bool isServiceLocked();
   QC::DeviceImageMode::type getDeviceImageMode();
   QC::ErrorCode::type collectMemoryDump(const std::string& pathName);
   QC::ErrorCode::type collectMemoryDumpWithOptions(const QC::MemoryDumpOptions& options);
   QC::ErrorCode::type resetDevice(const int32_t timeout, bool skipSahara);

private:
   friend class ImageManagementConnectionWorker;

   void onProtocolAddedChange(Device::ProtocolAddedEvent* pEvent);
   void onProtocolRemovedChange(Device::ProtocolRemovedEvent* pEvent);
   void onMemoryDumpCollectorEvent(Function::MemoryDumpCollectorEvent* pEvent);
   void onDeviceConnected(Device::DeviceConnectEvent* pEvent);

   void doSaharaConnect();
   void doSaharaCleanup();
   void doDestroy();

   std::shared_ptr<ImageManagementConnectionWorker> m_pImageManagementConnectionWorker;
   std::shared_ptr<Util::StdThreadWrapper> m_pImageManagementConnectionWorkerThread;
   Util::Event m_connectionWorkerEvent; ///< Event indicating connection worker is ready

   Device::Protocol::SaharaPtr m_pSaharaProtocol;
   volatile bool m_bSaharaProtocolAdded;
   Device::ConnectionPtr m_pSaharaConnection;


   Util::Event m_saharaReadyEvent;   ///< Event from device manager indicating
                                     ///< Sahara interface ready
   Util::Event m_deviceReconnectedEvent;

   volatile QC::DeviceImageMode::type m_deviceImageMode;

   std::recursive_mutex m_mutex;

   // Moving from serviceBase
   Device::ImplPtr m_pDevice;
   Util::CheckedPointer<DeviceManagerHandler> m_pDeviceManagerHandler;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace Rpc
