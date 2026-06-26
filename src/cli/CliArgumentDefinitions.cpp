// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "CliArgumentDefinitions.h"
#include "CliOptions.h"
#include "ExceptionHandler.h"
#include "FileSystem.h"
#include <KL/kLogger.h>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace QC {
namespace CLI {

#define REQUIRE_ABSOLUTE_PATH(somepath) if (!FileSystem::isAbsolutePath(somepath)) { \
           QC_THROW_CMDLINE_ERROR(QC::Common::Exception::INVALID_PARAMETERS, "path", "Relative path is not supported: " + value); \
        }
// Static member definitions
std::map<std::string, ArgumentDefinition> CliArgumentDefinitions::argumentDefinitions;
std::vector<CommandDefinition> CliArgumentDefinitions::commandDefinitions;
bool CliArgumentDefinitions::initialized = false;

const std::map<std::string, ArgumentDefinition>& CliArgumentDefinitions::getArgumentDefinitions() {
    if (!initialized) {
        initialize();
    }
    return argumentDefinitions;
}

const std::vector<CommandDefinition>& CliArgumentDefinitions::getCommandDefinitions() {
    if (!initialized) {
        initialize();
    }
    return commandDefinitions;
}

std::vector<ArgumentDefinition> CliArgumentDefinitions::getArgumentsByCategory(ArgumentCategory category) {
    if (!initialized) {
        initialize();
    }
    
    std::vector<ArgumentDefinition> result;
    for (const auto& pair : argumentDefinitions) {
        if (pair.second.category == category) {
            result.push_back(pair.second);
        }
    }
    return result;
}

const ArgumentDefinition* CliArgumentDefinitions::getArgumentDefinition(const std::string& name) {
    if (!initialized) {
        initialize();
    }
    
    auto it = argumentDefinitions.find(name);
    return (it != argumentDefinitions.end()) ? &it->second : nullptr;
}

void CliArgumentDefinitions::initialize() {
    if (initialized) return;
    // Sort options alphabetically in word

    // DEVICE OPTIONS
    argumentDefinitions["device"] = {
        "device",
        "Specify target device identifier for device operation",
        ArgumentType::STRING,
        ArgumentCategory::DEVICE_OPTIONS,
        "",
        {},
        true,
        "12345",
        "Use SERIAL NUMBER or DEVICE DESCRIPTION from --devices command",
        [](QC::CLI::CliOptions& options, const std::string& value) {
            options.deviceId = value;
        }
    };

    argumentDefinitions["path-name"] = {
        "path-name",
        "",
        ArgumentType::STRING,
        ArgumentCategory::MEMORY_OPTIONS,
        "",
        {},
        true,
        "/path/to/store/memory/dump/files",
        "Absolute path to store memory dump files. "
        "Location must have write permissions.",
        [](QC::CLI::CliOptions& options, const std::string& value) {
            if(FileSystem::isDirectory(value))
            {
                // Convert to absolute path
                options.memoryDumpOptions.__set_pathName(value);
            }
            else
            {
                QC_THROW_FILE_ERROR(QC::Common::Exception::DIRECTORY_NOT_FOUND, value, "--path-name directory validation");
            }
            
            if(!FileSystem::isDirectoryWritable(value))
            {
                QC_THROW_FILE_ERROR(QC::Common::Exception::FILE_WRITE_ERROR, value, "--path-name directory validation - Please specify a directory with write permissions");
            }
        }
    };

    // LOGGING
    argumentDefinitions["verbose"] = {
        "verbose",
        "Enable verbose logging output",
        ArgumentType::FLAG,
        ArgumentCategory::LOGGING,
        "",
        {},
        false,
        "qmdc --devices --verbose",
        "Shows detailed operation logs and debug information",
        [](QC::CLI::CliOptions& options, const std::string& value) {
            options.verbose = true;
            options.logOptions |= KL::LogOption::DebugToFile;
            KL::Logger::get_instance().setOptions(options.logOptions);
            KL::Logger::get_instance().setLevel(KL::Level::Debug);
        }
    };

     argumentDefinitions["port-trace"] = {
      "port-trace",
      "Enable port trace logging output",
      ArgumentType::FLAG,
      ArgumentCategory::LOGGING,
      "",
      {},
      false,
      "qil --devices --port-trace",
      "Shows detailed operation logs and debug information",
      [](QC::CLI::CliOptions& options, const std::string& value) {
         options.portTrace = true;
         options.logOptions |= KL::LogOption::PtraceToFile;;
         KL::Logger::get_instance().setOptions(options.logOptions);
         KL::Logger::get_instance().setLevel(KL::Level::Data);
      }
   };

    // Command definitions
    commandDefinitions = {
        {
            "devices",
            "List all available device identifiers",
            {},
            {
                argumentDefinitions["verbose"],
                argumentDefinitions["port-trace"]
            },
            "qmdc --devices",
            CliOptions::CommandType::LIST_DEVICES,
            nullptr
        },
        {
            "crash-collection",
            "collect memory dump",
            {
                argumentDefinitions["device"],
                argumentDefinitions["path-name"],
            },
            {
                argumentDefinitions["verbose"],
                argumentDefinitions["port-trace"]
            },
            "qmdc --crash-collection --device=<MSM_SERIAL_ID> --path-name=<PATH_TO_STORE_DUMP_FILES>",
            CliOptions::CommandType::COLLECT_MEMORY_DUMP,
            nullptr
        },
        {
            "help",
            "Display help information",
            {},
            {},
            "qmdc --help",
            CliOptions::CommandType::HELP,
            nullptr
        },
        {
            "-h",
            "Display help information",
            {},
            {},
            "qmdc -h",
            CliOptions::CommandType::HELP,
            nullptr
        },
        {
            "version",
            "Display qmdc Application version",
            {},
            {},
            "qmdc --version",
            CliOptions::CommandType::DISPLAY_VERSION,
            nullptr
        }
    };

    initialized = true;
}

} // namespace CLI
} // namespace QC
