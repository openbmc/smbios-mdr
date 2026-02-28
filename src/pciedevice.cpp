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

#include "pciedevice.hpp"

#include <cstdint>
#include <limits>

namespace phosphor
{
namespace smbios
{

// Same layout as Pcie::SystemSlotInfo in pcieslot.hpp (SMBIOS Type 9)
struct SystemSlotInfo
{
    uint8_t type;
    uint8_t length;
    uint16_t handle;
    uint8_t slotDesignation;
    uint8_t slotType;
    uint8_t slotDataBusWidth;
    uint8_t currUsage;
    uint8_t slotLength;
    uint16_t slotID;
    uint8_t characteristics1;
    uint8_t characteristics2;
    uint16_t segGroupNum;
    uint8_t busNum;
    uint8_t deviceNum;
} __attribute__((packed));

void PcieDevice::pcieDeviceInfoUpdate(uint8_t* smbiosTableStorage,
                                      const std::string& motherboard,
                                      const std::string& slotPath)
{
    storage = smbiosTableStorage;
    motherboardPath = motherboard;

    uint8_t* dataIn = getSMBIOSTypePtr(storage, systemSlots);

    if (dataIn == nullptr)
    {
        return;
    }

    for (uint8_t index = 0;
         index < deviceNum ||
         pcieSmbiosType.find(*(dataIn + 5)) == pcieSmbiosType.end();)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return;
        }
        dataIn = getSMBIOSTypePtr(dataIn, systemSlots);
        if (dataIn == nullptr)
        {
            return;
        }
        if (pcieSmbiosType.find(*(dataIn + 5)) != pcieSmbiosType.end())
        {
            index++;
        }
    }

    auto slotInfo = reinterpret_cast<struct SystemSlotInfo*>(dataIn);

    auto genIt = pcieGenerationTable.find(slotInfo->slotType);
    PCIeGeneration gen = (genIt != pcieGenerationTable.end())
                             ? genIt->second
                             : PCIeGeneration::Unknown;
    PCIeDeviceIface::generationInUse(gen);
    PCIeDeviceIface::generationSupported(gen);

    auto lanesIt = pcieLanesTable.find(slotInfo->slotDataBusWidth);
    size_t lanes = (lanesIt != pcieLanesTable.end()) ? lanesIt->second : 0;
    PCIeDeviceIface::maxLanes(lanes);

    // Not available from SMBIOS Type 9; max() causes bmcweb to omit the field
    PCIeDeviceIface::lanesInUse(std::numeric_limits<size_t>::max());

    PCIeDeviceIface::deviceType(PCIeDeviceType::Unknown);

#ifdef SLOT_DRIVE_PRESENCE
    PCIeDeviceItem::present(
        slotInfo->currUsage == static_cast<uint8_t>(Availability::InUse));
#else
    PCIeDeviceItem::present(true);
#endif

    if (!slotPath.empty())
    {
        std::vector<std::tuple<std::string, std::string, std::string>> assocs;
        assocs.emplace_back("contained_by", "containing", slotPath);
        PCIeDeviceAssoc::associations(assocs);
    }
}

} // namespace smbios
} // namespace phosphor
