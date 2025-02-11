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

#include "mdrv2.hpp"

#include "pcieslot.hpp"

#include <sys/mman.h>

#include <phosphor-logging/elog-errors.hpp>
#include <sdbusplus/exception.hpp>
#include <xyz/openbmc_project/Smbios/MDR_V2/error.hpp>

#include <fstream>

namespace phosphor
{
namespace smbios
{

std::vector<uint8_t> MDRV2::getDirectoryInformation(uint8_t dirIndex)
{
    std::vector<uint8_t> responseDir;

    std::ifstream smbiosFile(smbiosFilePath, std::ios_base::binary);
    if (!smbiosFile.good())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Read data from flash error - Open MDRV2 table file failure");
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }
    if (dirIndex > smbiosDir.dirEntries)
    {
        responseDir.push_back(0);
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }
    responseDir.push_back(mdr2Version);
    responseDir.push_back(smbiosDir.dirVersion);
    uint8_t returnedEntries = smbiosDir.dirEntries - dirIndex;
    responseDir.push_back(returnedEntries);
    if ((dirIndex + returnedEntries) >= smbiosDir.dirEntries)
    {
        responseDir.push_back(0);
    }
    else
    {
        responseDir.push_back(
            smbiosDir.dirEntries - dirIndex - returnedEntries);
    }
    for (uint8_t index = dirIndex; index < smbiosDir.dirEntries; index++)
    {
        for (uint8_t indexId = 0; indexId < sizeof(DataIdStruct); indexId++)
        {
            responseDir.push_back(
                smbiosDir.dir[index].common.id.dataInfo[indexId]);
        }
    }

    return responseDir;
}

bool MDRV2::smbiosIsAvailForUpdate(uint8_t index)
{
    bool ret = false;
    if (index >= maxDirEntries)
    {
        return ret;
    }

    switch (smbiosDir.dir[index].stage)
    {
        case MDR2SMBIOSStatusEnum::mdr2Updating:
            ret = false;
            break;

        case MDR2SMBIOSStatusEnum::mdr2Init:
            // This *looks* like there should be a break statement here,
            // as the effects of the previous statement are a noop given
            // the following code that this falls through to.
            // We've elected not to change it, though, since it's been
            // this way since the old generation, and it would affect
            // the way the code functions.
            // If it ain't broke, don't fix it.

        case MDR2SMBIOSStatusEnum::mdr2Loaded:
        case MDR2SMBIOSStatusEnum::mdr2Updated:
            if (smbiosDir.dir[index].lock == MDR2DirLockEnum::mdr2DirLock)
            {
                ret = false;
            }
            else
            {
                ret = true;
            }
            break;

        default:
            break;
    }

    return ret;
}

std::vector<uint8_t> MDRV2::getDataOffer()
{
    std::vector<uint8_t> offer(sizeof(DataIdStruct));
    if (smbiosIsAvailForUpdate(0))
    {
        std::copy(smbiosDir.dir[0].common.id.dataInfo,
                  &smbiosDir.dir[0].common.id.dataInfo[16], offer.data());
    }
    else
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "smbios is not ready for update");
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            UpdateInProgress();
    }
    return offer;
}

inline uint8_t MDRV2::smbiosValidFlag(uint8_t index)
{
    FlagStatus ret = FlagStatus::flagIsInvalid;
    MDR2SMBIOSStatusEnum stage = smbiosDir.dir[index].stage;
    MDR2DirLockEnum lock = smbiosDir.dir[index].lock;

    switch (stage)
    {
        case MDR2SMBIOSStatusEnum::mdr2Loaded:
        case MDR2SMBIOSStatusEnum::mdr2Updated:
            if (lock == MDR2DirLockEnum::mdr2DirLock)
            {
                ret = FlagStatus::flagIsLocked; // locked
            }
            else
            {
                ret = FlagStatus::flagIsValid; // valid
            }
            break;

        case MDR2SMBIOSStatusEnum::mdr2Updating:
        case MDR2SMBIOSStatusEnum::mdr2Init:
            ret = FlagStatus::flagIsInvalid; // invalid
            break;

        default:
            break;
    }

    return static_cast<uint8_t>(ret);
}

