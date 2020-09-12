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
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>

namespace phosphor
{
namespace cpu_info
{
static constexpr char const* cpuInfoObject = "xyz.openbmc_project.CPUInfo";
static constexpr char const* cpuInfoPath = "/xyz/openbmc_project/CPUInfo";
static constexpr char const* cpuInfoInterface = "xyz.openbmc_project.CPUInfo";

static constexpr const int configCheckInterval = 10;
static constexpr const int peciCheckInterval = 60;

/** \ todo add cpu interface to CPUInfo and consolidate with smbios service
 * using processor =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cpu;
*/
using asset =
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Asset;

struct CPUInfo : sdbusplus::server::object_t<asset>
{
  public:
    CPUInfo() = delete;
    CPUInfo(const CPUInfo&) = delete;
    CPUInfo& operator=(const CPUInfo&) = delete;
    CPUInfo(CPUInfo&&) = delete;
    CPUInfo& operator=(CPUInfo&&) = delete;
    ~CPUInfo() = default;

    CPUInfo(sdbusplus::bus::bus& bus, const std::string& path,
            const size_t& cpuId, const uint8_t& peciAddress,
            const uint8_t& i2cBusNum, const uint8_t& i2cSlaveAddress) :
        sdbusplus::server::object_t<asset>(bus, path.c_str()),
        id(cpuId), peciAddr(peciAddress), i2cBus(i2cBusNum),
        i2cDevice(i2cSlaveAddress)
    {}

    uint8_t id;
    uint8_t peciAddr;
    uint8_t i2cBus;
    uint8_t i2cDevice;

  private:
};

} // namespace cpu_info
} // namespace phosphor
