// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "ProgressWrapper.h"

#include "Definitions.h"

#include <KL/kLogger.h>
#include <stdexcept>

namespace QC {
namespace CLI {

// Initialize static members
Spinner* ProgressWrapper::g_pActiveProgressSpinner = nullptr;
std::string ProgressWrapper::g_activeOperationName;
std::mutex ProgressWrapper::g_callbackMutex;

ProgressWrapper::ProgressWrapper(const std::string& operationName)
: m_operationName(operationName)
, m_durationSeconds(0)
, m_bIsActive(false)
{
}

ProgressWrapper::~ProgressWrapper()
{
   if(m_bIsActive)
   {
      stopProgress();
   }
}

void ProgressWrapper::startProgress()
{
   if(m_bIsActive)
   {
      return; // Already started
   }

   // Configure line offsets for multi-line display
   // IMPORTANT: Ensure we're on a fresh line first, then create blank lines for spinners
   // This prevents overwriting any previous output
   KL::Logger::get_instance().writeRawConsole("\n");  // Move to new line (from any previous output)

   // Create space for event spinner line
   KL::Logger::get_instance().writeRawConsole("\n");

   // Start event spinner on this fresh line with offset 0 (no cursor movement)
   // m_eventSpinner.setLineOffset(0);
   // m_eventSpinner.start("Events");

   // Move to next line for progress spinner
   KL::Logger::get_instance().writeRawConsole("\n");

   // Start progress spinner on this fresh line
   m_progressSpinner.setLineOffset(0);
   m_progressSpinner.start("Initializing " + m_operationName);

   // NOW set the proper offsets for future updates
   // Event spinner should update 1 line up, progress spinner on current line
   // m_eventSpinner.setLineOffset(1);
   m_progressSpinner.setLineOffset(0);

   // Set global spinner pointers for callback access
   g_pActiveProgressSpinner = &m_progressSpinner;
   g_activeOperationName = m_operationName;
   // g_pActiveEventSpinner = &m_eventSpinner;

   // Note: KLogger line offset is now automatically managed by Spinner::start()
   // Each spinner registers itself with kLogger when started

   // Set up service event callback
   QC::DeviceDiscovery::setServiceEventCallback(&serviceEventCallback);

   m_bIsActive = true;
}

void ProgressWrapper::stopProgress()
{
   if(!m_bIsActive)
   {
      return; // Already stopped
   }

   // Clear service event callback
   QC::DeviceDiscovery::setServiceEventCallback(nullptr);

   // Note: KLogger line offset is now automatically managed by Spinner::stop()
   // Each spinner unregisters itself from kLogger when stopped

   // Clear global spinner pointers
   g_pActiveProgressSpinner = nullptr;
   g_activeOperationName.clear();
   // g_pActiveEventSpinner = nullptr;

   // Finish spinner with completion message (ensures proper newline for subsequent output)
   m_progressSpinner.finish(m_operationName + " complete");
   // m_eventSpinner.stop();

   m_bIsActive = false;
}

QC::ErrorType ProgressWrapper::execute(std::function<QC::ErrorType()> operation)
{
   // Start progress tracking
   startProgress();

   // Record start time
   auto startTime = std::chrono::high_resolution_clock::now();

   QC::ErrorType result;
   try
   {
      // Execute the operation
      result = operation();
   }
   catch(...)
   {
      // Ensure cleanup happens even if exception is thrown
      stopProgress();
      throw;
   }

   // Record end time
   auto endTime = std::chrono::high_resolution_clock::now();
   m_durationSeconds = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

   // Stop progress tracking
   stopProgress();

   return result;
}

#ifdef TOOLS_TARGET_WINDOWS
void __stdcall ProgressWrapper::
   serviceEventCallback(const std::string& serviceName, int64_t eventId, const std::string& eventDescription)
#else
void ProgressWrapper::
   serviceEventCallback(const std::string& serviceName, int64_t eventId, const std::string& eventDescription)
#endif
{
   (void)serviceName; // Suppress unused parameter warning
   (void)eventId;     // Suppress unused parameter warning

   // Lock mutex to protect static member access from race conditions
   std::lock_guard<std::mutex> lock(g_callbackMutex);

   // Early return if spinners not active
   if (nullptr == g_pActiveProgressSpinner)
   {
      return; // No spinners active
   }

   // Parse percentage from event description and update progress spinner
   // Events come in formats like:
   //   "Percentage completed: 25"
   //   "Downloading partions table, partition count: 5"
   //   "Partition completed: filename.bin"
   const std::string percentPrefix = "Percentage completed: ";
   if (eventDescription.find(percentPrefix) == 0)
   {
      try
      {
         int percentage = std::stoi(eventDescription.substr(percentPrefix.length()));
         g_pActiveProgressSpinner->update(percentage, g_activeOperationName);
      }
      catch (const std::exception&)
      {
         // Ignore parse errors
      }
   }
}

} // namespace CLI
} // namespace QC