// If source variable size is 4 bytes (uint32_t) and destination is Vector type
// is 1 byte (uint8_t), then by using this API can copy data byte by byte. For
// Example copying data from smbiosDir.dir[idIndex].common.size and
// smbiosDir.dir[idIndex].common.timestamp to vector variable responseInfo
template <typename T>
void appendReversed(std::vector<uint8_t>& vector, const T& value)
{
    auto data = reinterpret_cast<const uint8_t*>(&value);
    std::reverse_copy(data, data + sizeof(value), std::back_inserter(vector));
}

std::vector<uint8_t> MDRV2::getDataInformation(uint8_t idIndex)
{
    std::vector<uint8_t> responseInfo;
    responseInfo.push_back(mdr2Version);

    if (idIndex >= maxDirEntries)
    {
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }

    for (uint8_t index = 0; index < sizeof(DataIdStruct); index++)
    {
        responseInfo.push_back(
            smbiosDir.dir[idIndex].common.id.dataInfo[index]);
    }

    responseInfo.push_back(smbiosValidFlag(idIndex));
    appendReversed(responseInfo, smbiosDir.dir[idIndex].common.size);
    responseInfo.push_back(smbiosDir.dir[idIndex].common.dataVersion);
    appendReversed(responseInfo, smbiosDir.dir[idIndex].common.timestamp);

    return responseInfo;
}

bool MDRV2::readDataFromFlash(MDRSMBIOSHeader* mdrHdr, uint8_t* data)
{
    if (mdrHdr == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Read data from flash error - Invalid mdr header");
        return false;
    }
    if (data == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Read data from flash error - Invalid data point");
        return false;
    }
    std::ifstream smbiosFile(smbiosFilePath, std::ios_base::binary);
    if (!smbiosFile.good())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Read data from flash error - Open MDRV2 table file failure");
        return false;
    }
    smbiosFile.clear();
    smbiosFile.seekg(0, std::ios_base::end);
    size_t fileLength = smbiosFile.tellg();
    smbiosFile.seekg(0, std::ios_base::beg);
    if (fileLength < sizeof(MDRSMBIOSHeader))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "MDR V2 file size is smaller than mdr header");
        return false;
    }
    smbiosFile.read(reinterpret_cast<char*>(mdrHdr), sizeof(MDRSMBIOSHeader));
    if (mdrHdr->dataSize > smbiosTableStorageSize)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Data size out of limitation");
        smbiosFile.close();
        return false;
    }
    fileLength -= sizeof(MDRSMBIOSHeader);
    if (fileLength < mdrHdr->dataSize)
    {
        smbiosFile.read(reinterpret_cast<char*>(data), fileLength);
    }
    else
    {
        smbiosFile.read(reinterpret_cast<char*>(data), mdrHdr->dataSize);
    }
    smbiosFile.close();
    return true;
}

bool MDRV2::sendDirectoryInformation(
    uint8_t dirVersion, uint8_t dirIndex, uint8_t returnedEntries,
    uint8_t remainingEntries, std::vector<uint8_t> dirEntry)
{
    bool terminate = false;
    if ((dirIndex >= maxDirEntries) || (returnedEntries < 1))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Send Dir info failed - input parameter invalid");
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }
    if ((static_cast<size_t>(returnedEntries) * sizeof(DataIdStruct)) !=
        dirEntry.size())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Directory size invalid");
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }
    if (dirVersion == smbiosDir.dirVersion)
    {
        terminate = true;
    }
    else
    {
        if (remainingEntries > 0)
        {
            terminate = false;
        }
        else
        {
            terminate = true;
            smbiosDir.dirVersion = dirVersion;
        }
        uint8_t idIndex = dirIndex;
        smbiosDir.dirEntries = returnedEntries;

        uint8_t* pData = dirEntry.data();
        if (pData == nullptr)
        {
            return false;
        }
        for (uint8_t index = 0; index < returnedEntries; index++)
        {
            auto data = reinterpret_cast<const DataIdStruct*>(pData);
            std::copy(data->dataInfo, data->dataInfo + sizeof(DataIdStruct),
                      smbiosDir.dir[idIndex + index].common.id.dataInfo);
            pData += sizeof(DataIdStruct);
        }
    }
    return terminate;
}

