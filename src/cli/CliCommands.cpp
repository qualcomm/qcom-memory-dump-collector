// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "CliCommands.h"

#include "CliHelp.h"
#include <KL/kLogger.h>
#include "Definitions.h"
#include "DeviceDiscovery.h"
#include "ExceptionHandler.h"
#include "FileSystem.h"
#include "ProgressWrapper.h"
#include "CrashCollection.h"
#include "Spinner.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace QC {
namespace CLI {

#define QC_CMDLINE_IGNORE_EXCEPTION(expr)                        \
    do {                                                         \
        try {                                                    \
            expr;                                                \
        } catch (const std::exception& e) {                      \
            std::ostringstream _qc_oss;                          \
            _qc_oss << #expr << " failed in " << __func__        \
                    << " (" << __FILE__ << ":" << __LINE__       \
                    << "): " << e.what();                        \
            CFLOG_ERROR(_qc_oss.str(), /*printMsgOnly=*/false);  \
        } catch (...) {                                          \
            std::ostringstream _qc_oss;                          \
            _qc_oss << #expr << " failed in " << __func__        \
                    << " (" << __FILE__ << ":" << __LINE__       \
                    << "): unknown exception";                   \
            CFLOG_ERROR(_qc_oss.str(), /*printMsgOnly=*/false); \
        } \
    } while(0);


#define QC_TRY_CLEANUP(crashCollectionPtr)                   \
    do {                                                      \
        QC_CMDLINE_IGNORE_EXCEPTION(                          \
            if (crashCollectionPtr) {                        \
                (crashCollectionPtr)->destroyService();      \
            }                                                 \
        );                                                    \
        QC_CMDLINE_IGNORE_EXCEPTION(                          \
            QC::DeviceDiscovery::stopMonitoring()             \
        );                                                    \
    } while (0);

std::string toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

int CliCommands::execute(const CliOptions& options) {
    try {
        switch (options.command) {
            case CliOptions::CommandType::HELP:
                // Check if help was requested
                CliHelp::showUsage();
                return 0;
            case CliOptions::CommandType::LIST_DEVICES:
                return listDevices();
            case CliOptions::CommandType::COLLECT_MEMORY_DUMP:
                return collectMemoryDump(options);
            
            case QC::CLI::CliOptions::CommandType::DISPLAY_VERSION:
            return displayVersion();
            
            case CliOptions::CommandType::NONE:
                QC_THROW_CMDLINE_ERROR(QC::Common::Exception::MISSING_REQUIRED_PARAMETER, "command", "No command specified. Use -h or --help for usage information.");
                
            default:
                QC_THROW_CMDLINE_ERROR(QC::Common::Exception::INVALID_PARAMETERS, "command", "Unknown command. Use -h or --help for usage information.");
        }
    }
    catch (const QC::Common::Exception& e) {
        // Print formatted message with suggestion if available
        std::ostringstream oss;
        oss << "Error: " << e.getFormattedMessage();

        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        return 1;
    }
    
    catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Error: " << e.what();

        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        return 1;
    }
}

std::string CliCommands::getProtocolName(QC::ProtocolType type)
{
   switch(type)
   {
      case QC::ProtocolType::PROT_SAHARA:
         return "SAHARA";
      default:
         return "UNKNOWN";
   }
}

