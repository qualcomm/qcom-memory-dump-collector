// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include <cstdint>
#include <map>

#pragma once
#include "ImageManagementDefinitions.h"
#include "Definitions.h"

namespace QC {
#ifdef TOOLS_TARGET_WINDOWS

#elif defined TOOLS_TARGET_LINUX

#endif
   class CrashCollection {
   public:
      CrashCollection(DeviceInfo deviceInfo);
      virtual ~CrashCollection();

      /// <summary>
      /// Get the dll version as per the version file
      ///
      /// </summary>
      ///
      /// <returns>DLL version</returns>
      ///
      static std::string getDLLVersion();

      ErrorType initializeService();
      ErrorType destroyService();
      ErrorType collectMemoryDumpWithOptions(const MemoryDumpOptions& options);
      ErrorType resetDevice(const int timeout);
      QC::DeviceImageMode::type getDeviceImageMode();
   private:
      int64_t m_deviceHandle;
   };
} // namespace QC
