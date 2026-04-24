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

    CliOptions():memoryDumpOptions("") {}

    ~CliOptions() {}

    void setCommandType(CliOptions::CommandType command) {
        if (this->command == CliOptions::CommandType::NONE) {
            this->command = command;
            return;
        } 
        throw std::invalid_argument("Duplicated command type.");
    }

    /**
     * @brief Prints human readable command
     * 
     * @param cmd Current command
     * @return Human readable command
     */
    static std::string getCommandString(const CommandType &cmd)
    {
         switch (cmd) {
            case CommandType::NONE: return "NONE";
            case CommandType::LIST_DEVICES: return "LIST_DEVICES";
            case CommandType::COLLECT_MEMORY_DUMP: return "COLLECT_MEMORY_DUMP";
            case CommandType::DISPLAY_VERSION: return "DISPLAY_VERSION";
            case CommandType::HELP: return "HELP";
            default: return std::string("UNKNOWN_COMMAND (") + std::to_string(static_cast<int>(cmd)) + ")";
        }
    }
    
};

} // namespace CLI

} // namespace QC