bool MDRV2::sendDataInformation(uint8_t idIndex, uint8_t /* flag */,
                                uint32_t dataLen, uint32_t dataVer,
                                uint32_t timeStamp)
{
    if (idIndex >= maxDirEntries)
    {
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }
    int entryChanged = 0;
    if (smbiosDir.dir[idIndex].common.dataSetSize != dataLen)
    {
        entryChanged++;
        smbiosDir.dir[idIndex].common.dataSetSize = dataLen;
    }

    if (smbiosDir.dir[idIndex].common.dataVersion != dataVer)
    {
        entryChanged++;
        smbiosDir.dir[idIndex].common.dataVersion = dataVer;
    }

    if (smbiosDir.dir[idIndex].common.timestamp != timeStamp)
    {
        entryChanged++;
        smbiosDir.dir[idIndex].common.timestamp = timeStamp;
    }
    if (entryChanged == 0)
    {
        return false;
    }
    return true;
}

int MDRV2::findIdIndex(std::vector<uint8_t> dataInfo)
{
    if (dataInfo.size() != sizeof(DataIdStruct))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Length of dataInfo invalid");
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidId();
    }
    std::array<uint8_t, 16> arrayDataInfo;

    std::copy(dataInfo.begin(), dataInfo.end(), arrayDataInfo.begin());

    for (int index = 0; index < smbiosDir.dirEntries; index++)
    {
        size_t info = 0;
        for (; info < arrayDataInfo.size(); info++)
        {
            if (arrayDataInfo[info] !=
                smbiosDir.dir[index].common.id.dataInfo[info])
            {
                break;
            }
        }
        if (info == arrayDataInfo.size())
        {
            return index;
        }
    }
    throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::InvalidId();
}

uint8_t MDRV2::directoryEntries(uint8_t value)
{
    std::ifstream smbiosFile(smbiosFilePath, std::ios_base::binary);
    if (!smbiosFile.good())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Read data from flash error - Open MDRV2 table file failure");
        value = 0;
    }
    else
    {
        value = smbiosDir.dirEntries;
    }
    return sdbusplus::server::xyz::openbmc_project::smbios::MDRV2::
        directoryEntries(value);
}