int CliCommands::listDevices() {
    try {
        // Note: Callback setup removed to avoid lambda issues
        // Will rely on direct device enumeration instead
        
        // Start device discovery monitoring
        CFLOG_INFO("Starting device monitoring...", true);
        QC::DeviceDiscovery::startMonitoring();

        // Wait longer for device enumeration (increased from 10 to 15 seconds)
        CFLOG_INFO("Waiting for device enumeration...", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        
        // Get list of devices with enhanced error handling
        std::list<QC::DeviceInfo> devices;
        try {
            devices = QC::DeviceDiscovery::getDeviceList();
        } catch (const std::exception& e) {
            CFLOG_ERROR(std::string("Error getting device list: ") + e.what(), true);
            CFLOG_ERROR("Continuing with empty device list", true);
        } catch (...) {
            CFLOG_ERROR("Unknown error getting device list", true);
            CFLOG_ERROR("Continuing with empty device list", true);
        }

        // Remove devices with no protocol available
        devices.remove_if([](const QC::DeviceInfo& device) {
            if(device.deviceHandle == 0) {
                return true;
            }
            try {
                std::list<QC::ProtocolInfo> protocolList = QC::DeviceDiscovery::getProtocolList(device.deviceHandle);
                return protocolList.empty();
            } catch (...) {
                return true;
            }
        });

        // Keep only devices in crash mode (Sahara MODE_MEMORY_DEBUG), exclude EDL devices
        const std::regex edlPattern(".*?EDL|QDLoader.*?", std::regex::icase);
        devices.remove_if([&edlPattern](const QC::DeviceInfo& device) {
            try {
                std::list<QC::ProtocolInfo> protocolList = QC::DeviceDiscovery::getProtocolList(device.deviceHandle);
                for (const auto& protocol : protocolList) {
                    // Remove EDL devices
                    if (protocol.protocolType == QC::ProtocolType::PROT_SAHARA &&
                        std::regex_match(protocol.description, edlPattern)) {
                        return true; // Remove this device
                    }
                    if (protocol.protocolType == QC::ProtocolType::PROT_SAHARA &&
                        protocol.deviceMode == QC::DeviceMode::DEVICE_MODE_SAHARA_CRASH) {
                        return false; // Keep this device
                    }
                }
            } catch (...) {
                // If we can't get protocols, filter it out
            }
            return true; // Remove this device
        });

        CFLOG_INFO("Device enumeration completed. Found " + std::to_string(devices.size()) + " device(s).", true);

        if (devices.empty()) {
            CFLOG_INFO(
            std::string("No devices found\n"
                        "Troubleshooting tips:\n"
                        "1. Ensure the device is connected via USB\n"
                        "2. Check if the device is in Crash mode\n"
                        "3. Verify USB permissions (run with sudo if needed)\n"
                        "4. Check if device drivers are installed"),
                true
            );

            QC::DeviceDiscovery::stopMonitoring();
            return 0;
        }

        CFLOG_INFO("Available Devices:\n", true);
        
        for (auto& device : devices) {
            try {
                std::string deviceId = !device.serialNumber.empty() ? device.serialNumber : 
                                        (!device.adbSerialNumber.empty() ? device.adbSerialNumber:
                                         !device.description.empty() ? device.description: "UNKNOWN");
                
                CFLOG_INFO("[ " + deviceId + " ]", true);

                CFLOG_INFO(
                "Device Description : " + (!device.description.empty() ? device.description : "Unknown Device"),
                true
                );
                CFLOG_INFO("Device Handle      : " + std::to_string(device.deviceHandle), true);

                
                if (!device.serialNumber.empty()) {
                    CFLOG_INFO("Serial Number      : " + device.serialNumber, true);
                }
                else {
                    CFLOG_INFO("Serial Number      : UNKNOWN", true);
                }

                if (!device.adbSerialNumber.empty()) {
                    CFLOG_INFO("ADB Serial Number  : " + device.adbSerialNumber, true);
                }
                else {
                    CFLOG_INFO("ADB Serial Number  : UNKNOWN", true);
                }
                
                if (!device.vid.empty()) {
                    CFLOG_INFO("VID                : " + device.vid, true);
                }
                else {
                    CFLOG_INFO("VID               : UNKNOWN", true);
                }

                if (!device.pid.empty()) {
                    CFLOG_INFO("PID                : " + device.pid, true);
                }
                else {
                    CFLOG_INFO("PID                : UNKNOWN", true);
                }

                if (!device.edlChipId.empty()) {
                    CFLOG_INFO("EDL Chip ID        : " + device.edlChipId, true);
                }
                else {
                    CFLOG_INFO("EDL Chip ID        : UNKNOWN", true);
                }

                if (!device.location.empty()) {
                    CFLOG_INFO("Location           : " + device.location, true);
                }
                else {
                    CFLOG_INFO("Location           : UNKNOWN", true);
                }
                
                // Get and display protocols for this device
                try {
                    if (device.deviceHandle != 0) {
                        std::list<QC::ProtocolInfo> protocolList = QC::DeviceDiscovery::getProtocolList(device.deviceHandle);
                        if (!protocolList.empty()) {
                            CFLOG_INFO("Protocols          :", true);
                            for (const auto& protocolInfo : protocolList) {
                                CFLOG_INFO(
                                    "Protocol Type: " + getProtocolName(protocolInfo.protocolType) + " --- " +
                                    protocolInfo.description,
                                    true
                                );
                                if (!protocolInfo.alternateDescription.empty()) {
                                    CFLOG_INFO(" (" + protocolInfo.alternateDescription + ")", true);
                                }
                            }
                        } else {
                            CFLOG_INFO("Protocols          : None available", true);
                        }
                    } else {
                        CFLOG_INFO("Protocols          : Invalid device handle", true);
                    }
                } catch (const std::exception& e) {
                    CFLOG_ERROR(std::string("Protocols          : Error - ") + e.what(), true);
                } catch (...) {
                    CFLOG_ERROR("Protocols          : Unknown error", true);
                }
                
                CFLOG_INFO("\n", true);
                
            } catch (const std::exception& e) {
                CFLOG_ERROR(std::string("Error processing device: ") + e.what(), true);
            } catch (...) {
                CFLOG_ERROR("Unknown error processing device", true);
            }
        }
        
        CFLOG_INFO("Total devices found: " + std::to_string(devices.size()), true);
        
        // Stop monitoring
        CFLOG_INFO("Stopping device monitoring...", true);
        QC::DeviceDiscovery::stopMonitoring();
        return 0;
    } catch (const QC::Common::Exception& e)
   {
        std::ostringstream oss;
        oss << "Error: " << e.getFormattedMessage();

        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        QC_CMDLINE_IGNORE_EXCEPTION(QC::DeviceDiscovery::stopMonitoring()); 
        return 1;
   }

   catch (const std::exception& e)
   {
        std::ostringstream oss;
        oss << "Error executing " << __func__ << "(): " << e.what();
        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        QC_CMDLINE_IGNORE_EXCEPTION(QC::DeviceDiscovery::stopMonitoring()); 
        return 1;
   } 
   
   catch (...)
   {
        std::ostringstream oss;
        oss << "Error executing " << __func__ << "(): " << "(): Unknown Error";

        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        QC_CMDLINE_IGNORE_EXCEPTION(QC::DeviceDiscovery::stopMonitoring()); 
        return 1;
   }
}

QC::DeviceInfo CliCommands::findTargetDevice(const std::string &deviceIdentifier) {
    
    auto matchesIdentifier = [&deviceIdentifier](const QC::DeviceInfo& device) -> bool {
        // Match by device handle (converted to string) or serial number
        std::string upperDeviceIdentifier = toUpper(deviceIdentifier);
        std::string upperAdbSerialNumber = toUpper(device.serialNumber);
        std::string upperSerialNumber = toUpper(device.adbSerialNumber);
        std::string upperDeviceDescription = toUpper(device.description);
        return (upperSerialNumber == upperDeviceIdentifier ||
            upperAdbSerialNumber == upperDeviceIdentifier ||
            upperDeviceDescription == upperDeviceIdentifier);
    };
    
    QC::DeviceInfo targetDevice;
    bool deviceFound = false;

    auto startTime = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(30);
    
    while (!deviceFound) {

        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if(elapsed >= timeout)
        {
            break; // Timeout reached
        }

        // Get current device list
        std::list<QC::DeviceInfo> devices = QC::DeviceDiscovery::getDeviceList();

        // Check if our target device is in the list
        bool foundInThisScan = false;
        for(const auto& device: devices)
        {
            if(matchesIdentifier(device))
            {
                targetDevice = device;
                foundInThisScan = true;

                if(!deviceFound)
                {
                    CFLOG_INFO("Target device found: " + device.description, false);
                    deviceFound = true;
                }

            }
        }

        if(deviceFound && !foundInThisScan)
        {
            CFLOG_INFO("Device was removed.", false);
            deviceFound = false;
        }

        // If we found device we're done
        if(deviceFound)
        {
            break;
        }

        // Wait 1 second before next poll
        std::this_thread::sleep_for(std::chrono::seconds(1));
       
    }

    if(!deviceFound)
   {
        CFLOG_ERROR("Error: Device not found: " + deviceIdentifier, false);
        // Get current device list for error reporting
        std::list<QC::DeviceInfo> currentDevices = QC::DeviceDiscovery::getDeviceList();
        
        CFLOG_ERROR("Available devices:", false);
        for (const auto &device : currentDevices) {
            CFLOG_ERROR("  - Handle: " + std::to_string(device.deviceHandle), false);
            if (!device.serialNumber.empty()) {
                CFLOG_ERROR(", Serial: " + device.serialNumber, false);
            }
            if (!device.adbSerialNumber.empty()) {
                CFLOG_ERROR(", ADB Serial: " + device.adbSerialNumber, false);
            }
            CFLOG_ERROR("\n", false);
        }
        QC_THROW_DEVICE_ERROR(QC::Common::Exception::DEVICE_NOT_FOUND, deviceIdentifier, "device discovery");
   }

    CFLOG_INFO(
        "Found target device:\n"
        "  Handle: " +
            std::to_string(targetDevice.deviceHandle) + "  Description: " + targetDevice.description,
        false
    );   
    CFLOG_INFO(
      "Found target device:\n"
      "  Handle: " +
         std::to_string(targetDevice.deviceHandle) + "  Description: " + targetDevice.description,
      false
    );

    if(!targetDevice.serialNumber.empty())
    {
        CFLOG_INFO("  Serial Number: " + targetDevice.serialNumber, false);
    }
    CFLOG_INFO("\n", false);

    return targetDevice;
}

int CliCommands::displayVersion() {
    CliHelp::showVersion();
    return 0;
}

int CliCommands::collectMemoryDump(const CliOptions& options) {
    std::unique_ptr<QC::CrashCollection> crashCollection;
    try {
        CFLOG_INFO("Target Device: " + options.deviceId, true);
        
        // Simple spinner for device discovery
        Spinner discoverySpinner;
        QC::DeviceDiscovery::startMonitoring();
        // Wait longer for device enumeration (increased from 10 to 15 seconds)
        CFLOG_INFO("Waiting for device enumeration (15 seconds)...", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(15000));
        // Find the target device
        QC::DeviceInfo targetDevice = findTargetDevice(options.deviceId);
        
        // Create CrashCollection instance
        CFLOG_INFO("Initializing Crash collection service...", true);
        crashCollection.reset(new QC::CrashCollection(targetDevice));
        
        // Initialize the service
        QC::ErrorType result = crashCollection->initializeService();
        THROW_FROM_ERROR_TYPE(result, "service initialization");
            
        CFLOG_INFO("Service initialized successfully.", true);
        CFLOG_INFO("Collecting memory dump...", true);

        ProgressWrapper progressWrapper("Crash Collection");
        result = progressWrapper.execute([&]() {
            return crashCollection->collectMemoryDumpWithOptions(options.memoryDumpOptions);
        });

        // Throw exception if crash collection failed
        THROW_FROM_ERROR_TYPE(result, "crash collection");
        CFLOG_INFO("Crash dump files located at: " + options.memoryDumpOptions.pathName, true);

        CFLOG_INFO("Resetting device...", true);
        result = crashCollection->resetDevice(0);
        THROW_FROM_ERROR_TYPE(result, "reset device failed");

        // Cleanup
        CFLOG_INFO("Cleaning up...", true);
        crashCollection->destroyService();
        QC::DeviceDiscovery::stopMonitoring();
        return 0;
   } catch (const QC::Common::Exception& e)
   {
        std::ostringstream oss;
        oss << "Error " 
            << e.getFormattedMessage();
        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);

        QC_TRY_CLEANUP(crashCollection);
        return 1;
   } catch (const std::exception& e)
   {
        std::ostringstream oss;
        oss << "Error executing " << __func__ << "(): " << e.what();
        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        
        QC_TRY_CLEANUP(crashCollection);
        return 1;
   } catch (...)
   {
        std::ostringstream oss;
        oss << "Error executing " << __func__ << "(): " << "(): Unknown Error";

        CFLOG_ERROR(oss.str(), /*printMsgOnly=*/false);
        QC_TRY_CLEANUP(crashCollection);
        return 1;
   }
}

} // namespace CLI
} // namespace QC
