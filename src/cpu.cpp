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

#include "cpu.hpp"

#include <bitset>
#include <map>

namespace phosphor
{
namespace smbios
{

void Cpu::socket(const uint8_t positionNum, const uint8_t structLen,
                 uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    processor::socket(result);

    location::locationCode(result);
}

static constexpr uint8_t processorFamily2Indicator = 0xfe;
void Cpu::family(const uint8_t family, const uint16_t family2)
{
    std::map<uint8_t, const char*>::const_iterator it =
        familyTable.find(family);
    if (it == familyTable.end())
    {
        processor::family("Unknown Processor Family");
    }
    else if (it->first == processorFamily2Indicator)
    {
        std::map<uint16_t, const char*>::const_iterator it2 =
            family2Table.find(family2);
        if (it2 == family2Table.end())
        {
            processor::family("Unknown Processor Family");
        }
        else
        {
            processor::family(it2->second);
            processor::effectiveFamily(family2);
        }
    }
    else
    {
        processor::family(it->second);
        processor::effectiveFamily(family);
    }
}

void Cpu::manufacturer(const uint8_t positionNum, const uint8_t structLen,
                       uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    asset::manufacturer(result);
}

void Cpu::partNumber(const uint8_t positionNum, const uint8_t structLen,
                     uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    asset::partNumber(result);
}

void Cpu::serialNumber(const uint8_t positionNum, const uint8_t structLen,
                       uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);

    asset::serialNumber(result);
}

void Cpu::version(const uint8_t positionNum, const uint8_t structLen,
                  uint8_t* dataIn)
{
    std::string result;

    result = positionToString(positionNum, structLen, dataIn);

    rev::version(result);
}

void Cpu::characteristics(uint16_t value)
{
    std::vector<processor::Capability> result;
    std::optional<processor::Capability> cap;

    std::bitset<16> charBits = value;
    for (uint8_t index = 0; index < charBits.size(); index++)
    {
        if (charBits.test(index))
        {
            if (cap = characteristicsTable[index])
            {
                result.emplace_back(*cap);
            }
        }
    }

    processor::characteristics(result);
}

static constexpr uint8_t maxOldVersionCount = 0xff;
void Cpu::infoUpdate(void)
{
    uint8_t* dataIn = storage;

    dataIn = getSMBIOSTypePtr(dataIn, processorsType);
    if (dataIn == nullptr)
    {
        return;
    }

    for (uint8_t index = 0; index < cpuNum; index++)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return;
        }
        dataIn = getSMBIOSTypePtr(dataIn, processorsType);
        if (dataIn == nullptr)
        {
            return;
        }
    }

    auto cpuInfo = reinterpret_cast<struct ProcessorInfo*>(dataIn);

    socket(cpuInfo->socketDesignation, cpuInfo->length, dataIn); // offset 4h

    constexpr uint32_t socketPopulatedMask = 1 << 6;
    if ((cpuInfo->status & socketPopulatedMask) == 0)
    {
        // Don't attempt to fill in any other details if the CPU is not present.
        present(false);
        return;
    }
    present(true);

    // this class is for type CPU  //offset 5h
    family(cpuInfo->family, cpuInfo->family2); // offset 6h and 28h
    manufacturer(cpuInfo->manufacturer, cpuInfo->length,
                 dataIn); // offset 7h
    id(cpuInfo->id);      // offset 8h
    // Processor ID field
    // SteppinID:   4;
    // Model:       4;
    // Family:      4;
    // Type:        2;
    // Reserved1:   2;
    // XModel:      4;
    // XFamily:     8;
    // Reserved2:   4;
    uint16_t cpuStep = cpuInfo->id & 0xf;
    uint16_t cpuModel = (cpuInfo->id & 0xf0) >> 4;
    uint16_t cpuFamily = (cpuInfo->id & 0xf00) >> 8;
    uint16_t cpuXModel = (cpuInfo->id & 0xf0000) >> 16;
    step(cpuStep);
    if (cpuFamily == 0x6)
    {
        effectiveModel((cpuXModel << 4) | cpuModel);
    }
    else
    {
        effectiveModel(cpuModel);
    }

    version(cpuInfo->version, cpuInfo->length, dataIn); // offset 10h
    maxSpeedInMhz(cpuInfo->maxSpeed);                   // offset 14h
    serialNumber(cpuInfo->serialNum, cpuInfo->length,
                 dataIn); // offset 20h
    partNumber(cpuInfo->partNum, cpuInfo->length,
               dataIn);                          // offset 22h
    if (cpuInfo->coreCount < maxOldVersionCount) // offset 23h or 2Ah
    {
        coreCount(cpuInfo->coreCount);
    }
    else
    {
        coreCount(cpuInfo->coreCount2);
    }

    if (cpuInfo->threadCount < maxOldVersionCount) // offset 25h or 2Eh)
    {
        threadCount(cpuInfo->threadCount);
    }
    else
    {
        threadCount(cpuInfo->threadCount2);
    }

    characteristics(cpuInfo->characteristics); // offset 26h

    if (!motherboardPath.empty())
    {
        std::vector<std::tuple<std::string, std::string, std::string>> assocs;
        assocs.emplace_back("chassis", "processors", motherboardPath);
        association::associations(assocs);
    }
}

} // namespace smbios
} // namespace phosphor
