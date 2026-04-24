// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
// Prevent ambiguous 'byte' symbol conflict between Windows headers and
// std::byte Must be defined before any Windows headers are included
#ifndef _HAS_STD_BYTE
#define _HAS_STD_BYTE 0
#endif
#include "function/ImageTransfer.h"

#include "device/Buffer.h"
#include "device/Connection.h"
#include "device/ErrorMessage.h"
#include "device/Exception.h"
#include "device/Impl.h"
#include "device/Manager.h"
#include "util/ThisThread.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Device::Protocol;

namespace Function {

static const uint64_t MAX_SAHARA_WRITE = 1024 * 1024;
static const uint32_t MAX_SAHARA_LEFTOVER_PACKETS_COUNT = 3;

// ----------------------------------------------------------------------------
// ImageTransferEvent
//
// ----------------------------------------------------------------------------
ImageTransferEvent::ImageTransferEvent(EventId eventId, const std::string& description)
: Util::Event()
, m_eventId(eventId)
, m_description(description)
{
}

// ----------------------------------------------------------------------------
// ~ImageTransferEvent
//
// ----------------------------------------------------------------------------
ImageTransferEvent::~ImageTransferEvent()
{
}

// ----------------------------------------------------------------------------
// getEventId
//
// ----------------------------------------------------------------------------
ImageTransferEvent::EventId ImageTransferEvent::getEventId() const
{
   return m_eventId;
}

// ----------------------------------------------------------------------------
// getDescription
//
// ----------------------------------------------------------------------------
std::string ImageTransferEvent::getDescription() const
{
   return m_description;
}

// ----------------------------------------------------------------------------
// ImageTransfer
//
// ----------------------------------------------------------------------------
ImageTransfer::
   ImageTransfer(const Device::ConnectionPtr& pSaharaConnection)
: m_pSaharaConnection(pSaharaConnection)
{
}

// ----------------------------------------------------------------------------
// ~ImageTransfer
//
// ----------------------------------------------------------------------------
ImageTransfer::~ImageTransfer()
{
}

// ----------------------------------------------------------------------------
// saharaWaitForHandshakeReady
//
/// wait for a sahara mode ready
// ----------------------------------------------------------------------------
Device::SharedByteBufferPtr ImageTransfer::saharaWaitForHandshakeReady(
   const Device::Protocol::Sahara::Mode mode,
   const uint32_t leftoverPacketCount,
   const std::chrono::milliseconds& timeout
)
{
   SaharaPtr pSahara = (m_pSaharaConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();
   Device::SharedByteBufferPtr pBuffer;

   // Check if there is already a packet waiting. Skip leftover packets
   for(uint32_t i = 0; i < leftoverPacketCount; ++i)
   {
      if(0 == i)
      {
         pBuffer = pSahara->getFrame(timeout);
      }
      else
      {
         pBuffer = pSahara->getFrame();
      }
      if(pBuffer == nullptr)
      {
         // Timeout, break and try sending hello response later later
         FLOG_WARNING(
            "No packet received from device waiting for initial "
            "hello (image_tx_pending state) waitMode: " +
            std::to_string(mode) + " in Sahara protocol: " + pSahara->getDescription()
         );
         return nullptr;
      }
      else
      {
         const Sahara::FrameHeader* pHeader =
            Util::buffer_cast<const Sahara::FrameHeader*>(pBuffer->begin(), pBuffer->size());

         if(Sahara::SAHARA_HELLO == pHeader->m_command)
         {
            // valid command or hello, continue
            return pBuffer;
         }
         else if(Sahara::Mode::MODE_IMAGE_TX_PENDING == mode &&
                 (Sahara::CommandId::SAHARA_READ_DATA == pHeader->m_command ||
                  Sahara::CommandId::SAHARA_64_BIT_READ_DATA == pHeader->m_command))
         {
            return pBuffer;
         }
         else if(Sahara::Mode::MODE_COMMAND == mode && Sahara::CommandId::SAHARA_COMMAND_READY == pHeader->m_command)
         {
            return pBuffer;
         }
         else
         {
            // left over command, wait for next command
            FLOG_WARNING(
               "Leftover sahara packet from device waiting for initial hello "
               "(image_tx_pending state): " +
               std::string(std::to_string(pHeader->m_command)) +
               " size: " + std::string(std::to_string(pHeader->m_length)) + " waitMode: " + std::to_string(mode) +
               " in Sahara protocol: " + pSahara->getDescription()
            );
         }
      }
   }
   return nullptr;
}

// ----------------------------------------------------------------------------
// saharaInitializeHelloRequest
//
/// sahara wait for initialize hello handshake process
// ----------------------------------------------------------------------------
Device::SharedByteBufferPtr
ImageTransfer::saharaInitializeHelloRequest(const Sahara::Mode mode, const std::chrono::milliseconds& timeout)
{
   SaharaPtr pSahara = (m_pSaharaConnection->getProtocol()).dynamicCast<Device::Protocol::Sahara>();
   Device::SharedByteBufferPtr pSendBuffer;
   Device::SharedByteBufferPtr pBuffer;
   Device::SharedByteBufferPtr pHelloResponseBuffer =
      pSahara->createCommand<Sahara::HelloResponse>(Sahara::SAHARA_HELLO_RESP);
   Sahara::HelloResponse* pHelloResp =
      Util::buffer_cast<Sahara::HelloResponse*>(pHelloResponseBuffer->begin(), pHelloResponseBuffer->size());

   pBuffer = saharaWaitForHandshakeReady(mode, MAX_SAHARA_LEFTOVER_PACKETS_COUNT, timeout);

   // If no incoming packet, send a hello response assuming hello packet has
   // been consumed
   if(pBuffer == nullptr)
   {
      FLOG_WARNING(
         "Unframed packet received from device waiting for initial hello "
         "(image_tx_pending state) try sending hello response with mode:" +
         std::to_string(mode) + " in Sahara protocol: " + pSahara->getDescription()
      );
      pHelloResp->m_versionNumber = pSahara->getSaharaVersion();
      pHelloResp->m_versionCompatible = Sahara::SUPPORTED_LOWEST_VERSION;
      pHelloResp->m_status = Sahara::STATUS_SUCCESS;
      pHelloResp->m_mode = mode;
      pHelloResp->m_reserved[0] = 1;
      pHelloResp->m_reserved[1] = 2;
      pHelloResp->m_reserved[2] = 3;
      pHelloResp->m_reserved[3] = 4;
      pHelloResp->m_reserved[4] = 5;
      pHelloResp->m_reserved[5] = 6;
      m_pSaharaConnection->sendSync(pHelloResponseBuffer);

      pBuffer = saharaWaitForHandshakeReady(mode, MAX_SAHARA_LEFTOVER_PACKETS_COUNT);
   }

   // If still no incoming packet, force a sahara state machine reset
   if(pBuffer == nullptr)
   {
      pSendBuffer = pSahara->createCommand<Sahara::ResetStateMachine>(Sahara::SAHARA_RESET_SAHARA_STATE_MACHINE);
      m_pSaharaConnection->sendSync(pSendBuffer);

      // Get a hello from the device
      pBuffer = saharaWaitForHandshakeReady(mode, MAX_SAHARA_LEFTOVER_PACKETS_COUNT);
   }

   TOOLS_ASSERT_OR_THROW(
      pBuffer != nullptr,
      Device::Exception(
         Device::Exception::DEVICE_INVALID_PACKET,
         "No packet received from device waiting for reset "
         "state machine (image_tx_pending state)" +
            pSahara->getDescription()
      )
   );

   return pBuffer;
}

// ----------------------------------------------------------------------------
// saharaWaitForNextImage
//
/// Wair for a new image transfer request from device
// ----------------------------------------------------------------------------
Sahara::Mode ImageTransfer::saharaWaitForNextImage(
   uint64_t& imageId,
   uint64_t& headerSize,
   bool& bMore,
   const std::chrono::milliseconds& timeout
)
{
   SaharaPtr pSahara = (m_pSaharaConnection->getProtocol()).dynamicCast<Sahara>();
   Device::SharedByteBufferPtr pSendBuffer;
   Device::SharedByteBufferPtr pBuffer;
   const Sahara::FrameHeader* pHeader = nullptr;

   if(pSahara->isCommandModeEnabled())
   {
      pSendBuffer = pSahara->createCommand<Sahara::CommandSwitchMode>(Sahara::SAHARA_COMMAND_SWITCH_MODE);
      Sahara::CommandSwitchMode* pCmdSwitchMode =
         Util::buffer_cast<Sahara::CommandSwitchMode*>(pSendBuffer->begin(), pSendBuffer->size());
      pCmdSwitchMode->m_mode = Sahara::Mode::MODE_IMAGE_TX_PENDING;
      pSahara->sendSync(pSendBuffer);
   }
   // Returned buffer is never NULL, otherwise a exception generated in
   // saharaInitializeHelloRequest
   pBuffer = saharaInitializeHelloRequest(Sahara::Mode::MODE_IMAGE_TX_PENDING, timeout);

   pHeader = Util::buffer_cast<const Sahara::FrameHeader*>(pBuffer->begin(), pBuffer->size());

   if(Sahara::SAHARA_HELLO == pHeader->m_command)
   {
      const Sahara::Hello* pHello = Util::buffer_cast<const Sahara::Hello*>(pBuffer->begin(), pBuffer->size());

      TOOLS_ASSERT_OR_THROW(
         Sahara::SAHARA_HELLO == pHello->m_header.m_command &&
            TOOLS_SIZEOF(Sahara::Hello) == pHello->m_header.m_length &&
            (Sahara::Mode::MODE_IMAGE_TX_PENDING == pHello->m_mode ||
             Sahara::Mode::MODE_COMMAND == pHello->m_mode              // mode_command for DDR store case
             || Sahara::Mode::MODE_IMAGE_TX_COMPLETE == pHello->m_mode // image_tx_complete means one last image to
                                                                       // be transferred and dont expect any further
             || Sahara::Mode::MODE_MEMORY_DEBUG == pHello->m_mode),    // Crash recovery after image transfer in
                                                                       // flashless mode
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Image transfer mode not available in Sahara protocol: " + pSahara->getDescription()
         )
      );

      pSahara->setMode(static_cast<Sahara::Mode>(pHello->m_mode));
      pSahara->setSaharaVersion(pHello->m_versionNumber);
      // The are two image transfer scenatios that requires special handling
      // 1. target initiated command_mode in between image_tx_pending mode, so
      // it means DDR store request
      // 2. Target enters memory debug mode after image transfer. This happens
      // when a flashless target crashes. Do not send HelloResponse here, leave
      // it to subsequent Sahara function
      if(Sahara::Mode::MODE_COMMAND == pHello->m_mode || Sahara::Mode::MODE_MEMORY_DEBUG == pHello->m_mode)
      {
         return pSahara->getMode();
      }

      // if not Sahara::MODE_COMMAND, could be Sahara::MODE_IMAGE_TX_PENDING or
      // Sahara::MODE_IMAGE_TX_COMPLETE Send a hello response
      Device::SharedByteBufferPtr pHelloResponseBuffer =
         pSahara->createCommand<Sahara::HelloResponse>(Sahara::SAHARA_HELLO_RESP);
      Sahara::HelloResponse* pHelloResp =
         Util::buffer_cast<Sahara::HelloResponse*>(pHelloResponseBuffer->begin(), pHelloResponseBuffer->size());
      pHelloResp->m_versionNumber = pHello->m_versionNumber;
      pHelloResp->m_versionCompatible = pHello->m_versionNumber;
      pHelloResp->m_status = Sahara::STATUS_SUCCESS;
      if(Sahara::Mode::MODE_IMAGE_TX_PENDING == pHello->m_mode)
      {
         pHelloResp->m_mode = Sahara::MODE_IMAGE_TX_PENDING;
      }
      if(Sahara::Mode::MODE_IMAGE_TX_COMPLETE == pHello->m_mode)
      {
         pHelloResp->m_mode = Sahara::MODE_IMAGE_TX_COMPLETE;
         bMore = false;
      }
      pHelloResp->m_reserved[0] = 1;
      pHelloResp->m_reserved[1] = 2;
      pHelloResp->m_reserved[2] = 3;
      pHelloResp->m_reserved[3] = 4;
      pHelloResp->m_reserved[4] = 5;
      pHelloResp->m_reserved[5] = 6;
      m_pSaharaConnection->sendSync(pHelloResponseBuffer);

      // Get a READ_DATA from the device
      pBuffer = pSahara->getFrame();
      TOOLS_ASSERT_OR_THROW(
         pBuffer != nullptr,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "No packet received from device waiting for next "
            "image (image_tx_pending state)" +
               pSahara->getDescription()
         )
      );
      pHeader = Util::buffer_cast<const Sahara::FrameHeader*>(pBuffer->begin(), pBuffer->size());
   }
   else if(Sahara::CommandId::SAHARA_READ_DATA == pHeader->m_command &&
           TOOLS_SIZEOF(Sahara::ReadData) == pHeader->m_length)
   {
      // On hello stage get read_data command
      pSahara->setMode(Sahara::Mode::MODE_IMAGE_TX_PENDING);
      pSahara->setSaharaVersion(Sahara::SUPPORTED_LOWEST_VERSION);
   }
   else if(Sahara::CommandId::SAHARA_64_BIT_READ_DATA == pHeader->m_command &&
           TOOLS_SIZEOF(Sahara::ReadData64Bit) == pHeader->m_length)
   {
      // On hello stage get read_data_64 command
      pSahara->setMode(Sahara::Mode::MODE_IMAGE_TX_PENDING);
      pSahara->setSaharaVersion(Sahara::SUPPORTED_LOWEST_VERSION);
   }