void MDRV2::systemInfoUpdate()
{
    // By default, look for System interface on any system/board/* object
    std::string mapperAncestorPath = smbiosInventoryPath;
    std::string matchParentPath = smbiosInventoryPath + "/board/";
    bool requireExactMatch = false;

    // If customized, look for System on only that custom object
    if (smbiosInventoryPath != defaultInventoryPath)
    {
        std::filesystem::path path(smbiosInventoryPath);

        // Search under parent to find exact match for self
        mapperAncestorPath = path.parent_path().string();
        matchParentPath = mapperAncestorPath;
        requireExactMatch = true;
    }

    std::string motherboardPath;
    auto method = bus->new_method_call(mapperBusName, mapperPath,
                                       mapperInterface, "GetSubTreePaths");
    method.append(mapperAncestorPath);
    method.append(0);

    // If customized, also accept Board as anchor, not just System
    std::vector<std::string> desiredInterfaces{systemInterface};
    if (requireExactMatch)
    {
        desiredInterfaces.emplace_back(boardInterface);
    }
    method.append(desiredInterfaces);

    try
    {
        std::vector<std::string> paths;
        sdbusplus::message_t reply = bus->call(method);
        reply.read(paths);

        size_t pathsCount = paths.size();
        for (size_t i = 0; i < pathsCount; ++i)
        {
            if (requireExactMatch && (paths[i] != smbiosInventoryPath))
            {
                continue;
            }

            motherboardPath = std::move(paths[i]);
            break;
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error(
            "Exception while trying to find Inventory anchor object for SMBIOS content {I}: {E}",
            "I", smbiosInventoryPath, "E", e.what());
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Failed to query system motherboard",
            phosphor::logging::entry("ERROR=%s", e.what()));
    }

    if (motherboardPath.empty())
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Failed to get system motherboard dbus path. Setting up a "
            "match rule");

        if (motherboardConfigMatch)
        {
            lg2::info("Motherboard match rule already exists");
        }
        else
        {
            motherboardConfigMatch = std::make_unique<sdbusplus::bus::match_t>(
                *bus,
                sdbusplus::bus::match::rules::interfacesAdded() +
                    sdbusplus::bus::match::rules::argNpath(0, matchParentPath),
                [this, requireExactMatch](sdbusplus::message_t& msg) {
                    sdbusplus::message::object_path objectName;
                    boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::variant<std::string, uint64_t>>>
                        msgData;
                    msg.read(objectName, msgData);
                    bool gotMatch = false;

                    if (msgData.contains(systemInterface))
                    {
                        lg2::info("Successful match on system interface");
                        gotMatch = true;
                    }

                    // If customized, also accept Board as anchor, not just
                    // System
                    if (requireExactMatch && msgData.contains(boardInterface))
                    {
                        lg2::info("Successful match on board interface");
                        gotMatch = true;
                    }

                    if (gotMatch)
                    {
                        // There is a race condition here: our desired interface
                        // has just been created, triggering the D-Bus callback,
                        // but Object Mapper has not been told of it yet. The
                        // mapper must also add it. Stall for time, so it can.
                        sleep(2);
                        systemInfoUpdate();
                    }
                });
        }
    }
    else
    {
#ifdef ASSOC_TRIM_PATH
        // When enabled, chop off last component of motherboardPath, to trim one
        // layer, so that associations are built to the underlying chassis
        // itself, not the system boards in the chassis. This is for
        // compatibility with traditional systems which only have one
        // motherboard per chassis.
        std::filesystem::path foundPath(motherboardPath);
        motherboardPath = foundPath.parent_path().string();
#endif

        lg2::info("Found Inventory anchor object for SMBIOS content {I}: {M}",
                  "I", smbiosInventoryPath, "M", motherboardPath);
    }

    lg2::info("Using Inventory anchor object for SMBIOS content {I}: {M}", "I",
              smbiosInventoryPath, "M", motherboardPath);

    std::optional<size_t> num = getTotalCpuSlot();
    if (!num)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "get cpu total slot failed");
        return;
    }

    // In case the new size is smaller than old, trim the vector
    if (*num < cpus.size())
    {
        cpus.resize(*num);
    }

    for (unsigned int index = 0; index < *num; index++)
    {
        std::string path =
            smbiosInventoryPath + cpuSuffix + std::to_string(index);
        if (index + 1 > cpus.size())
        {
            cpus.emplace_back(std::make_unique<phosphor::smbios::Cpu>(
                *bus, path, index, smbiosDir.dir[smbiosDirIndex].dataStorage,
                motherboardPath));
        }
        else
        {
            cpus[index]->infoUpdate(smbiosDir.dir[smbiosDirIndex].dataStorage,
                                    motherboardPath);
        }
    }

#ifdef DIMM_DBUS

    num = getTotalDimmSlot();
    if (!num)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "get dimm total slot failed");
        return;
    }

    // In case the new size is smaller than old, trim the vector
    if (*num < dimms.size())
    {
        dimms.resize(*num);
    }

    for (unsigned int index = 0; index < *num; index++)
    {
        std::string path =
            smbiosInventoryPath + dimmSuffix + std::to_string(index);
        if (index + 1 > dimms.size())
        {
            dimms.emplace_back(std::make_unique<phosphor::smbios::Dimm>(
                *bus, path, index, smbiosDir.dir[smbiosDirIndex].dataStorage,
                motherboardPath));
        }
        else
        {
            dimms[index]->memoryInfoUpdate(
                smbiosDir.dir[smbiosDirIndex].dataStorage, motherboardPath);
        }
    }

#endif

    num = getTotalPcieSlot();
    if (!num)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "get pcie total slot failed");
        return;
    }

    // In case the new size is smaller than old, trim the vector
    if (*num < pcies.size())
    {
        pcies.resize(*num);
    }

    for (unsigned int index = 0; index < *num; index++)
    {
        std::string path =
            smbiosInventoryPath + pcieSuffix + std::to_string(index);
        if (index + 1 > pcies.size())
        {
            pcies.emplace_back(std::make_unique<phosphor::smbios::Pcie>(
                *bus, path, index, smbiosDir.dir[smbiosDirIndex].dataStorage,
                motherboardPath));
        }
        else
        {
            pcies[index]->pcieInfoUpdate(
                smbiosDir.dir[smbiosDirIndex].dataStorage, motherboardPath);
        }
    }

