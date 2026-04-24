// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "CliParserUtil.h"

#include <algorithm>
#include <cctype>

namespace QC {
namespace CLI {

    std::string CliParserUtil::toUpper(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                    [](unsigned char c) { return std::toupper(c); });
        return result;
    }

}
}
