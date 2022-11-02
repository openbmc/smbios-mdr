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

#include "dimm.hpp"

#include "mdrv2.hpp"

#include <boost/algorithm/string.hpp>
#include <phosphor-logging/elog-errors.hpp>

namespace phosphor
{
namespace smbios
{

using DeviceType =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::DeviceType;

using EccType =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::Ecc;

static constexpr uint16_t maxOldDimmSize = 0x7fff;
void Dimm::memoryInfoUpdate(void)
{
    uint8_t* dataIn = storage;

    dataIn = getSMBIOSTypePtr(dataIn, memoryDeviceType);

    if (dataIn == nullptr)
    {
        return;
    }
    for (uint8_t index = 0; index < dimmNum; index++)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return;
        }
        dataIn = getSMBIOSTypePtr(dataIn, memoryDeviceType);
        if (dataIn == nullptr)
        {
            return;
        }
    }

    auto memoryInfo = reinterpret_cast<struct MemoryInfo*>(dataIn);

    memoryDataWidth(memoryInfo->dataWidth);

    if (memoryInfo->size == maxOldDimmSize)
    {
        dimmSizeExt(memoryInfo->extendedSize);
    }
    else
    {
        dimmSize(memoryInfo->size);
    }

    dimmDeviceLocator(memoryInfo->bankLocator, memoryInfo->deviceLocator,
                      memoryInfo->length, dataIn);
    dimmType(memoryInfo->memoryType);
    dimmTypeDetail(memoryInfo->typeDetail);
    maxMemorySpeedInMhz(memoryInfo->speed);
    dimmManufacturer(memoryInfo->manufacturer, memoryInfo->length, dataIn);
    dimmSerialNum(memoryInfo->serialNum, memoryInfo->length, dataIn);
    dimmPartNum(memoryInfo->partNum, memoryInfo->length, dataIn);
    memoryAttributes(memoryInfo->attributes);
    memoryConfiguredSpeedInMhz(memoryInfo->confClockSpeed);

    updateEccType(memoryInfo->phyArrayHandle);

    if (!motherboardPath.empty())
    {
        std::vector<std::tuple<std::string, std::string, std::string>> assocs;
        assocs.emplace_back("chassis", "memories", motherboardPath);
        association::associations(assocs);
    }

    return;
}

void Dimm::updateEccType(uint16_t exPhyArrayHandle)
{
    uint8_t* dataIn = storage;

    while (dataIn != nullptr)
    {
        dataIn = getSMBIOSTypePtr(dataIn, physicalMemoryArrayType);
        if (dataIn == nullptr)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Failed to get SMBIOS table type-16 data.");
            return;
        }

        auto info = reinterpret_cast<struct PhysicalMemoryArrayInfo*>(dataIn);
        if (info->handle == exPhyArrayHandle)
        {
            std::map<uint8_t, EccType>::const_iterator it =
                dimmEccTypeMap.find(info->memoryErrorCorrection);
            if (it == dimmEccTypeMap.end())
            {
                ecc(EccType::NoECC);
            }
            else
            {
                ecc(it->second);
            }
            return;
        }

        dataIn = smbiosNextPtr(dataIn);
    }
    phosphor::logging::log<phosphor::logging::level::ERR>(
        "Failed find the corresponding SMBIOS table type-16 data for dimm:",
        phosphor::logging::entry("DIMM:%d", dimmNum));
}

EccType Dimm::ecc(EccType value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::ecc(
        value);
}

uint16_t Dimm::memoryDataWidth(uint16_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryDataWidth(value);
}

static constexpr uint16_t baseNewVersionDimmSize = 0x8000;
static constexpr uint16_t dimmSizeUnit = 1024;
void Dimm::dimmSize(const uint16_t size)
{
    size_t result = size & maxOldDimmSize;
    if (0 == (size & baseNewVersionDimmSize))
    {
        result = result * dimmSizeUnit;
    }
    memorySizeInKB(result);
}

void Dimm::dimmSizeExt(size_t size)
{
    size = size * dimmSizeUnit;
    memorySizeInKB(size);
}

size_t Dimm::memorySizeInKB(size_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memorySizeInKB(value);
}

void Dimm::dimmDeviceLocator(const uint8_t bankLocatorPositionNum,
                             const uint8_t deviceLocatorPositionNum,
                             const uint8_t structLen, uint8_t* dataIn)
{
    std::string deviceLocator =
        positionToString(deviceLocatorPositionNum, structLen, dataIn);
    std::string bankLocator =
        positionToString(bankLocatorPositionNum, structLen, dataIn);

    std::string result;
    if (!bankLocator.empty())
    {
        result = bankLocator + " " + deviceLocator;
    }
    else
    {
        result = deviceLocator;
    }

    memoryDeviceLocator(result);

    locationCode(result);
}

std::string Dimm::memoryDeviceLocator(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryDeviceLocator(value);
}

void Dimm::dimmType(const uint8_t type)
{
    std::map<uint8_t, DeviceType>::const_iterator it = dimmTypeTable.find(type);
    if (it == dimmTypeTable.end())
    {
        memoryType(DeviceType::Unknown);
    }
    else
    {
        memoryType(it->second);
    }
}

DeviceType Dimm::memoryType(DeviceType value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryType(value);
}

void Dimm::dimmTypeDetail(uint16_t detail)
{
    std::string result;
    for (uint8_t index = 0; index < (8 * sizeof(detail)); index++)
    {
        if (detail & 0x01)
        {
            result += detailTable[index];
        }
        detail >>= 1;
    }
    memoryTypeDetail(result);
}

std::string Dimm::memoryTypeDetail(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryTypeDetail(value);
}

uint16_t Dimm::maxMemorySpeedInMhz(uint16_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        maxMemorySpeedInMhz(value);
}

void Dimm::dimmManufacturer(const uint8_t positionNum, const uint8_t structLen,
                            uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    bool val = true;
    if (result == "NO DIMM")
    {
        val = false;

        // No dimm presence so making manufacturer value as "" (instead of
        // NO DIMM - as there won't be any manufacturer for DIMM which is not
        // present).
        result = "";
    }
    manufacturer(result);
    present(val);
    functional(val);
}

std::string Dimm::manufacturer(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::
        Asset::manufacturer(value);
}

bool Dimm::present(bool value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::server::Item::present(
        value);
}

void Dimm::dimmSerialNum(const uint8_t positionNum, const uint8_t structLen,
                         uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    serialNumber(result);
}

std::string Dimm::serialNumber(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::
        Asset::serialNumber(value);
}

void Dimm::dimmPartNum(const uint8_t positionNum, const uint8_t structLen,
                       uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    // Part number could contain spaces at the end. Eg: "abcd123  ". Since its
    // unnecessary, we should remove them.
    boost::algorithm::trim_right(result);
    partNumber(result);
}

std::string Dimm::partNumber(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::
        Asset::partNumber(value);
}

std::string Dimm::locationCode(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::
        LocationCode::locationCode(value);
}

uint8_t Dimm::memoryAttributes(uint8_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryAttributes(value);
}

uint16_t Dimm::memoryConfiguredSpeedInMhz(uint16_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryConfiguredSpeedInMhz(value);
}

bool Dimm::functional(bool value)
{
    return sdbusplus::xyz::openbmc_project::State::Decorator::server::
        OperationalStatus::functional(value);
}

} // namespace smbios
} // namespace phosphor
