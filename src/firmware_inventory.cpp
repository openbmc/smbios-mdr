#include "firmware_inventory.hpp"

#include "mdrv2.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace phosphor
{
namespace smbios
{
namespace utils
{
std::vector<std::string> getExistingVersionPaths(sdbusplus::bus_t& bus)
{
    std::vector<std::string> existingVersionPaths;

    auto getVersionPaths = bus.new_method_call(
        phosphor::smbios::mapperBusName, phosphor::smbios::mapperPath,
        phosphor::smbios::mapperInterface, "GetSubTreePaths");
    getVersionPaths.append(firmwarePath);
    getVersionPaths.append(0);
    getVersionPaths.append(
        std::array<std::string, 1>({phosphor::smbios::versionInterface}));

    try
    {
        sdbusplus::message_t reply = bus.call(getVersionPaths);
        reply.read(existingVersionPaths);
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cerr << "Failed to query version objects. ERROR=" << e.what()
                  << std::endl;
        existingVersionPaths.clear();
    }

    return existingVersionPaths;
}
} // namespace utils

void FirmwareInventory::firmwareInfoUpdate(uint8_t* smbiosTableStorage)
{
    uint8_t* dataIn = smbiosTableStorage;
    dataIn = getSMBIOSTypePtr(dataIn, firmwareInventoryInformationType);
    if (dataIn == nullptr)
    {
        return;
    }
    for (uint8_t index = 0; index < firmwareInventoryIndex; index++)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return;
        }
        dataIn = getSMBIOSTypePtr(dataIn, firmwareInventoryInformationType);
        if (dataIn == nullptr)
        {
            return;
        }
    }
    auto firmwareInfo = reinterpret_cast<struct FirmwareInfo*>(dataIn);

    firmwareComponentName(firmwareInfo->componentName, firmwareInfo->length,
                          dataIn);
    firmwareVersion(firmwareInfo->version, firmwareInfo->length, dataIn);
    firmwareId(firmwareInfo->id, firmwareInfo->length, dataIn);
    firmwareReleaseDate(firmwareInfo->releaseDate, firmwareInfo->length,
                        dataIn);
    firmwareManufacturer(firmwareInfo->manufacturer, firmwareInfo->length,
                         dataIn);
    present(true);
    purpose(softwareversionIntf::VersionPurpose::Other);

    std::vector<std::tuple<std::string, std::string, std::string>> assocs;
    assocs.emplace_back("software_version", "functional",
                        "/xyz/openbmc_project/software");
    association::associations(assocs);
}

std::string FirmwareInventory::checkAndCreateFirmwarePath(
    uint8_t* dataIn, int inventoryIndex,
    std::vector<std::string>& existingVersionPaths)
{
    std::string firmwareId;
    auto firmwarePtr =
        getSMBIOSTypePtr(dataIn, firmwareInventoryInformationType);
    if (firmwarePtr == nullptr)
    {
        return firmwareId;
    }
    for (uint8_t index = 0; index < inventoryIndex; index++)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return "";
        }
        dataIn = getSMBIOSTypePtr(dataIn, firmwareInventoryInformationType);
        if (dataIn == nullptr)
        {
            return "";
        }
    }
    auto firmwareInfo = reinterpret_cast<struct FirmwareInfo*>(firmwarePtr);
    firmwareId =
        positionToString(firmwareInfo->id, firmwareInfo->length, firmwarePtr);
    auto firmwareName = positionToString(firmwareInfo->componentName,
                                         firmwareInfo->length, firmwarePtr);
    if (firmwareInfo->numOfAssociatedComponents > 0)
    {
        for (int i = 0; i < firmwareInfo->numOfAssociatedComponents; i++)
        {
            auto component = smbiosHandlePtr(
                dataIn, firmwareInfo->associatedComponentHandles[i]);
            if (component == nullptr)
            {
                continue;
            }

            auto header = reinterpret_cast<struct StructureHeader*>(component);
            switch (header->type)
            {
                case processorsType:
                case systemSlots:
                case onboardDevicesExtended:
                {
                    auto designation = positionToString(
                        component[4], header->length, component);
                    if (!designation.empty())
                    {
                        firmwareId.append("_").append(designation);
                    }
                    break;
                }
                case systemPowerSupply:
                {
                    auto location = positionToString(component[5],
                                                     header->length, component);
                    if (!location.empty())
                    {
                        firmwareId.append("_").append(location);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    if (firmwareId.empty())
    {
        firmwareId = "firmware" + std::to_string(inventoryIndex);
    }
    firmwareId =
        std::regex_replace(firmwareId, std::regex("[^a-zA-Z0-9_/]+"), "_");

    auto eqObjName = [firmwareId](std::string s) {
        std::filesystem::path p(s);
        return p.filename().compare(firmwareId) == 0;
    };
    if (std::find_if(existingVersionPaths.begin(), existingVersionPaths.end(),
                     std::move(eqObjName)) != existingVersionPaths.end())
    {
        return "";
    }
    std::string path = firmwarePath;
    path.append("/").append(firmwareId);
    return path;
}

void FirmwareInventory::firmwareComponentName(
    const uint8_t positionNum, const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    prettyName(result);
}

void FirmwareInventory::firmwareVersion(
    const uint8_t positionNum, const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    version(result);
}

void FirmwareInventory::firmwareId(const uint8_t positionNum,
                                   const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    softwareId(result);
}

void FirmwareInventory::firmwareReleaseDate(
    const uint8_t positionNum, const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    buildDate(result);
}

void FirmwareInventory::firmwareManufacturer(
    const uint8_t positionNum, const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    manufacturer(result);
}
} // namespace smbios
} // namespace phosphor
