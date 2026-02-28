/*
// Copyright (c) 2025 9elements GmbH
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
#include "pcieslot.hpp"
#include "smbios_mdrv2.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/PCIeDevice/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>

#include <cstdint>

namespace phosphor
{

namespace smbios
{

using PCIeDeviceIface =
    sdbusplus::server::xyz::openbmc_project::inventory::item::PCIeDevice;
using PCIeDeviceType = sdbusplus::server::xyz::openbmc_project::inventory::
    item::PCIeDevice::DeviceTypes;
using PCIeDeviceItem =
    sdbusplus::server::xyz::openbmc_project::inventory::Item;
using PCIeDeviceAssoc =
    sdbusplus::server::xyz::openbmc_project::association::Definitions;

class PcieDevice :
    sdbusplus::server::object_t<PCIeDeviceIface, PCIeDeviceItem,
                                PCIeDeviceAssoc>
{
  public:
    PcieDevice() = delete;
    PcieDevice(const PcieDevice&) = delete;
    PcieDevice& operator=(const PcieDevice&) = delete;
    PcieDevice(PcieDevice&&) = delete;
    PcieDevice& operator=(PcieDevice&&) = delete;
    ~PcieDevice() = default;

    PcieDevice(sdbusplus::bus_t& bus, const std::string& objPath,
               const uint8_t& deviceId, uint8_t* smbiosTableStorage,
               const std::string& motherboard,
               const std::string& slotPath) :
        sdbusplus::server::object_t<PCIeDeviceIface, PCIeDeviceItem,
                                    PCIeDeviceAssoc>(bus, objPath.c_str()),
        deviceNum(deviceId)
    {
        pcieDeviceInfoUpdate(smbiosTableStorage, motherboard, slotPath);
    }

    void pcieDeviceInfoUpdate(uint8_t* smbiosTableStorage,
                              const std::string& motherboard,
                              const std::string& slotPath);

  private:
    uint8_t deviceNum;
    uint8_t* storage = nullptr;
    std::string motherboardPath;
};

} // namespace smbios

} // namespace phosphor
