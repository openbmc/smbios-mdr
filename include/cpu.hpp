/*
// Copyright (c) 2018 Intel Corporation
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
#include "smbios_mdrv2.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Connector/Slot/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Revision/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cpu/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

namespace phosphor
{

namespace smbios
{

using rev =
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Revision;
using asset =
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Asset;
using location =
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::LocationCode;
using connector =
    sdbusplus::xyz::openbmc_project::Inventory::Connector::server::Slot;
using processor = sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cpu;
using Item = sdbusplus::xyz::openbmc_project::Inventory::server::Item;
using association =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;
using opStatus = sdbusplus::xyz::openbmc_project::State::Decorator::server::
    OperationalStatus;

// Definition follow smbios spec DSP0134 3.0.0
static const std::map<uint8_t, const char*> familyTable = {
    {0x1, "Other"},
    {0x2, "Unknown"},
    {0x10, "Pentium II Xeon processor"},
    {0xa1, "Quad-Core Intel Xeon processor 3200 Series"},
    {0xa2, "Dual-Core Intel Xeon processor 3000 Series"},
    {0xa3, "Quad-Core Intel Xeon processor 5300 Series"},
    {0xa4, "Dual-Core Intel Xeon processor 5100 Series"},
    {0xa5, "Dual-Core Intel Xeon processor 5000 Series"},
    {0xa6, "Dual-Core Intel Xeon processor LV"},
    {0xa7, "Dual-Core Intel Xeon processor ULV"},
    {0xa8, "Dual-Core Intel Xeon processor 7100 Series"},
    {0xa9, "Quad-Core Intel Xeon processor 5400 Series"},
    {0xaa, "Quad-Core Intel Xeon processor"},
    {0xab, "Dual-Core Intel Xeon processor 5200 Series"},
    {0xac, "Dual-Core Intel Xeon processor 7200 Series"},
    {0xad, "Quad-Core Intel Xeon processor 7300 Series"},
    {0xae, "Quad-Core Intel Xeon processor 7400 Series"},
    {0xaf, "Multi-Core Intel Xeon processor 7400 Series"},
    {0xb0, "Pentium III Xeon processor"},
    {0xb3, "Intel Xeon processor"},
    {0xb5, "Intel Xeon processor MP"},
    {0xd6, "Multi-Core Intel Xeon processor"},
    {0xd7, "Dual-Core Intel Xeon processor 3xxx Series"},
    {0xd8, "Quad-Core Intel Xeon processor 3xxx Series"},
    {0xd9, "VIA Nano Processor Family"},
    {0xda, "Dual-Core Intel Xeon processor 5xxx Series"},
    {0xdb, "Quad-Core Intel Xeon processor 5xxx Series"},
    {0xdd, "Dual-Core Intel Xeon processor 7xxx Series"},
    {0xde, "Quad-Core Intel Xeon processor 7xxx Series"},
    {0xdf, "Multi-Core Intel Xeon processor 7xxx Series"},
    {0xe0, "Multi-Core Intel Xeon processor 3400 Series"},
    {0xfe, "Processor Family 2 Indicator"}

};

// Definition follow smbios spec DSP0134 3.1.1
static const std::map<uint16_t, const char*> family2Table = {
    {0x100, "ARMv7"}, {0x101, "ARMv8"}, {0x118, "ARM"}, {0x119, "StrongARM"}

};

// Definition follow smbios spec DSP0134 3.0.0
static const std::array<std::optional<processor::Capability>, 16>
    characteristicsTable{std::nullopt,
                         std::nullopt,
                         processor::Capability::Capable64bit,
                         processor::Capability::MultiCore,
                         processor::Capability::HardwareThread,
                         processor::Capability::ExecuteProtection,
                         processor::Capability::EnhancedVirtualization,
                         processor::Capability::PowerPerformanceControl,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt,
                         std::nullopt};

class Cpu :
    sdbusplus::server::object_t<processor, asset, location, connector, rev,
                                Item, association, opStatus>
{
  public:
    Cpu() = delete;
    Cpu(const Cpu&) = delete;
    Cpu& operator=(const Cpu&) = delete;
    Cpu(Cpu&&) = delete;
    Cpu& operator=(Cpu&&) = delete;
    ~Cpu() = default;

    Cpu(sdbusplus::bus_t& bus, const std::string& objPath, const uint8_t& cpuId,
        uint8_t* smbiosTableStorage, const std::string& motherboard) :
        sdbusplus::server::object_t<processor, asset, location, connector, rev,
                                    Item, association, opStatus>(
            bus, objPath.c_str()),
        cpuNum(cpuId), storage(smbiosTableStorage), motherboardPath(motherboard)
    {
        infoUpdate();
    }

    void infoUpdate(void);

  private:
    uint8_t cpuNum;

    uint8_t* storage;

    std::string motherboardPath;

    struct ProcessorInfo
    {
        uint8_t type;
        uint8_t length;
        uint16_t handle;
        uint8_t socketDesignation;
        uint8_t processorType;
        uint8_t family;
        uint8_t manufacturer;
        uint64_t id;
        uint8_t version;
        uint8_t voltage;
        uint16_t exClock;
        uint16_t maxSpeed;
        uint16_t currSpeed;
        uint8_t status;
        uint8_t upgrade;
        uint16_t l1Handle;
        uint16_t l2Handle;
        uint16_t l3Handle;
        uint8_t serialNum;
        uint8_t assetTag;
        uint8_t partNum;
        uint8_t coreCount;
        uint8_t coreEnable;
        uint8_t threadCount;
        uint16_t characteristics;
        uint16_t family2;
        uint16_t coreCount2;
        uint16_t coreEnable2;
        uint16_t threadCount2;
    } __attribute__((packed));

    void socket(const uint8_t positionNum, const uint8_t structLen,
                uint8_t* dataIn);
    void family(const uint8_t family, const uint16_t family2);
    void manufacturer(const uint8_t positionNum, const uint8_t structLen,
                      uint8_t* dataIn);
    void serialNumber(const uint8_t positionNum, const uint8_t structLen,
                      uint8_t* dataIn);
    void partNumber(const uint8_t positionNum, const uint8_t structLen,
                    uint8_t* dataIn);
    void version(const uint8_t positionNum, const uint8_t structLen,
                 uint8_t* dataIn);
    void characteristics(const uint16_t value);
    void functionalState(const uint8_t coreCount);
};

} // namespace smbios

} // namespace phosphor