#ifdef TPM_DBUS

    num = getTotalTpm();
    if (!num)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>("get tpm failed");
        return;
    }
    // In case the new size is smaller than old, trim the vector
    if (*num < tpms.size())
    {
        tpms.resize(*num);
    }

    for (unsigned int index = 0; index < *num; index++)
    {
        std::string path =
            smbiosInventoryPath + tpmSuffix + std::to_string(index);
        if (index + 1 > tpms.size())
        {
            tpms.emplace_back(std::make_unique<phosphor::smbios::Tpm>(
                *bus, path, index, smbiosDir.dir[smbiosDirIndex].dataStorage,
                motherboardPath));
        }
        else
        {
            tpms[index]->tpmInfoUpdate(
                smbiosDir.dir[smbiosDirIndex].dataStorage, motherboardPath);
        }
    }
#endif

    system.reset();
    system = std::make_unique<System>(bus, smbiosInventoryPath + systemSuffix,
                                      smbiosDir.dir[smbiosDirIndex].dataStorage,
                                      smbiosFilePath);
}

std::optional<size_t> MDRV2::getTotalCpuSlot()
{
    uint8_t* dataIn = smbiosDir.dir[smbiosDirIndex].dataStorage;
    size_t num = 0;

    if (dataIn == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "get cpu total slot failed - no storage data");
        return std::nullopt;
    }

    while (1)
    {
        dataIn = getSMBIOSTypePtr(dataIn, processorsType);
        if (dataIn == nullptr)
        {
            break;
        }
        num++;
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            break;
        }
        if (num >= limitEntryLen)
        {
            break;
        }
    }

    return num;
}

std::optional<size_t> MDRV2::getTotalDimmSlot()
{
    uint8_t* dataIn = smbiosDir.dir[smbiosDirIndex].dataStorage;
    size_t num = 0;

    if (dataIn == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Fail to get dimm total slot - no storage data");
        return std::nullopt;
    }

    while (1)
    {
        dataIn = getSMBIOSTypePtr(dataIn, memoryDeviceType);
        if (dataIn == nullptr)
        {
            break;
        }
        num++;
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            break;
        }
        if (num >= limitEntryLen)
        {
            break;
        }
    }

    return num;
}

std::optional<size_t> MDRV2::getTotalPcieSlot()
{
    uint8_t* dataIn = smbiosDir.dir[smbiosDirIndex].dataStorage;
    size_t num = 0;

    if (dataIn == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Fail to get total system slot - no storage data");
        return std::nullopt;
    }

    while (1)
    {
        dataIn = getSMBIOSTypePtr(dataIn, systemSlots);
        if (dataIn == nullptr)
        {
            break;
        }

        /* System slot type offset. Check if the slot is a PCIE slots. All
         * PCIE slot type are hardcoded in a table.
         */
        if (pcieSmbiosType.find(*(dataIn + 5)) != pcieSmbiosType.end())
        {
            num++;
        }
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            break;
        }
        if (num >= limitEntryLen)
        {
            break;
        }
    }

    return num;
}

std::optional<size_t> MDRV2::getTotalTpm()
{
    uint8_t* dataIn = smbiosDir.dir[smbiosDirIndex].dataStorage;
    size_t num = 0;

    if (dataIn == nullptr)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Fail to get tpm total slot - no storage data");
        return std::nullopt;
    }

    while (1)
    {
        dataIn = getSMBIOSTypePtr(dataIn, tpmDeviceType);
        if (dataIn == nullptr)
        {
            break;
        }
        num++;
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            break;
        }
        if (num >= limitEntryLen)
        {
            break;
        }
    }

    return num;
}

