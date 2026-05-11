// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include <KL/kLogger.h>
#include "Globals.h"
#include "CrashCollection.h"
#include "rpc/ImageManagementServiceHandler.h"
#include "tracker/FunctionTracker.h"
#include <iostream>

typedef std::pair<uint64_t, std::shared_ptr<Rpc::ImageManagementServiceHandler>> GLOBAL_HANDLER;
typedef std::unordered_map<uint64_t, std::shared_ptr<Rpc::ImageManagementServiceHandler>> GLOBAL_HANDLER_MAP;
static GLOBAL_HANDLER_MAP g_handlers;

namespace QC {

   void PrintMessage(std::string message)
   {
#ifdef TOOLS_TARGET_WINDOWS
      std::cout << message << std::endl;
#elif defined TOOLS_TARGET_LINUX
#endif
   }

   CrashCollection::CrashCollection(DeviceInfo deviceInfo)
   {
      if (g_handlers.find(deviceInfo.deviceHandle) == g_handlers.end())
      {
         m_deviceHandle = deviceInfo.deviceHandle;
         std::string serviceName =
         "CrashCollection_" + std::to_string(m_deviceHandle);
         g_handlers.insert(
         GLOBAL_HANDLER(deviceInfo.deviceHandle, std::make_shared<Rpc::ImageManagementServiceHandler>(serviceName))
         );
      }
   }
   CrashCollection:: ~CrashCollection()
   {
      g_handlers.erase(m_deviceHandle);
   }

   /// <summary>
   /// Get the dll version as per the version file
   ///
   /// </summary>
   ///
   /// <returns>DLL version</returns>
   ///
   std::string CrashCollection::getDLLVersion()
   {return QC::getDLLVersion();
   }

   ErrorType CrashCollection::initializeService()
   {
      QC::ErrorType result;

      try {
         QFS_PRINT_FUNCTION
         result.errorCode =  
            Rpc::DeviceManagerHandler::getInstance()->attachService(m_deviceHandle, g_handlers[m_deviceHandle]);
      }
      LIB_CATCH
      return result;
   }
   ErrorType CrashCollection::destroyService()
   {
      QC::ErrorType result;
      try {
         QFS_CHECK_INITIALIZE
            result.errorCode = g_handlers[m_deviceHandle]->destroyService();
      }
      LIB_CATCH
      return result;
   }

   ErrorType CrashCollection::collectMemoryDumpWithOptions(const MemoryDumpOptions& options)
   {
      QC::ErrorType result;
      try {
         QFS_CHECK_INITIALIZE
         result.errorCode = g_handlers[m_deviceHandle]->collectMemoryDumpWithOptions(options);
      }
      LIB_CATCH
      return result;
   }

   ErrorType CrashCollection::resetDevice(const int timeout)
   {
      QC::ErrorType result;
      try {
         QFS_CHECK_INITIALIZE
         result.errorCode = g_handlers[m_deviceHandle]->resetDevice(timeout, false);
      }
      LIB_CATCH
      return result;
   }

} // namespace QC
