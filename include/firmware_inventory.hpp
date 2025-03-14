#pragma once
#include "smbios_mdrv2.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/Software/ExtendedVersion/server.hpp>
#include <xyz/openbmc_project/Software/Version/server.hpp>

#include <vector>

namespace phosphor
{

namespace smbios
{
namespace utils
{
std::vector<std::string> getExistingVersionPaths(sdbusplus::bus_t& bus);
}

using association =
    sdbusplus::server::xyz::openbmc_project::association::Definitions;
using asset =
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::Asset;
using Item = sdbusplus::server::xyz::openbmc_project::inventory::Item;
using softwareVersion =
    sdbusplus::server::xyz::openbmc_project::software::Version;
using softwareExtendedVersion =
    sdbusplus::server::xyz::openbmc_project::software::ExtendedVersion;

class FirmwareInventory :
    sdbusplus::server::object_t<asset, Item, association, softwareVersion,
                                softwareExtendedVersion>
{
  public:
    FirmwareInventory() = delete;
    ~FirmwareInventory() = default;
    FirmwareInventory(const FirmwareInventory&) = delete;
    FirmwareInventory& operator=(const FirmwareInventory&) = delete;
    FirmwareInventory(FirmwareInventory&&) = default;
    FirmwareInventory& operator=(FirmwareInventory&&) = default;

    FirmwareInventory(sdbusplus::bus_t& bus, const std::string& objPath,
                      const uint8_t index, uint8_t* smbiosTableStorage) :
        sdbusplus::server::object_t<asset, Item, association, softwareVersion,
                                    softwareExtendedVersion>(bus,
                                                             objPath.c_str()),
        firmwareInventoryIndex(index), storage(smbiosTableStorage)
    {
        firmwareInfoUpdate(smbiosTableStorage);
    }

    void firmwareInfoUpdate(uint8_t* smbiosTableStorage);

    static std::string checkAndCreateFirmwarePath(
        uint8_t* dataIn, int index,
        std::vector<std::string>& existingVersionPaths);

  private:
    int firmwareInventoryIndex;

    uint8_t* storage;

    struct FirmwareInfo
    {
        uint8_t type;
        uint8_t length;
        uint16_t handle;
        uint8_t componentName;
        uint8_t version;
        uint8_t versionFormat;
        uint8_t id;
        uint8_t idFormat;
        uint8_t releaseDate;
        uint8_t manufacturer;
        uint8_t lowestSupportedVersion;
        uint64_t imageSize;
        uint16_t characteristics;
        uint8_t state;
        uint8_t numOfAssociatedComponents;
        uint16_t associatedComponentHandles[1];
    } __attribute__((packed));

    void firmwareId(const uint8_t positionNum, const uint8_t structLen,
                    uint8_t* dataIn);
    void firmwareVersion(const uint8_t positionNum, const uint8_t structLen,
                         uint8_t* dataIn);
    void firmwareReleaseDate(const uint8_t positionNum, const uint8_t structLen,
                             uint8_t* dataIn);
    void firmwareManufacturer(const uint8_t positionNum,
                              const uint8_t structLen, uint8_t* dataIn);
    void firmwareComponentName(const uint8_t positionNum,
                               const uint8_t structLen, uint8_t* dataIn);
};
} // namespace smbios
} // namespace phosphor
