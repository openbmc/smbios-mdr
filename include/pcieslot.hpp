#pragma once
#include "smbios_mdrv2.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Connector/Embedded/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/PCIeSlot/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>

#include <cstdint>
#include <map>
#include <unordered_set>

namespace phosphor
{

namespace smbios
{

using PCIeSlot =
    sdbusplus::server::xyz::openbmc_project::inventory::item::PCIeSlot;
using PCIeGeneration = sdbusplus::server::xyz::openbmc_project::inventory::
    item::PCIeSlot::Generations;
using PCIeType = sdbusplus::server::xyz::openbmc_project::inventory::item::
    PCIeSlot::SlotTypes;
using embedded =
    sdbusplus::server::xyz::openbmc_project::inventory::connector::Embedded;
using location =
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::LocationCode;
using item = sdbusplus::server::xyz::openbmc_project::inventory::Item;
using association =
    sdbusplus::server::xyz::openbmc_project::association::Definitions;

class Pcie :
    sdbusplus::server::object_t<PCIeSlot, location, embedded, item, association>
{
  public:
    Pcie() = delete;
    Pcie(const Pcie&) = delete;
    Pcie& operator=(const Pcie&) = delete;
    Pcie(Pcie&&) = delete;
    Pcie& operator=(Pcie&&) = delete;
    ~Pcie() = default;

    Pcie(sdbusplus::bus_t& bus, const std::string& objPath,
         const uint8_t& pcieId, uint8_t* smbiosTableStorage,
         const std::string& motherboard) :
        sdbusplus::server::object_t<PCIeSlot, location, embedded, item,
                                    association>(bus, objPath.c_str()),
        pcieNum(pcieId)
    {
        pcieInfoUpdate(smbiosTableStorage, motherboard);
    }

    void pcieInfoUpdate(uint8_t* smbiosTableStorage,
                        const std::string& motherboard);

  private:
    uint8_t pcieNum;
    uint8_t* storage;
    std::string motherboardPath;

    struct SystemSlotInfo
    {
        uint8_t type;
        uint8_t length;
        uint16_t handle;
        uint8_t slotDesignation;
        uint8_t slotType;
        uint8_t slotDataBusWidth;
        uint8_t currUsage;
        uint8_t slotLength;
        uint16_t slotID;
        uint8_t characteristics1;
        uint8_t characteristics2;
        uint16_t segGroupNum;
        uint8_t busNum;
        uint8_t deviceNum;
    } __attribute__((packed));

    void pcieGeneration(const uint8_t type);
    void pcieType(const uint8_t type);
    void pcieLaneSize(const uint8_t width);
    void pcieIsHotPluggable(const uint8_t characteristics);
    void pcieLocation(const uint8_t slotDesignation, const uint8_t structLen,
                      uint8_t* dataIn);
};

static const std::unordered_set<uint8_t> pcieSmbiosType = {
    0x09, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1c,
    0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1,
    0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd,
    0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6};

// Definition follow smbios spec DSP0134 3.4.0
static const std::map<uint8_t, PCIeGeneration> pcieGenerationTable = {
    {0x09, PCIeGeneration::Unknown}, {0x14, PCIeGeneration::Gen3},
    {0x15, PCIeGeneration::Gen3},    {0x16, PCIeGeneration::Gen3},
    {0x17, PCIeGeneration::Gen3},    {0x18, PCIeGeneration::Gen1},
    {0x19, PCIeGeneration::Gen1},    {0x1a, PCIeGeneration::Gen1},
    {0x1b, PCIeGeneration::Gen1},    {0x1c, PCIeGeneration::Gen1},
    {0x1d, PCIeGeneration::Gen3},    {0x1e, PCIeGeneration::Gen3},
    {0x1f, PCIeGeneration::Gen2},    {0x20, PCIeGeneration::Gen3},
    {0x21, PCIeGeneration::Gen1},    {0x22, PCIeGeneration::Gen1},
    {0x23, PCIeGeneration::Gen1},    {0x24, PCIeGeneration::Gen4},
    {0x25, PCIeGeneration::Gen5},    {0x26, PCIeGeneration::Unknown},
    {0x27, PCIeGeneration::Unknown}, {0x28, PCIeGeneration::Unknown},
    {0x29, PCIeGeneration::Unknown}, {0xa5, PCIeGeneration::Gen1},
    {0xa6, PCIeGeneration::Gen1},    {0xa7, PCIeGeneration::Gen1},
    {0xa8, PCIeGeneration::Gen1},    {0xa9, PCIeGeneration::Gen1},
    {0xaa, PCIeGeneration::Gen1},    {0xab, PCIeGeneration::Gen2},
    {0xac, PCIeGeneration::Gen2},    {0xad, PCIeGeneration::Gen2},
    {0xae, PCIeGeneration::Gen2},    {0xaf, PCIeGeneration::Gen2},
    {0xb0, PCIeGeneration::Gen2},    {0xb1, PCIeGeneration::Gen3},
    {0xb2, PCIeGeneration::Gen3},    {0xb3, PCIeGeneration::Gen3},
    {0xb4, PCIeGeneration::Gen3},    {0xb5, PCIeGeneration::Gen3},
    {0xb6, PCIeGeneration::Gen3},    {0xb8, PCIeGeneration::Gen4},
    {0xb9, PCIeGeneration::Gen4},    {0xba, PCIeGeneration::Gen4},
    {0xbb, PCIeGeneration::Gen4},    {0xbc, PCIeGeneration::Gen4},
    {0xbd, PCIeGeneration::Gen4},    {0xbe, PCIeGeneration::Gen5},
    {0xbf, PCIeGeneration::Gen5},    {0xc0, PCIeGeneration::Gen5},
    {0xc1, PCIeGeneration::Gen5},    {0xc2, PCIeGeneration::Gen5},
    {0xc3, PCIeGeneration::Gen5},    {0xc4, PCIeGeneration::Unknown},
    {0xc5, PCIeGeneration::Unknown}, {0xc6, PCIeGeneration::Unknown}};

static const std::map<uint8_t, PCIeType> pcieTypeTable = {
    {0x09, PCIeType::OEM},       {0x14, PCIeType::M_2},
    {0x15, PCIeType::M_2},       {0x16, PCIeType::M_2},
    {0x17, PCIeType::M_2},       {0x18, PCIeType::Unknown},
    {0x19, PCIeType::Unknown},   {0x1a, PCIeType::Unknown},
    {0x1b, PCIeType::Unknown},   {0x1c, PCIeType::Unknown},
    {0x1d, PCIeType::Unknown},   {0x1e, PCIeType::Unknown},
    {0xa8, PCIeType::Unknown},   {0xa9, PCIeType::Unknown},
    {0x1F, PCIeType::U_2},       {0x20, PCIeType::U_2},
    {0x21, PCIeType::Mini},      {0x22, PCIeType::Mini},
    {0x23, PCIeType::Mini},      {0x24, PCIeType::U_2},
    {0x25, PCIeType::U_2},       {0x26, PCIeType::OCP3Small},
    {0x27, PCIeType::OCP3Large}, {0x28, PCIeType::Unknown},
    {0x29, PCIeType::Unknown},   {0xa5, PCIeType::OEM},
    {0xa6, PCIeType::OEM},       {0xa7, PCIeType::OEM},
    {0xa8, PCIeType::OEM},       {0xa9, PCIeType::OEM},
    {0xaa, PCIeType::OEM},       {0xab, PCIeType::OEM},
    {0xac, PCIeType::OEM},       {0xad, PCIeType::OEM},
    {0xae, PCIeType::OEM},       {0xaf, PCIeType::OEM},
    {0xb0, PCIeType::OEM},       {0xb1, PCIeType::OEM},
    {0xb2, PCIeType::OEM},       {0xb3, PCIeType::OEM},
    {0xb4, PCIeType::OEM},       {0xb5, PCIeType::OEM},
    {0xb6, PCIeType::OEM},       {0xb8, PCIeType::OEM},
    {0xb9, PCIeType::OEM},       {0xba, PCIeType::OEM},
    {0xbb, PCIeType::OEM},       {0xbc, PCIeType::OEM},
    {0xbd, PCIeType::OEM},       {0xbe, PCIeType::OEM},
    {0xbf, PCIeType::OEM},       {0xc0, PCIeType::OEM},
    {0xc1, PCIeType::OEM},       {0xc2, PCIeType::OEM},
    {0xc3, PCIeType::OEM},       {0xc4, PCIeType::OEM},
    {0xc5, PCIeType::Unknown},   {0xc6, PCIeType::Unknown}};

const std::map<uint8_t, size_t> pcieLanesTable = {
    {0x08, 1}, {0x09, 2}, {0xa, 4}, {0xb, 8}, {0xc, 12}, {0xd, 16}, {0xe, 32}};

}; // namespace smbios

}; // namespace phosphor