   if(Sahara::CommandId::SAHARA_READ_DATA == pHeader->m_command && TOOLS_SIZEOF(Sahara::ReadData) == pHeader->m_length)
   {
      const Sahara::ReadData* pReadData = Util::buffer_cast<const Sahara::ReadData*>(pBuffer->begin(), pBuffer->size());
      TOOLS_ASSERT_OR_THROW(
         Sahara::SAHARA_READ_DATA == pReadData->m_header.m_command &&
            TOOLS_SIZEOF(Sahara::ReadData) == pReadData->m_header.m_length && 0 == pReadData->m_dataOffset,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Read data not available from Sahara protocol: " + pSahara->getDescription()
         )
      );
      imageId = pReadData->m_imageId;
      headerSize = pReadData->m_dataLength;
   }
   else if(Sahara::CommandId::SAHARA_64_BIT_READ_DATA == pHeader->m_command &&
           TOOLS_SIZEOF(Sahara::ReadData64Bit) == pHeader->m_length)
   {
      const Sahara::ReadData64Bit* pReadData64Bit =
         Util::buffer_cast<const Sahara::ReadData64Bit*>(pBuffer->begin(), pBuffer->size());
      TOOLS_ASSERT_OR_THROW(
         Sahara::SAHARA_64_BIT_READ_DATA == pReadData64Bit->m_header.m_command &&
            TOOLS_SIZEOF(Sahara::ReadData64Bit) == pReadData64Bit->m_header.m_length &&
            0 == pReadData64Bit->m_dataOffset,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "64 bit Read data not available "
            "from Sahara protocol: " +
               pSahara->getDescription()
         )
      );
      imageId = pReadData64Bit->m_imageId;
      headerSize = pReadData64Bit->m_dataLength;
   }
   else
   {
      // Will naver comes here
      TOOLS_THROW(Device::Exception(
         Device::Exception::DEVICE_INVALID_PACKET,
         "Read data not received from Sahara protocol: " + pSahara->getDescription()
      ));
   }

   return pSahara->getMode();
}

