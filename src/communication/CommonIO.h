// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#pragma once
#include "communication/Fwd.h"
#include "device/Buffer.h"
#include "device/Fwd.h"
#include "device/Logger.h"

#include <functional>

namespace Device {
namespace Communication {

// ----------------------------------------------------------------------------
// CommonIo
//
/// Interface for all protocols to inherit from
// ----------------------------------------------------------------------------
class CommonIo
{
public:
   typedef std::function<void(const Device::SharedByteBufferPtr&)> ReceiveDelegate;

   virtual std::string getDescription() const = 0;
   virtual std::string getIdentifier() const = 0;
   virtual void setIdentifier(const std::string& identifier) = 0;
   virtual std::string getSerialNumber() const = 0;
   virtual bool isOpen() const = 0;
   virtual void open() = 0;
   virtual void close() = 0;
   virtual void reset() = 0;
   virtual uint64_t sendSync(const Device::SharedByteBufferPtr& pBuffer) = 0;
   virtual void registerReceiveAsync(const ReceiveDelegate& callback) = 0;
};

} // namespace Communication
} // namespace Device
