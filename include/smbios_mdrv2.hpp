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

#include <phosphor-logging/elog-errors.hpp>

#include <array>
#include <string>

static constexpr const char* mdrDefaultFile = "/var/lib/smbios/smbios2";

static constexpr uint16_t mdrSMBIOSSize = 32 * 1024;

constexpr uint16_t smbiosAgentId = 0x0101;
constexpr int firstAgentIndex = 1;

constexpr uint8_t maxDirEntries = 4;
constexpr uint32_t mdr2SMSize = 0x00100000;
constexpr uint32_t mdr2SMBaseAddress = 0x9FF00000;

constexpr uint8_t mdrDirVersion = 1;
constexpr uint8_t mdrTypeII = 2;

constexpr uint8_t mdr2Version = 2;
constexpr uint8_t smbiosAgentVersion = 1;
constexpr uint8_t smbiosDirVersion = 1;

constexpr uint32_t pageMask = 0xf000;
constexpr int smbiosDirIndex = 0;

constexpr uint32_t smbiosTableVersion = 15;
constexpr uint32_t smbiosTableTimestamp = 0x45464748;
constexpr uint32_t smbiosSMMemoryOffset = 0;
constexpr uint32_t smbiosSMMemorySize = 1024 * 1024;
constexpr uint32_t smbiosTableStorageSize = 64 * 1024;
constexpr uint32_t defaultTimeout = 2'000'000; // 2-seconds.

enum class MDR2SMBIOSStatusEnum
{
    mdr2Init = 0,
    mdr2Loaded = 1,
    mdr2Updated = 2,
    mdr2Updating = 3
};

enum class MDR2DirLockEnum
{
    mdr2DirUnlock = 0,
    mdr2DirLock = 1
};

enum class DirDataRequestEnum
{
    dirDataNotRequested = 0x00,
    dirDataRequested = 0x01
};

enum class FlagStatus
{
    flagIsInvalid = 0,
    flagIsValid = 1,
    flagIsLocked = 2
};

typedef struct
{
    uint8_t dataInfo[16];
} DataIdStruct;

typedef struct
{
    DataIdStruct id;
    uint32_t size;
    uint32_t dataSetSize;
    uint8_t dataVersion;
    uint32_t timestamp;
} Mdr2DirEntry;

typedef struct
{
    Mdr2DirEntry common;
    MDR2SMBIOSStatusEnum stage;
    MDR2DirLockEnum lock;
    uint16_t lockHandle;
    uint32_t xferBuff;
    uint32_t xferSize;
    uint32_t maxDataSize;
    uint8_t* dataStorage;
} Mdr2DirLocalStruct;

typedef struct
{
    uint8_t agentVersion;
    uint8_t dirVersion;
    uint8_t dirEntries;
    uint8_t status; // valid / locked / etc
    uint8_t remoteDirVersion;
    uint16_t sessionHandle;
    Mdr2DirLocalStruct dir[maxDirEntries];
} Mdr2DirStruct;

struct MDRSMBIOSHeader
{
    uint8_t dirVer;
    uint8_t mdrType;
    uint32_t timestamp;
    uint32_t dataSize;
} __attribute__((packed));

typedef struct
{
    uint8_t majorVersion;
    uint8_t minorVersion;
} SMBIOSVersion;

struct EntryPointStructure21
{
    uint32_t anchorString;
    uint8_t epChecksum;
    uint8_t epLength;
    SMBIOSVersion smbiosVersion;
    uint16_t maxStructSize;
    uint8_t epRevision;
    uint8_t formattedArea[5];
    uint8_t intermediateAnchorString[5];
    uint8_t intermediateChecksum;
    uint16_t structTableLength;
    uint32_t structTableAddress;
    uint16_t noOfSmbiosStruct;
    uint8_t smbiosBDCRevision;
} __attribute__((packed));

struct EntryPointStructure30
{
    uint8_t anchorString[5];
    uint8_t epChecksum;
    uint8_t epLength;
    SMBIOSVersion smbiosVersion;
    uint8_t smbiosDocRev;
    uint8_t epRevision;
    uint8_t reserved;
    uint32_t structTableMaxSize;
    uint64_t structTableAddr;
} __attribute__((packed));

