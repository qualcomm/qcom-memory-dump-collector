// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once

#include "CliOptions.h"

namespace QC {
namespace CLI {

/**
 * @brief Command execution logic
 * 
 * Executes commands based on parsed CLI options
 */
class CliCommands {
public:
    /**
     * @brief Execute command based on options
     * 
     * @param options Parsed CLI options
     * @return Exit code (0 = success, non-zero = error)
     */
    static int execute(const CliOptions& options);

private:
    /**
     * @brief List all available devices
     * 
     * @return Exit code
     */
    static int listDevices();
    
   

    /**
     * @brief Find target device by identifier
     * 
     * @param deviceIdentifier Device identifier to search for.
     */
    static QC::DeviceInfo findTargetDevice(const std::string &deviceIdentifier);

    /**
    * @brief Get QFS Memory Dump Collector Application version
    * 
    * @return Exit code
    */
    static int displayVersion();
    
    /**
     * @brief Collect memory dump from device
     * 
     * @param options CLI options
     */
    static int collectMemoryDump(const CliOptions& options);
    
    /**
     * @brief Get the protocol name from enum value
     * 
     * @param type Protocol enum
     */
    static std::string getProtocolName(QC::ProtocolType type);
};

} // namespace CLI
} // namespace QC
