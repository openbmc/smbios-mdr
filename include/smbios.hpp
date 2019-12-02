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

#pragma once

#include <array>

static constexpr uint16_t smbiosAgentId = 0x0101;
static constexpr int smbiosDirIndex = 0;
static constexpr int firstAgentIndex = 1;
static constexpr uint8_t maxDirEntries = 4;
static constexpr uint32_t pageMask = 0xf000;
static constexpr uint8_t smbiosAgentVersion = 1;
static constexpr uint32_t defaultTimeout = 20000;
static constexpr uint32_t smbiosTableVersion = 15;
static constexpr uint32_t smbiosSMMemoryOffset = 0;
static constexpr uint32_t smbiosSMMemorySize = 1024 * 1024;
static constexpr uint32_t smbiosTableTimestamp = 0x45464748;
static constexpr uint32_t smbiosTableStorageSize = 64 * 1024;
static constexpr const char* smbiosPath = "/var/lib/smbios";

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