static constexpr const char* cpuSuffix = "/chassis/motherboard/cpu";

static constexpr const char* dimmSuffix = "/chassis/motherboard/dimm";

static constexpr const char* pcieSuffix = "/chassis/motherboard/pcieslot";

static constexpr const char* systemSuffix = "/chassis/motherboard/bios";

constexpr std::array<SMBIOSVersion, 8> supportedSMBIOSVersions{
    SMBIOSVersion{3, 0}, SMBIOSVersion{3, 2}, SMBIOSVersion{3, 3},
    SMBIOSVersion{3, 4}, SMBIOSVersion{3, 5}, SMBIOSVersion{3, 6},
    SMBIOSVersion{3, 7}, SMBIOSVersion{3, 8}};

typedef enum
{
    biosType = 0,
    systemType = 1,
    baseboardType = 2,
    chassisType = 3,
    processorsType = 4,
    memoryControllerType = 5,
    memoryModuleInformationType = 6,
    cacheType = 7,
    portConnectorType = 8,
    systemSlots = 9,
    onBoardDevicesType = 10,
    oemStringsType = 11,
    systemCconfigurationOptionsType = 12,
    biosLanguageType = 13,
    groupAssociatonsType = 14,
    systemEventLogType = 15,
    physicalMemoryArrayType = 16,
    memoryDeviceType = 17,
} SmbiosType;

static constexpr uint8_t separateLen = 2;

static inline uint8_t* smbiosNextPtr(uint8_t* smbiosDataIn)
{
    if (smbiosDataIn == nullptr)
    {
        return nullptr;
    }
    uint8_t* smbiosData = smbiosDataIn + *(smbiosDataIn + 1);
    int len = 0;
    while ((*smbiosData | *(smbiosData + 1)) != 0)
    {
        smbiosData++;
        len++;
        if (len >= mdrSMBIOSSize) // To avoid endless loop
        {
            return nullptr;
        }
    }
    return smbiosData + separateLen;
}

// When first time run getSMBIOSTypePtr, need to send the RegionS[].regionData
// to smbiosDataIn
static inline uint8_t* getSMBIOSTypePtr(uint8_t* smbiosDataIn, uint8_t typeId,
                                        size_t size = 0)
{
    if (smbiosDataIn == nullptr)
    {
        return nullptr;
    }
    char* smbiosData = reinterpret_cast<char*>(smbiosDataIn);
    while ((*smbiosData != '\0') || (*(smbiosData + 1) != '\0'))
    {
        uint32_t len = *(smbiosData + 1);
        if (*smbiosData != typeId)
        {
            smbiosData += len;
            while ((*smbiosData != '\0') || (*(smbiosData + 1) != '\0'))
            {
                smbiosData++;
                len++;
                if (len >= mdrSMBIOSSize) // To avoid endless loop
                {
                    return nullptr;
                }
            }
            smbiosData += separateLen;
            continue;
        }
        if (len < size)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Record size mismatch!");
            return nullptr;
        }
        return reinterpret_cast<uint8_t*>(smbiosData);
    }
    return nullptr;
}

static inline std::string positionToString(uint8_t positionNum,
                                           uint8_t structLen, uint8_t* dataIn)
{
    if (dataIn == nullptr || positionNum == 0)
    {
        return "";
    }
    uint16_t limit = mdrSMBIOSSize; // set a limit to avoid endless loop

    char* target = reinterpret_cast<char*>(dataIn + structLen);
    if (target == nullptr)
    {
        return "";
    }
    for (uint8_t index = 1; index < positionNum; index++)
    {
        for (; *target != '\0'; target++)
        {
            limit--;
            // When target = dataIn + structLen + limit,
            // following target++ will be nullptr
            if (limit < 1 || target == nullptr)
            {
                return "";
            }
        }
        target++;
        if (target == nullptr || *target == '\0')
        {
            return ""; // 0x00 0x00 means end of the entry.
        }
    }

    std::string result = target;
    return result;
}