bool MDRV2::checkSMBIOSVersion(uint8_t* dataIn)
{
    const std::string anchorString21 = "_SM_";
    const std::string anchorString30 = "_SM3_";
    std::string buffer(reinterpret_cast<const char*>(dataIn),
                       smbiosTableStorageSize);

    auto it = std::search(std::begin(buffer), std::end(buffer),
                          std::begin(anchorString21), std::end(anchorString21));
    bool smbios21Found = it != std::end(buffer);
    if (!smbios21Found)
    {
        phosphor::logging::log<phosphor::logging::level::INFO>(
            "SMBIOS 2.1 Anchor String not found. Looking for SMBIOS 3.0");
        it = std::search(std::begin(buffer), std::end(buffer),
                         std::begin(anchorString30), std::end(anchorString30));
        if (it == std::end(buffer))
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "SMBIOS 2.1 and 3.0 Anchor Strings not found");
            return false;
        }
    }

    auto pos = std::distance(std::begin(buffer), it);
    size_t length = smbiosTableStorageSize - pos;
    uint8_t foundMajorVersion;
    uint8_t foundMinorVersion;

    if (smbios21Found)
    {
        if (length < sizeof(EntryPointStructure21))
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Invalid entry point structure for SMBIOS 2.1");
            return false;
        }

        auto epStructure =
            reinterpret_cast<const EntryPointStructure21*>(&dataIn[pos]);
        foundMajorVersion = epStructure->smbiosVersion.majorVersion;
        foundMinorVersion = epStructure->smbiosVersion.minorVersion;
    }
    else
    {
        if (length < sizeof(EntryPointStructure30))
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Invalid entry point structure for SMBIOS 3.0");
            return false;
        }

        auto epStructure =
            reinterpret_cast<const EntryPointStructure30*>(&dataIn[pos]);
        foundMajorVersion = epStructure->smbiosVersion.majorVersion;
        foundMinorVersion = epStructure->smbiosVersion.minorVersion;
    }
    lg2::info("SMBIOS VERSION - {MAJOR}.{MINOR}", "MAJOR", foundMajorVersion,
              "MINOR", foundMinorVersion);

    auto itr = std::find_if(
        std::begin(supportedSMBIOSVersions), std::end(supportedSMBIOSVersions),
        [&](SMBIOSVersion versionItr) {
            return versionItr.majorVersion == foundMajorVersion &&
                   versionItr.minorVersion == foundMinorVersion;
        });
    if (itr == std::end(supportedSMBIOSVersions))
    {
        return false;
    }
    return true;
}

bool MDRV2::agentSynchronizeData()
{
    struct MDRSMBIOSHeader mdr2SMBIOS;
    bool status = readDataFromFlash(&mdr2SMBIOS,
                                    smbiosDir.dir[smbiosDirIndex].dataStorage);
    if (!status)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "agent data sync failed - read data from flash failed");
        return false;
    }

    if (!checkSMBIOSVersion(smbiosDir.dir[smbiosDirIndex].dataStorage))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Unsupported SMBIOS table version");
        return false;
    }

    if (0 == static_cast<uint8_t>(sdbusplus::server::xyz::openbmc_project::
                                      smbios::MDRV2::directoryEntries()))
    {
        directoryEntries(smbiosDir.dirEntries);
    }

    systemInfoUpdate();
    smbiosDir.dir[smbiosDirIndex].common.dataVersion = mdr2SMBIOS.dirVer;
    smbiosDir.dir[smbiosDirIndex].common.timestamp = mdr2SMBIOS.timestamp;
    smbiosDir.dir[smbiosDirIndex].common.size = mdr2SMBIOS.dataSize;
    smbiosDir.dir[smbiosDirIndex].stage = MDR2SMBIOSStatusEnum::mdr2Loaded;
    smbiosDir.dir[smbiosDirIndex].lock = MDR2DirLockEnum::mdr2DirUnlock;

    return true;
}

std::vector<uint32_t> MDRV2::synchronizeDirectoryCommonData(uint8_t idIndex,
                                                            uint32_t size)
{
    std::chrono::microseconds usec(
        defaultTimeout); // default lock time out is 2s
    std::vector<uint32_t> result;
    smbiosDir.dir[idIndex].common.size = size;
    result.push_back(smbiosDir.dir[idIndex].common.dataSetSize);
    result.push_back(smbiosDir.dir[idIndex].common.dataVersion);
    result.push_back(smbiosDir.dir[idIndex].common.timestamp);

    timer.expires_after(usec);
    timer.async_wait([this](boost::system::error_code ec) {
        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Timer Error!");
            return;
        }
        agentSynchronizeData();
    });
    return result;
}

