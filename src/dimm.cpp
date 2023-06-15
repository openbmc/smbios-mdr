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

#ifdef DIMM_ONLY_LOCATOR
bool onlyDimmLocationCode = true;
#else
bool onlyDimmLocationCode = false;
#endif

using DeviceType =
    sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::DeviceType;

using EccType =
    sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::Ecc;

static constexpr uint16_t maxOldDimmSize = 0x7fff;
void Dimm::memoryInfoUpdate(uint8_t* smbiosTableStorage,
                            const std::string& motherboard)
{
    storage = smbiosTableStorage;
    motherboardPath = motherboard;

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
    // If the size is 0, no memory device is installed in the socket.
    const auto isDimmPresent = memoryInfo->size > 0;
    present(isDimmPresent);
    functional(isDimmPresent);

    dimmDeviceLocator(memoryInfo->bankLocator, memoryInfo->deviceLocator,
                      memoryInfo->length, dataIn);
    dimmType(memoryInfo->memoryType);
    dimmTypeDetail(memoryInfo->typeDetail);
    maxMemorySpeedInMhz(memoryInfo->speed);
    dimmManufacturer(memoryInfo->manufacturer, memoryInfo->length, dataIn);
    dimmSerialNum(memoryInfo->serialNum, memoryInfo->length, dataIn);
    dimmPartNum(memoryInfo->partNum, memoryInfo->length, dataIn);
    memoryAttributes(memoryInfo->attributes);
    dimmMedia(memoryInfo->memoryTechnology);
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
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::ecc(
        value);
}

uint16_t Dimm::memoryDataWidth(uint16_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memoryDataWidth(value);
}

static constexpr uint16_t baseNewVersionDimmSize = 0x8000;
static constexpr uint16_t dimmSizeUnit = 1024;
void Dimm::dimmSize(const uint16_t size)
{
    uint32_t result = size & maxOldDimmSize;
    if (0 == (size & baseNewVersionDimmSize))
    {
        result = result * dimmSizeUnit;
    }
    memorySizeInKB(result);
}

void Dimm::dimmSizeExt(uint32_t size)
{
    size = size * dimmSizeUnit;
    memorySizeInKB(size);
}

size_t Dimm::memorySizeInKB(size_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memorySizeInKB(value);
}

void Dimm::dimmDeviceLocator(const uint8_t bankLocatorPositionNum,
                             const uint8_t deviceLocatorPositionNum,
                             const uint8_t structLen, uint8_t* dataIn)
{
    std::string deviceLocator = positionToString(deviceLocatorPositionNum,
                                                 structLen, dataIn);
    std::string bankLocator = positionToString(bankLocatorPositionNum,
                                               structLen, dataIn);

    std::string result;
    if (bankLocator.empty() || onlyDimmLocationCode)
    {
        result = deviceLocator;
    }
    else
    {
        result = bankLocator + " " + deviceLocator;
    }

    memoryDeviceLocator(result);

    locationCode(result);
    std::string substrCpu = "CPU";
    std::string substrDimm = "DIMM";
    uint8_t socketNum;
    const uint8_t numDelimiters = 2; /* there will be 2 delimiters like '_' or '
                              ' for ex: CPU0_DIMM_A or CPU0 DIMM_A */
    std::string cpuString = deviceLocator.substr(deviceLocator.find(substrCpu),
                                                 substrCpu.length() + 1);
    std::string slotString = deviceLocator.substr(
        deviceLocator.find(substrDimm) + substrDimm.length() + 1,
        (deviceLocator.length() - cpuString.length() - substrDimm.length() -
         numDelimiters));
    std::string socketString =
        cpuString.substr(cpuString.find(substrCpu) + substrCpu.length(), 1);
    std::vector<uint8_t> slotVector(slotString.begin(), slotString.end());
    uint8_t* slotPtr = &slotVector[0];
    try
    {
        socketNum = static_cast<uint8_t>(std::stoi(socketString) + 1);
        socket(socketNum);
    }
    catch (const sdbusplus::exception_t& ex)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "std::stoi operation failed ",
            phosphor::logging::entry("ERROR=%s", ex.what()));
    }

    slot(*slotPtr);
}

std::string Dimm::memoryDeviceLocator(std::string value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
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
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memoryType(value);
}

void Dimm::dimmMedia(const uint8_t type)
{
    std::map<uint8_t, MemoryTechType>::const_iterator it =
        dimmMemoryTechTypeMap.find(type);
    if (it == dimmMemoryTechTypeMap.end())
    {
        memoryMedia(MemoryTechType::Unknown);
    }
    else
    {
        memoryMedia(it->second);
    }
}

MemoryTechType Dimm::memoryMedia(MemoryTechType value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memoryMedia(value);
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
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memoryTypeDetail(value);
}

uint16_t Dimm::maxMemorySpeedInMhz(uint16_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        maxMemorySpeedInMhz(value);
}

void Dimm::dimmManufacturer(const uint8_t positionNum, const uint8_t structLen,
                            uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    if (result == "NO DIMM")
    {
        // No dimm presence so making manufacturer value as "" (instead of
        // NO DIMM - as there won't be any manufacturer for DIMM which is not
        // present).
        result = "";
    }
    manufacturer(result);
}

std::string Dimm::manufacturer(std::string value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::decorator::
        Asset::manufacturer(value);
}

bool Dimm::present(bool value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::Item::present(
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
    return sdbusplus::server::xyz::openbmc_project::inventory::decorator::
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
    return sdbusplus::server::xyz::openbmc_project::inventory::decorator::
        Asset::partNumber(value);
}

std::string Dimm::locationCode(std::string value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::decorator::
        LocationCode::locationCode(value);
}

size_t Dimm::memoryAttributes(size_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memoryAttributes(value);
}

uint8_t Dimm::slot(uint8_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::dimm::
        MemoryLocation::slot(value);
}

uint8_t Dimm::socket(uint8_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::dimm::
        MemoryLocation::socket(value);
}

uint16_t Dimm::memoryConfiguredSpeedInMhz(uint16_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::Dimm::
        memoryConfiguredSpeedInMhz(value);
}

bool Dimm::functional(bool value)
{
    return sdbusplus::server::xyz::openbmc_project::state::decorator::
        OperationalStatus::functional(value);
}

} // namespace smbios
} // namespace phosphor
