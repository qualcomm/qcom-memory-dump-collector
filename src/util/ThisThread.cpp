// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "ThisThread.h"

#include <cassert>
#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>


namespace Util {

// --------------------------------------------------------------------------
// getId
//
// --------------------------------------------------------------------------
ThreadId ThisThread::getId()
{
   std::hash<std::thread::id> hasher;
   return static_cast<ThreadId>(hasher(std::this_thread::get_id()));
}

// --------------------------------------------------------------------------
// sleep
//
/// Tells this thread to sleep for a given duration
// --------------------------------------------------------------------------
void ThisThread::sleep(const std::optional<std::chrono::milliseconds>& duration)
{
   if(!duration.has_value()) return; // nothing to do
   std::this_thread::sleep_for(duration.value());
}

// --------------------------------------------------------------------------
// waitForEvent
//
/// Waits for the given event to signal
/// Return true if event is signaled
// --------------------------------------------------------------------------
bool ThisThread::waitForEvent(const Util::Event* pEvent, const std::optional<std::chrono::milliseconds>& timeout)
{
   if(!pEvent)
   {
      throw std::invalid_argument("CurrentThread::waitForEvent: pEvent is nullptr");
   }
   return pEvent->wait(timeout) ? WAIT_RESULT_SUCCESS : WAIT_RESULT_UNKNOWN;
}

} // namespace Util
