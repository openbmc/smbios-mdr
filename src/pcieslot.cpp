#include "pcieslot.hpp"

#include <cstdint>
#include <map>

namespace phosphor
{
namespace smbios
{

void Pcie::pcieInfoUpdate(uint8_t* smbiosTableStorage,
                          const std::string& motherboard)
{
    storage = smbiosTableStorage;
    motherboardPath = motherboard;

    uint8_t* dataIn = getSMBIOSTypePtr(storage, systemSlots);

    if (dataIn == nullptr)
    {
        return;
    }

    /* offset 5 points to the slot type */
    for (uint8_t index = 0;
         index < pcieNum ||
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

    auto pcieInfo = reinterpret_cast<struct SystemSlotInfo*>(dataIn);

    pcieGeneration(pcieInfo->slotType);
    pcieType(pcieInfo->slotType, pcieInfo->slotLength);
    pcieLaneSize(pcieInfo->slotDataBusWidth);
    pcieIsHotPluggable(pcieInfo->characteristics2);
    pcieLocation(pcieInfo->slotDesignation, pcieInfo->length, dataIn);

    /* Pcie slot is embedded on the board. Always be true */
    Item::present(true);

    if (!motherboardPath.empty())
    {
        std::vector<std::tuple<std::string, std::string, std::string>> assocs;
        assocs.emplace_back("chassis", "pcie_slots", motherboardPath);
        association::associations(assocs);
    }
}

void Pcie::pcieGeneration(const uint8_t type)
{
    std::map<uint8_t, PCIeGeneration>::const_iterator it =
        pcieGenerationTable.find(type);
    if (it == pcieGenerationTable.end())
    {
        PCIeSlot::generation(PCIeGeneration::Unknown);
    }
    else
    {
        PCIeSlot::generation(it->second);
    }
}

void Pcie::pcieType(const uint8_t type, const uint8_t slotLength)
{
    // Try to find PCIeType in the main table
    auto it = pcieTypeTable.find(type);
    std::optional<PCIeType> pcieSlotType =
        (it != pcieTypeTable.end()) ? std::make_optional(it->second)
                                    : std::nullopt;

    // If not found in `pcieTypeTable`, check `PCIeTypeByLength`
    if (!pcieSlotType)
    {
        auto slotIt = PCIeTypeByLength.find(slotLength);
        if (slotIt != PCIeTypeByLength.end())
        {
            pcieSlotType = slotIt->second;
        }
        else
        {
            pcieSlotType = PCIeType::Unknown;
        }
    }

    // Set the slot type
    PCIeSlot::slotType(*pcieSlotType);
}

void Pcie::pcieLaneSize(const uint8_t width)
{
    std::map<uint8_t, size_t>::const_iterator it = pcieLanesTable.find(width);
    if (it == pcieLanesTable.end())
    {
        PCIeSlot::lanes(0);
    }
    else
    {
        PCIeSlot::lanes(it->second);
    }
}

void Pcie::pcieIsHotPluggable(const uint8_t characteristics)
{
    /*  Bit 1 of slot characteristics 2 indicates if slot supports hot-plug
     *  devices
     */
    PCIeSlot::hotPluggable(characteristics & 0x2);
}

void Pcie::pcieLocation(const uint8_t slotDesignation, const uint8_t structLen,
                        uint8_t* dataIn)
{
    location::locationCode(
        positionToString(slotDesignation, structLen, dataIn));
}

} // namespace smbios
} // namespace phosphor
