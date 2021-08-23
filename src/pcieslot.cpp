#include "pcieslot.hpp"

#include <cstdint>
#include <map>

namespace phosphor
{
namespace smbios
{

void Pcie::pcieInfoUpdate()
{
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
    pcieType(pcieInfo->slotType);
    pcieLaneSize(pcieInfo->slotDataBusWidth);
    pcieIsHotPluggable(pcieInfo->characteristics2);
    pcieLocation(pcieInfo->slotDesignation, pcieInfo->length, dataIn);

    /* Pcie slot is embedded on the board. Always be true */
    Item::present(true);
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

void Pcie::pcieType(const uint8_t type)
{
    std::map<uint8_t, PCIeType>::const_iterator it = pcieTypeTable.find(type);
    if (it == pcieTypeTable.end())
    {
        PCIeSlot::slotType(PCIeType::Unknown);
    }
    else
    {
        PCIeSlot::slotType(it->second);
    }
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
