/*
// Copyright (c) 2019 Intel Corporation
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

#include <sys/mman.h>

#include <fstream>
#include <phosphor-logging/elog-errors.hpp>
#include <sdbusplus/exception.hpp>
#include <xyz/openbmc_project/Smbios/MDR_V2/error.hpp>

namespace phosphor
{
namespace smbios
{

std::vector<uint8_t> MDR_V2::getDirectoryInformation(uint8_t dirIndex)
{
}

bool MDR_V2::smbiosIsAvailForUpdate(uint8_t index)
{
    bool ret = false;
    if (index > maxDirEntries)
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

std::vector<uint8_t> MDR_V2::getDataOffer()
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

inline uint8_t MDR_V2::smbiosValidFlag(uint8_t index)
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

std::vector<uint8_t> MDR_V2::getDataInformation(uint8_t idIndex)
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
    responseInfo.push_back(smbiosDir.dir[idIndex].common.size);
    responseInfo.push_back(smbiosDir.dir[idIndex].common.dataVersion);
    responseInfo.push_back(smbiosDir.dir[idIndex].common.timestamp);

    return responseInfo;
}

bool MDR_V2::sendDirectoryInformation(uint8_t dirVersion, uint8_t dirIndex,
                                      uint8_t returnedEntries,
                                      uint8_t remainingEntries,
                                      std::vector<uint8_t> dirEntry)
{
}

bool MDR_V2::sendDataInformation(uint8_t idIndex, uint8_t flag,
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

int MDR_V2::findIdIndex(std::vector<uint8_t> dataInfo)
{
    if (dataInfo.size() != sizeof(DataIdStruct))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Length of dataInfo invalid");
        throw sdbusplus::xyz::openbmc_project::Smbios::MDR_V2::Error::
            InvalidParameter();
    }
    std::array<uint8_t, 16> arrayDataInfo;

    std::copy(dataInfo.begin(), dataInfo.end(), arrayDataInfo.begin());

    for (int index = 0; index < smbiosDir.dirEntries; index++)
    {
        int info = 0;
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

uint8_t MDR_V2::directoryEntries(uint8_t value)
{
    value = smbiosDir.dirEntries;
    return sdbusplus::xyz::openbmc_project::Smbios::server::MDR_V2::
        directoryEntries(value);
}

bool MDR_V2::agentSynchronizeData()
{
}

std::vector<uint32_t> MDR_V2::synchronizeDirectoryCommonData(uint8_t idIndex,
                                                             uint32_t size)
{
}

} // namespace smbios
} // namespace phosphor
