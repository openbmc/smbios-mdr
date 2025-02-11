#pragma once
#include "smbios_mdrv2.hpp"

#include <sdbusplus/asio/connection.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Tpm/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/Software/Version/server.hpp>

namespace phosphor
{

namespace smbios
{

using tpmIntf = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Tpm>;
using assetIntf = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::Asset>;
using itemIntf = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::inventory::Item>;
using softwareversionIntf = sdbusplus::server::object_t<
    sdbusplus::server::xyz::openbmc_project::software::Version>;
class Tpm : tpmIntf, assetIntf, itemIntf, softwareversionIntf
{
  public:
    Tpm() = delete;
    ~Tpm() = default;
    Tpm(const Tpm&) = delete;
    Tpm& operator=(const Tpm&) = delete;
    Tpm(Tpm&&) = default;
    Tpm& operator=(Tpm&&) = default;

    Tpm(std::shared_ptr<sdbusplus::asio::connection> bus,
        const std::string& objPath, uint8_t* smbiosTableStorage) :
        tpmIntf(*bus, objPath.c_str()), assetIntf(*bus, objPath.c_str()),
        itemIntf(*bus, objPath.c_str()),
        softwareversionIntf(*bus, objPath.c_str()), path(objPath),
        storage(smbiosTableStorage)
    {
        tpmInfoUpdate();
    }

    void tpmInfoUpdate(void);

  private:
    /** @brief Path of the group instance */
    std::string path;

    uint8_t* storage;
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
