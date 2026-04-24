// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "function/Fwd.h"
#include "protocol/Sahara.h"
#include "util/AppEvent.h"
#include "util/Event.h"

#include <memory>
#include <string>
#include <vector>

namespace Function {

// ----------------------------------------------------------------------------
// ImageTransferEvent
//
/// Notifies differnt stages of the image transfer
// ----------------------------------------------------------------------------
class ImageTransferEvent : public Util::Event
{
   TOOLS_FORBID_COPY(ImageTransferEvent);

public:
   enum EventId
   {
      INITIALIZE = 0,
      SAHARA_TRANSFER_IMAGE,
      SAHARA_RESETTING,
   };

   ImageTransferEvent(EventId eventId, const std::string& description);
   virtual ~ImageTransferEvent();

   EventId getEventId() const;
   std::string getDescription() const;

private:
   EventId m_eventId;
   std::string m_description;
};

// ----------------------------------------------------------------------------
// ImageTransfer
//
/// Downloads build to device over the Sahara protocol
// ----------------------------------------------------------------------------
class ImageTransfer : public Util::EventPublisher
{
   TOOLS_FORBID_COPY(ImageTransfer);

public:
   ImageTransfer(const Device::ConnectionPtr& pSaharaConnection);

   virtual ~ImageTransfer();

private:
   Device::SharedByteBufferPtr saharaWaitForHandshakeReady(
      const Device::Protocol::Sahara::Mode mode,
      const uint32_t leftoverPacketCount = 1,
      const std::chrono::milliseconds& timeout = std::chrono::milliseconds(10000)
   );
   Device::SharedByteBufferPtr saharaInitializeHelloRequest(
      const Device::Protocol::Sahara::Mode mode,
      const std::chrono::milliseconds& timeout = std::chrono::milliseconds(10000)
   );
   Device::Protocol::Sahara::Mode saharaWaitForNextImage(
      uint64_t& imageId,
      uint64_t& headerSize,
      bool& bMore,
      const std::chrono::milliseconds& timeout = std::chrono::milliseconds(10000)
   );
   void saharaTransferSingleImage(const std::filesystem::path& imageFile, uint64_t headerSize, bool& bMore);
   void saharaTransferImageRegion(const std::filesystem::path& imageFile, uint64_t offset, uint64_t length);
   void saharaReset();

   Device::ConnectionPtr m_pSaharaConnection; ///< Connection to Sahara
};

} // namespace Function