std::vector<boost::container::flat_map<std::string, RecordVariant>>
    MDRV2::getRecordType(size_t type)
{
    std::vector<boost::container::flat_map<std::string, RecordVariant>> ret;
    if (type == memoryDeviceType)
    {
        uint8_t* dataIn = smbiosDir.dir[smbiosDirIndex].dataStorage;

        if (dataIn == nullptr)
        {
            throw std::runtime_error("Data not populated");
        }

        do
        {
            dataIn =
                getSMBIOSTypePtr(dataIn, memoryDeviceType, sizeof(MemoryInfo));
            if (dataIn == nullptr)
            {
                break;
            }
            boost::container::flat_map<std::string, RecordVariant>& record =
                ret.emplace_back();

            auto memoryInfo = reinterpret_cast<MemoryInfo*>(dataIn);

            record["Type"] = memoryInfo->type;
            record["Length"] = memoryInfo->length;
            record["Handle"] = uint16_t(memoryInfo->handle);
            record["Physical Memory Array Handle"] =
                uint16_t(memoryInfo->phyArrayHandle);
            record["Memory Error Information Handle"] =
                uint16_t(memoryInfo->errInfoHandle);
            record["Total Width"] = uint16_t(memoryInfo->totalWidth);
            record["Data Width"] = uint16_t(memoryInfo->dataWidth);
            record["Size"] = uint16_t(memoryInfo->size);
            record["Form Factor"] = memoryInfo->formFactor;
            record["Device Set"] = memoryInfo->deviceSet;
            record["Device Locator"] = positionToString(
                memoryInfo->deviceLocator, memoryInfo->length, dataIn);
            record["Bank Locator"] = positionToString(
                memoryInfo->bankLocator, memoryInfo->length, dataIn);
            record["Memory Type"] = memoryInfo->memoryType;
            record["Type Detail"] = uint16_t(memoryInfo->typeDetail);
            record["Speed"] = uint16_t(memoryInfo->speed);
            record["Manufacturer"] = positionToString(
                memoryInfo->manufacturer, memoryInfo->length, dataIn);
            record["Serial Number"] = positionToString(
                memoryInfo->serialNum, memoryInfo->length, dataIn);
            record["Asset Tag"] = positionToString(memoryInfo->assetTag,
                                                   memoryInfo->length, dataIn);
            record["Part Number"] = positionToString(
                memoryInfo->partNum, memoryInfo->length, dataIn);
            record["Attributes"] = uint32_t(memoryInfo->attributes);
            record["Extended Size"] = uint32_t(memoryInfo->extendedSize);
            record["Configured Memory Speed"] =
                uint32_t(memoryInfo->confClockSpeed);
            record["Minimum voltage"] = uint16_t(memoryInfo->minimumVoltage);
            record["Maximum voltage"] = uint16_t(memoryInfo->maximumVoltage);
            record["Configured voltage"] =
                uint16_t(memoryInfo->configuredVoltage);
            record["Memory Technology"] = memoryInfo->memoryTechnology;
            record["Memory Operating Mode Capability"] =
                uint16_t(memoryInfo->memoryOperatingModeCap);
            record["Firmware Version"] = memoryInfo->firwareVersion;
            record["Module Manufacturer ID"] =
                uint16_t(memoryInfo->modelManufId);
            record["Module Product ID"] = uint16_t(memoryInfo->modelProdId);
            record["Memory Subsystem Controller Manufacturer ID"] =
                uint16_t(memoryInfo->memSubConManufId);
            record["Memory Subsystem Controller Product Id"] =
                uint16_t(memoryInfo->memSubConProdId);
            record["Non-volatile Size"] = uint64_t(memoryInfo->nvSize);
            record["Volatile Size"] = uint64_t(memoryInfo->volatileSize);
            record["Cache Size"] = uint64_t(memoryInfo->cacheSize);
            record["Logical Size"] = uint64_t(memoryInfo->logicalSize);
        } while ((dataIn = smbiosNextPtr(dataIn)) != nullptr);

        return ret;
    }

    throw std::invalid_argument("Invalid record type");
    return ret;
}

} // namespace smbios
} // namespace phosphor
