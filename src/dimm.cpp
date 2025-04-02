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
#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <iostream>
#include <regex>

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

static constexpr const char* filename =
    "/usr/share/smbios-mdr/memoryLocationTable.json";

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
    memoryTotalWidth(memoryInfo->totalWidth);

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
            lg2::error("Failed to get SMBIOS table type-16 data.");
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
    lg2::error(
        "Failed find the corresponding SMBIOS table type-16 data for dimm: {DIMM}",
        "DIMM", dimmNum);
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

uint16_t Dimm::memoryTotalWidth(uint16_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Dimm::
        memoryTotalWidth(value);
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
    std::string deviceLocator =
        positionToString(deviceLocatorPositionNum, structLen, dataIn);
    std::string bankLocator =
        positionToString(bankLocatorPositionNum, structLen, dataIn);

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
    const std::string substrCpu = "CPU";
    auto cpuPos = deviceLocator.find(substrCpu);

    auto data = parseConfigFile();

    if (!data.empty())
    {
        auto it = data.find(deviceLocator);

        if (it != data.end())
        {
            uint8_t memoryControllerValue =
                it.value()["MemoryController"].get<uint8_t>();
            uint8_t socketValue = it.value()["Socket"].get<uint8_t>();
            uint8_t slotValue = it.value()["Slot"].get<uint8_t>();
            uint8_t channelValue = it.value()["Channel"].get<uint8_t>();

            socket(socketValue);
            memoryController(memoryControllerValue);
            slot(slotValue);
            channel(channelValue);
        }
        else
        {
            socket(0);
            memoryController(0);
            slot(0);
            channel(0);
            lg2::error("Failed find the corresponding table for dimm {DIMM}",
                       "DIMM", deviceLocator.c_str());
        }
    }
    else
    {
        if (cpuPos != std::string::npos)
        {
            std::string socketString =
                deviceLocator.substr(cpuPos + substrCpu.length(), 1);
            try
            {
                uint8_t socketNum =
                    static_cast<uint8_t>(std::stoi(socketString) + 1);
                socket(socketNum);
            }
            catch (const sdbusplus::exception_t& ex)
            {
                lg2::error("std::stoi operation failed {ERROR}", "ERROR",
                           ex.what());
            }
        }
    }

    const std::string substrDimm = "DIMM";
    auto dimmPos = deviceLocator.find(substrDimm);

    if (dimmPos != std::string::npos)
    {
        std::string slotString =
            deviceLocator.substr(dimmPos + substrDimm.length() + 1);
        /* slotString is extracted from substrDimm (DIMM_A) if slotString is
         * single alphabet like A, B , C.. then assign ASCII value of slotString
         * to slot */
        if ((std::regex_match(slotString, std::regex("^[A-Za-z]+$"))) &&
            (slotString.length() == 1))
        {
            slot(static_cast<uint8_t>(toupper(slotString[0])));
        }
    }
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

uint8_t Dimm::memoryController(uint8_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::dimm::
        MemoryLocation::memoryController(value);
}

uint8_t Dimm::channel(uint8_t value)
{
    return sdbusplus::server::xyz::openbmc_project::inventory::item::dimm::
        MemoryLocation::channel(value);
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

Json Dimm::parseConfigFile()
{
    std::ifstream memoryLocationFile(filename);

    if (!memoryLocationFile.is_open())
    {
        lg2::error("config JSON file not found, FILENAME {FILENAME}",
                   "FILENAME", filename);
        return {};
    }

    auto data = Json::parse(memoryLocationFile, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("config readings JSON parser failure");
        return {};
    }

    return data;
}

} // namespace smbios
} // namespace phosphor
