#pragma once

#include "handler.hpp"

#include <ipmid/api.h>
#include <systemd/sd-bus.h>

#include <gtest/gtest.h>

sd_bus* ipmid_get_sd_bus_connection()
{
    return nullptr;
}

namespace blobs
{

class SmbiosBlobHandlerTest : public ::testing::Test
{
  protected:
    SmbiosBlobHandlerTest() = default;

    SmbiosBlobHandler handler;

    const uint16_t session = 0;
    const std::string expectedBlobId = "/smbios";
    const std::vector<std::string> expectedBlobIdList = {"/smbios"};
    const uint32_t handlerMaxBufferSize = 64 * 1024;
};
} // namespace blobs