// ----------------------------------------------------------------------------
// saharaTransferSingleImage
//
/// Sends a single image to device in Sahara image transfer mode
// ----------------------------------------------------------------------------
void ImageTransfer::saharaTransferSingleImage(const std::filesystem::path& imageFile, uint64_t headerSize, bool& bMore)
{
   SaharaPtr pSahara = (m_pSaharaConnection->getProtocol()).dynamicCast<Sahara>();

   notify(std::make_shared<ImageTransferEvent>(ImageTransferEvent::SAHARA_TRANSFER_IMAGE, "Sahara transfer image"));

   // Transfer image header
   saharaTransferImageRegion(imageFile, 0, headerSize);

   bool bImageEnded = false;
   while(!bImageEnded)
   {
      // Get a response from the device
      auto rxStartTime = std::chrono::system_clock::now();
      Device::SharedByteBufferPtr pBuffer = pSahara->getFrame();
      auto rxTimeSpan = std::chrono::system_clock::now() - rxStartTime;
      TOOLS_ASSERT_OR_THROW(
         pBuffer != nullptr,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "No packet received from device waiting for data transfer command. "
            "wait time(" +
               std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(rxTimeSpan).count()) +
               "ms): " + pSahara->getDescription()
         )
      );
      const Sahara::FrameHeader* pHeader =
         Util::buffer_cast<const Sahara::FrameHeader*>(pBuffer->begin(), pBuffer->size());

      if(Sahara::CommandId::SAHARA_READ_DATA == pHeader->m_command &&
         TOOLS_SIZEOF(Sahara::ReadData) == pHeader->m_length)
      {
         const Sahara::ReadData* pReadData =
            Util::buffer_cast<const Sahara::ReadData*>(pBuffer->begin(), pBuffer->size());
         TOOLS_ASSERT_OR_THROW(
            Sahara::SAHARA_READ_DATA == pReadData->m_header.m_command &&
               TOOLS_SIZEOF(Sahara::ReadData) == pReadData->m_header.m_length,
            Device::Exception(
               Device::Exception::DEVICE_INVALID_PACKET,
               "Read data not available from Sahara protocol: " + pSahara->getDescription()
            )
         );

         // Transfer an image segment
         saharaTransferImageRegion(imageFile, pReadData->m_dataOffset, pReadData->m_dataLength);
      }
      else if(Sahara::CommandId::SAHARA_64_BIT_READ_DATA == pHeader->m_command &&
              TOOLS_SIZEOF(Sahara::ReadData64Bit) == pHeader->m_length)
      {
         const Sahara::ReadData64Bit* pReadData64Bit =
            Util::buffer_cast<const Sahara::ReadData64Bit*>(pBuffer->begin(), pBuffer->size());
         TOOLS_ASSERT_OR_THROW(
            Sahara::SAHARA_64_BIT_READ_DATA == pReadData64Bit->m_header.m_command &&
               TOOLS_SIZEOF(Sahara::ReadData64Bit) == pReadData64Bit->m_header.m_length,
            Device::Exception(
               Device::Exception::DEVICE_INVALID_PACKET,
               "64 bit Read data not available from Sahara "
               "protocol: " +
                  pSahara->getDescription()
            )
         );

         // Transfer an image segment
         saharaTransferImageRegion(imageFile, pReadData64Bit->m_dataOffset, pReadData64Bit->m_dataLength);
      }
      else if(Sahara::CommandId::SAHARA_END_OF_IMAGE_TRANS == pHeader->m_command &&
              TOOLS_SIZEOF(Sahara::EndOfImageTransfer) == pHeader->m_length)
      {
         const Sahara::EndOfImageTransfer* pEndOfImageTransfer =
            Util::buffer_cast<const Sahara::EndOfImageTransfer*>(pBuffer->begin(), pBuffer->size());
         if(Sahara::Status::STATUS_SUCCESS != pEndOfImageTransfer->m_status)
         {
            saharaReset();
            Device::Exception ex(Device::Exception::DEVICE_INVALID_PACKET);
            TOOLS_THROW(Device::Exception(
               Device::Exception::DEVICE_INVALID_PACKET,
               Device::Exception::getErrorJson(
                  ERR_SAHARA_PROTOCOL_RESET(
                     std::string(imageFile.filename().string().c_str()),
                     ex.getErrorCodeString(Device::Exception::DEVICE_INVALID_PACKET)
                  ),
                  SUGG_SAHARA_PROTOCOL_RESET,
                  std::string(POC(TARGET)) + " or " + std::string(POC(BOOT))
               )
            ));
         }
         else
         {
            bImageEnded = true;
         }
      }
      else if(Sahara::CommandId::SAHARA_HELLO == pHeader->m_command && TOOLS_SIZEOF(Sahara::Hello) == pHeader->m_length)
      {
         TOOLS_THROW(Device::Exception(
            Device::Exception::DEVICE_INVALID_PARAMETERS,
            "Image " + std::string(imageFile.string().c_str()) + " rejected by device: " + pSahara->getDescription()
         ));
      }
      else
      {
         TOOLS_THROW(Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Read data not received from Sahara "
            "protocol: " +
               pSahara->getDescription()
         ));
      }
   }

   // Send done
   Device::SharedByteBufferPtr pDoneBuffer = pSahara->createCommand<Sahara::Done>(Sahara::SAHARA_DONE);
   m_pSaharaConnection->sendSync(pDoneBuffer);

   // Get done response from the device
   Device::SharedByteBufferPtr pDoneResponseBuffer = pSahara->getFrame();
   TOOLS_ASSERT_OR_THROW(
      pDoneResponseBuffer != nullptr,
      Device::Exception(
         Device::Exception::DEVICE_INVALID_PACKET,
         "No packet received from device waiting "
         "for done response:" +
            pSahara->getDescription()
      )
   );
   const Sahara::DoneResponse* pDoneResponse =
      Util::buffer_cast<const Sahara::DoneResponse*>(pDoneResponseBuffer->begin(), pDoneResponseBuffer->size());
   TOOLS_ASSERT_OR_THROW(
      Sahara::CommandId::SAHARA_DONE_RESP == pDoneResponse->m_header.m_command &&
         TOOLS_SIZEOF(Sahara::DoneResponse) == pDoneResponse->m_header.m_length,
      Device::Exception(
         Device::Exception::DEVICE_INVALID_PACKET,
         "Image transfer done response not "
         "received from Sahara protocol: " +
            pSahara->getDescription()
      )
   );

   switch(pDoneResponse->m_imageTransferStatus)
   {
      case Sahara::MODE_IMAGE_TX_COMPLETE:
         bMore = false;
         break;

      case Sahara::MODE_IMAGE_TX_PENDING:
         bMore = true;
         break;

      default:
         TOOLS_THROW(Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Invalid image transfer status from "
            "Sahara protocol: " +
               pSahara->getDescription()
         ));
   }
}

