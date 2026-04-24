// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "Event.h"

#include <chrono>
#include <optional>

namespace Util {

using ThreadId = unsigned int;

// ----------------------------------------------------------------------------
// ThisThread
// ----------------------------------------------------------------------------
class ThisThread
{
public:
   enum WaitResult
   {
      WAIT_RESULT_SUCCESS = 0,
      WAIT_RESULT_UNKNOWN = 0xFFFFFFFF
   };

   static ThreadId getId();
   static void sleep(const std::optional<std::chrono::milliseconds>& duration);
   static bool
   waitForEvent(const Util::Event* pEvent, const std::optional<std::chrono::milliseconds>& timeout = std::nullopt);
};

} // namespace Util
