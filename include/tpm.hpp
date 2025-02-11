#pragma once
#include "smbios_mdrv2.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Tpm/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/Software/Version/server.hpp>

namespace phosphor
{

namespace smbios
{

using tpm = sdbusplus::server::xyz::openbmc_project::inventory::item::Tpm;
using asset =
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::Asset;
using Item = sdbusplus::server::xyz::openbmc_project::inventory::Item;
using softwareversion =
    sdbusplus::server::xyz::openbmc_project::software::Version;
using association =
    sdbusplus::server::xyz::openbmc_project::association::Definitions;

constexpr uint8_t tpmMajorVerion1 = 0x01;
constexpr uint8_t tpmMajorVerion2 = 0x02;

class Tpm :
    sdbusplus::server::object_t<tpm, asset, Item, association, softwareversion>
{
  public:
    Tpm() = delete;
    ~Tpm() = default;
    Tpm(const Tpm&) = delete;
    Tpm& operator=(const Tpm&) = delete;
    Tpm(Tpm&&) = default;
    Tpm& operator=(Tpm&&) = default;

    Tpm(sdbusplus::bus_t& bus, const std::string& objPath, const uint8_t tpmID,
        uint8_t* smbiosTableStorage, const std::string& motherboard) :
        sdbusplus::server::object_t<tpm, asset, Item, association,
                                    softwareversion>(bus, objPath.c_str()),
        tpmId(tpmID), storage(smbiosTableStorage), motherboardPath(motherboard)
    {
        tpmInfoUpdate(smbiosTableStorage, motherboard);
    }

    void tpmInfoUpdate(uint8_t* smbiosTableStorage,
                       const std::string& motherboard);

  private:
    uint8_t tpmId;

    uint8_t* storage;

    std::string motherboardPath;
    struct TPMInfo
    {
        uint8_t type;
        uint8_t length;
        uint16_t handle;
        char vendor[4];
        uint8_t specMajor;
        uint8_t specMinor;
        uint32_t firmwareVersion1;
        uint32_t firmwareVersion2;
        uint8_t description;
        uint64_t characteristics;
        uint32_t oem;
    } __attribute__((packed));

    struct TPMVersionSpec1
    {
        uint8_t specMajor;
        uint8_t specMinor;
        uint8_t revMajor;
        uint8_t revMinor;
    } __attribute__((packed));

    struct TPMVersionSpec2
    {
        uint16_t revMinor;
        uint16_t revMajor;
    } __attribute__((packed));

    void tpmVendor(const struct TPMInfo* tpmInfo);
    void tpmFirmwareVersion(const struct TPMInfo* tpmInfo);
    void tpmDescription(const uint8_t positionNum, const uint8_t structLen,
                        uint8_t* dataIn);
};

} // namespace smbios

} // namespace phosphor
