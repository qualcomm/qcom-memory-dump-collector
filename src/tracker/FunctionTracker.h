// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "Definitions.h"
#include "device/Exception.h"
#include "device/Fwd.h"
#include "device/Logger.h"
#include "Version.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <set>

namespace QC {
} // namespace QC

template <typename _T>
inline std::string printParameter(const _T& parameter)
{
   std::ostringstream oss;
   oss << parameter;
   return oss.str();
}

template <>
inline std::string printParameter(const std::map<int32_t, std::string>& imageList)
{
   std::ostringstream ss;
   ss << "{";

   std::map<int32_t, std::string>::const_iterator it = imageList.begin();
   std::map<int32_t, std::string>::const_iterator end = imageList.end();

   for(; end != it; ++it)
   {
      ss << "\n Image id " + toString(it->first) + " - ";
      ss << "path " + it->second;
   }

   ss << "\n}";
   return ss.str();
}

template <>
inline std::string printParameter(const std::string& parameter)
{
   return "\"" + parameter + "\"";
}

template <>
inline std::string printParameter(const int32_t& parameter)
{
   return printParameter<int64_t>(parameter);
}

template <typename _T>
void logParameter(std::string& output, const std::string& parameterName, const _T& parameterValue)
{
   std::string::size_type open = parameterName.find('(');
   std::string::size_type close;
   if(std::string::npos != open && std::string::npos != (close = parameterName.find(')', open)))
   {
      output += parameterName.substr(open + 1, (close - open) - 1);
   }
   else
   {
      output += parameterName;
   }
   output += "=" + printParameter(parameterValue) + ", ";
}
#define DEFINE_ENUM_PARAMETER_LOGGING(EnumType)                                                                        \
   namespace QC {                                                                                                      \
   extern const std::map<int32_t, const char*> _##EnumType##_VALUES_TO_NAMES;                                          \
   }


#define PARAMETER(parameter) logParameter(__parameterList, #parameter, parameter);

