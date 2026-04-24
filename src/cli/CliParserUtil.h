// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once

#include <string>

namespace QC {
namespace CLI {
class CliParserUtil
{
public:

    /**
     * @brief Convert string to uppercase
     *
     * @param str Input string
     * @return Uppercase string
     */
    static std::string toUpper(const std::string& str);
};

}
}