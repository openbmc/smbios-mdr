/*
// Copyright (c) 2020 intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once

#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/UniqueIdentifier/server.hpp>

namespace cpu_info
{
static constexpr const char* cpuInfoObject = "xyz.openbmc_project.CPUInfo";
static constexpr const char* cpuInfoPath = "/xyz/openbmc_project/CPUInfo";
static constexpr const char* cpuInfoInterface = "xyz.openbmc_project.CPUInfo";
static constexpr const char* cpuPath =
    "/xyz/openbmc_project/inventory/system/chassis/motherboard/cpu";

static constexpr const int configCheckInterval = 10;
static constexpr const int peciCheckInterval = 60;

using UniqueIdentifier =
    sdbusplus::server::object_t<sdbusplus::server::xyz::openbmc_project::
                                    inventory::decorator::UniqueIdentifier>;

struct CPUInfo
{
    CPUInfo(const size_t cpuId, const uint8_t peciAddress,
            const uint8_t i2cBusNum, const uint8_t i2cSlaveAddress) :
        id(cpuId), peciAddr(peciAddress), i2cBus(i2cBusNum),
        i2cDevice(i2cSlaveAddress)
    {}

    void publishUUID(sdbusplus::bus_t& bus, const std::string& uuid)
    {
        uuidInterface.emplace(bus, (cpuPath + std::to_string(id - 1)).c_str(),
                              UniqueIdentifier::action::defer_emit);
        uuidInterface->uniqueIdentifier(uuid);
        uuidInterface->emit_added();
    }

    std::optional<UniqueIdentifier> uuidInterface;

    uint8_t id;
    uint8_t peciAddr;
    uint8_t i2cBus;
    uint8_t i2cDevice;
    std::string sSpec;
};

} // namespace cpu_info