namespace QC {

// ----------------------------------------------------------------------------
// FunctionTracker
//
/// Tracks when the function is entered and exitted
// ----------------------------------------------------------------------------
class FunctionTracker
{
   TOOLS_FORBID_COPY(FunctionTracker);

public:
   FunctionTracker(
      const std::string& clientName,
      int32_t port,
      const std::string& serviceName,
      const std::string& function,
      const std::string& parameterList,
      std::string& lastError
   );
   virtual ~FunctionTracker();

private:
   std::string m_clientName;                          ///< RPC Client name
   std::string m_port;                                ///< RPC port for the client
   std::string m_serviceName;                         ///< Service of the function being tracked
   std::string m_functionName;                        ///< Name of the function being tracked
   std::string& m_lastError;                          ///< Error tracked by the class calling this
   std::chrono::steady_clock::time_point m_startTime; ///< Record start time
};

// ----------------------------------------------------------------------------
// getName
//
/// @returns The name of the service
// ----------------------------------------------------------------------------
inline std::string getName()
{
   return "QFS";
}


// ----------------------------------------------------------------------------
// getClientName
//
/// @returns The name of the service
// ----------------------------------------------------------------------------
inline std::string getClientName()
{
   return "QFSClient";
}


// ----------------------------------------------------------------------------
// getClientPort
//
/// @returns The name of the service
// ----------------------------------------------------------------------------
inline int32_t getClientPort()
{
   return 1001;
}

// ----------------------------------------------------------------------------
// getAllUsersDirectory
//
/// @returns The location where temporary files should go
// ----------------------------------------------------------------------------
inline std::filesystem::path getAllUsersDirectory()
{
   std::string appName = getName();
#ifdef TOOLS_TARGET_WINDOWS
   std::filesystem::path path(std::string("C:\\ProgramData\\" + appName + "\\QMDC").c_str());
#endif
#ifdef TOOLS_TARGET_LINUX
   std::filesystem::path path(std::string("/var/tmp/" + appName + "/QMDC").c_str());
#endif
   return path;
}

// ----------------------------------------------------------------------------
// getTempDirectory
//
/// @returns The location where temporary files should go
// ----------------------------------------------------------------------------
inline std::filesystem::path getTempDirectory()
{
   return getAllUsersDirectory() / "Temp";
}

// ----------------------------------------------------------------------------
// getDLLVersion
//
/// Gets the DLL Version
// ----------------------------------------------------------------------------
inline std::string getDLLVersion()
{
   return std::string(VERSION_STRING);
}

// ----------------------------------------------------------------------------
// getAppMajorVersion
//
/// @returns The major version number of the application
// ----------------------------------------------------------------------------
inline uint16_t getAppMajorVersion()
{
   return MAJOR_VERSION;
}

// ----------------------------------------------------------------------------
// getAppMinorVersion
//
/// @returns The minor verison number of the application
// ----------------------------------------------------------------------------
inline uint16_t getAppMinorVersion()
{
   return MINOR_VERSION;
}

// ----------------------------------------------------------------------------
// getAppBuildId
//
/// @returns The build specific number for the application
// ----------------------------------------------------------------------------
inline std::string getAppBuildId()
{
   std::ostringstream ss;
   ss << "patch " << std::setw(2) << std::setfill('0') << PATCH_VERSION;
   return ss.str();
}

#define DEVICE_RPC_TRY_UNINITIALIZED(PARAMETER_LIST)                                                                   \
   std::string __parameterList;                                                                                        \
   PARAMETER_LIST                                                                                                      \
   Device::Exception::ErrorCode __functionError = Device::Exception::DEVICE_NO_ERROR;                                  \
   std::string __functionErrorString;                                                                                  \
   QC::FunctionTracker TOOLS_ANONYMOUS_IDENTIFIER(RPC)(                                                                \
      QC::getClientName(),                                                                                             \
      QC::getClientPort(),                                                                                             \
      QC::getName(),                                                                                                   \
      TOOLS_FUNCTION_NAME,                                                                                             \
      __parameterList,                                                                                                 \
      __functionErrorString                                                                                            \
   );                                                                                                                  \
   try                                                                                                                 \
   {
#define DEVICE_RPC_TRY(PARAMETER_LIST)                                                                                 \
   DEVICE_RPC_TRY_UNINITIALIZED(PARAMETER_LIST TOOLS_VOID)                                                             \
   TOOLS_ASSERT_OR_THROW(                                                                                              \
      isInitialized(),                                                                                                 \
      Device::Exception(                                                                                               \
         Device::Exception::DEVICE_SERVICE_NOT_INITIALIZED,                                                            \
         QC::getName() + " was used without being initialized.  Call "                                                 \
                         "initializeService()"                                                                         \
      )                                                                                                                \
   );


#define DEVICE_RPC_CATCH                                                                                               \
   __functionError = Device::Exception::DEVICE_NO_ERROR;                                                               \
   __functionErrorString.clear();                                                                                      \
   }                                                                                                                   \
   catch(const Device::Exception& e)                                                                                   \
   {                                                                                                                   \
      try                                                                                                              \
      {                                                                                                                \
         __functionError = e.getErrorCode();                                                                           \
         __functionErrorString = e.what();                                                                             \
         if(NULL != m_pPublisher)                                                                                      \
         {                                                                                                             \
            m_pPublisher->send(Util::Message::Level::EXCEPTION, QC::getName(), "Exception", e.what());                 \
         }                                                                                                             \
      }                                                                                                                \
      catch(...)                                                                                                       \
      {                                                                                                                \
         TOOLS_TRACE("Unhandled exception in RPC layer");                                                              \
      }                                                                                                                \
   }                                                                                                                   \
   TOOLS_CATCH(                                                                                                        \
      e, __functionError = Device::Exception::DEVICE_UNKNOWN_ERROR; __functionErrorString = e.what();                  \
      if(NULL != m_pPublisher) {                                                                                       \
         m_pPublisher->send(Util::Message::Level::EXCEPTION, QC::getName(), "Exception", e.what());                    \
      }                                                                                                                \
   );                                                                                                                  \
   m_lastError = __functionError;                                                                                      \
   switch(__functionError)                                                                                             \
   {                                                                                                                   \
      case Device::Exception::DEVICE_NO_ERROR:                                                                         \
         break;                                                                                                        \
      case Device::Exception::DEVICE_PERMISSIONS_ERROR:                                                                \
      case Device::Exception::DEVICE_CONNECTION_LOCKED:                                                                \
      case Device::Exception::DEVICE_DISCONNECTED:                                                                     \
      case Device::Exception::DEVICE_PROTOCOL_DISCONNECTED:                                                            \
      case Device::Exception::DEVICE_PROTOCOL_UNRESPONSIVE:                                                            \
      case Device::Exception::DEVICE_TX_CANCELLED:                                                                     \
      case Device::Exception::DEVICE_TIMEOUT:                                                                          \
      case Device::Exception::DEVICE_INVALID_PACKET:                                                                   \
      case Device::Exception::DEVICE_RESPONSE_ERROR:                                                                   \
      case Device::Exception::DEVICE_TCP_PORT_FAILURE:                                                                 \
      /*   break; */                                                                                                   \
      case Device::Exception::DEVICE_UNKNOWN_ERROR:                                                                    \
      case Device::Exception::DEVICE_INVALID_PARAMETERS:                                                               \
      case Device::Exception::DEVICE_INVALID_DEVICE_HANDLE:                                                            \
      case Device::Exception::DEVICE_INVALID_PROTOCOL_HANDLE:                                                          \
      case Device::Exception::DEVICE_INVALID_CONNECTION_HANDLE:                                                        \
      case Device::Exception::DEVICE_PROTOCOL_INVALID:                                                                 \
      case Device::Exception::DEVICE_INVALID_PROCESSOR:                                                                \
      case Device::Exception::DEVICE_INVALID_LOG_SESSION:                                                              \
      case Device::Exception::DEVICE_SERVICE_NOT_INITIALIZED:                                                          \
      case Device::Exception::DEVICE_SERVICE_ALREADY_INITIALIZED:                                                      \
      case Device::Exception::DEVICE_LICENSE_ERROR:                                                                    \
      case Device::Exception::DEVICE_OTP_PROGRAMED:                                                                    \
      case Device::Exception::DEVICE_DECRYPTION_FAILED:                                                                \
      case Device::Exception::DEVICE_SERVICE_LOCKED:                                                                   \
      default:                                                                                                         \
         Tool::ErrorException e(static_cast<int32_t>(__functionError), __functionErrorString.c_str());                 \
         throw e;                                                                                                      \
   }


} // namespace QC
