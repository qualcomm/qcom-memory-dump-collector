// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once

#include "FileSystem.h"
#include "ImageManagementDefinitions.h"
#include <KL/kLogger.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace QC {
namespace CLI {

/**
 * @brief Command-line options data structure
 * 
 * This structure holds all parsed command-line arguments for the
 * Qualcomm Memory Dump CLI application.
 */
class CliOptions {
public:
    /**
     * @brief Command type enumeration
     */
    enum class CommandType {
        NONE,
        LIST_DEVICES,
        COLLECT_MEMORY_DUMP,
        DISPLAY_VERSION,
        HELP,
    };

    // Command type
    CommandType command = CommandType::NONE;

    // Device identification
    std::string deviceId;               // --device   

    // Embedded MemoryDumpOptions object
    QC::MemoryDumpOptions memoryDumpOptions;

    // Logging
    bool verbose = false;               // --verbose
    KL::LogOption logOptions = KL::LogOption::None;

    bool portTrace = false; // --verbose

    CliOptions():memoryDumpOptions("") {}

    ~CliOptions() {}

    void setCommandType(CliOptions::CommandType command) {
        if (this->command == CliOptions::CommandType::NONE) {
            this->command = command;
            return;
        } 
        throw std::invalid_argument("Duplicated command type.");
    }

};

} // namespace CLI

} // namespace QC