// ----------------------------------------------------------------------------
// saharaTransferImageRegion
//
/// Transfer a chunk in Sahara image transfer mode
// ----------------------------------------------------------------------------
void ImageTransfer::
   saharaTransferImageRegion(const std::filesystem::path& imageFilePath, uint64_t offset, uint64_t length)
{
   SaharaPtr pSahara = (m_pSaharaConnection->getProtocol()).dynamicCast<Sahara>();

   auto imageFile = std::make_shared<std::fstream>();
   std::filesystem::path localPath =

      Device::Manager::getInstance()
         ->getAccessiblePath(imageFilePath, std::filesystem::path(), false, Device::Protocol::Base::NO_CLIENT_ID, true);
   imageFile->exceptions(std::ios::failbit | std::ios::badbit);
   imageFile->open(localPath.string().c_str(), std::ios::in | std::ios::binary);

   imageFile->seekg(offset, std::ios::beg);

   uint64_t bytesWrite = 0;
   while(bytesWrite < length)
   {
      uint64_t bytesToWrite = std::min<uint64_t>(length - bytesWrite, MAX_SAHARA_WRITE);
      Device::SharedByteBufferPtr pBuffer = Device::Buffer::createBuffer(static_cast<size_t>(bytesToWrite));

      imageFile->read(reinterpret_cast<char*>(pBuffer->begin()), pBuffer->size());
      TOOLS_ASSERT_OR_THROW(
         pBuffer != nullptr,
         Device::Exception(
            Device::Exception::DEVICE_INVALID_PACKET,
            "Image transfer fail to read file: " + std::string(localPath.string().c_str()) + " offset " +
               std::to_string(offset) + " length " + std::to_string(length) + pSahara->getDescription()
         )
      );
      m_pSaharaConnection->sendSync(pBuffer);
      bytesWrite += bytesToWrite;
   }

   imageFile->close();
}

// ----------------------------------------------------------------------------
// saharaReset
//
/// Resets the device out of Sahara mode
// ----------------------------------------------------------------------------
void ImageTransfer::saharaReset()
{
   SaharaPtr pSahara = (m_pSaharaConnection->getProtocol()).dynamicCast<Sahara>();

   // Send the reset command
   Device::SharedByteBufferPtr pResetBuffer = pSahara->createCommand<Sahara::Reset>(Sahara::SAHARA_RESET);

   m_pSaharaConnection->sendSync(pResetBuffer);

   notify(std::make_shared<
          ImageTransferEvent>(ImageTransferEvent::SAHARA_RESETTING, "Resetting the device out of Sahara"));

   // Get the reset response but do not expect it will arrive
   pSahara->getFrame(std::chrono::seconds(1));
}

} // namespace Function
