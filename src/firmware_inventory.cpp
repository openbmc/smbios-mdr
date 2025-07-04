#include "firmware_inventory.hpp"

#include "mdrv2.hpp"

#include <boost/algorithm/string.hpp>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
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
        auto reply = bus.call(getVersionPaths);
        reply.read(existingVersionPaths);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Failed to query version objects. ERROR={E}", "E", e.what());
        existingVersionPaths.clear();
    }

    return existingVersionPaths;
}
} // namespace utils

bool FirmwareInventory::getFirmwareInventoryData(uint8_t*& dataIn,
                                                 int inventoryIndex)
{
    dataIn = getSMBIOSTypePtr(dataIn, firmwareInventoryInformationType);
    if (dataIn == nullptr)
    {
        return false;
    }
    for (uint8_t index = 0; index < inventoryIndex; index++)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return false;
        }
        dataIn = getSMBIOSTypePtr(dataIn, firmwareInventoryInformationType);
        if (dataIn == nullptr)
        {
            return false;
        }
    }
    return true;
}

void FirmwareInventory::firmwareInfoUpdate(uint8_t* smbiosTableStorage)
{
    uint8_t* dataIn = smbiosTableStorage;
    if (!getFirmwareInventoryData(dataIn, firmwareInventoryIndex))
    {
        lg2::info("Failed to get data for firmware inventory index {I}", "I",
                  firmwareInventoryIndex);
        return;
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
    purpose(softwareVersion::VersionPurpose::Other);
}

std::string FirmwareInventory::checkAndCreateFirmwarePath(
    uint8_t* dataIn, int inventoryIndex,
    std::vector<std::string>& existingVersionPaths)
{
    if (!getFirmwareInventoryData(dataIn, inventoryIndex))
    {
        lg2::info("Failed to get data for firmware inventory index {I}", "I",
                  inventoryIndex);
        return "";
    }
    auto firmwareInfo = reinterpret_cast<struct FirmwareInfo*>(dataIn);
    std::string firmwareId =
        positionToString(firmwareInfo->id, firmwareInfo->length, dataIn);
    auto firmwareName = positionToString(firmwareInfo->componentName,
                                         firmwareInfo->length, dataIn);
    std::string firmwareObjPath = "";
#ifdef EXPOSE_FW_COMPONENT_NAME
    firmwareObjPath = firmwareName;
#else
    firmwareObjPath = firmwareId;
#endif
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
                        firmwareObjPath.append("_").append(designation);
                    }
                    break;
                }
                case systemPowerSupply:
                {
                    auto location = positionToString(component[5],
                                                     header->length, component);
                    if (!location.empty())
                    {
                        firmwareObjPath.append("_").append(location);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    if (firmwareObjPath.empty())
    {
        firmwareObjPath = "firmware" + std::to_string(inventoryIndex);
    }
    boost::algorithm::trim_right(firmwareObjPath);
    firmwareObjPath =
        std::regex_replace(firmwareObjPath, std::regex("[^a-zA-Z0-9_/]+"), "_");

    auto eqObjName = [firmwareObjPath](std::string s) {
        std::filesystem::path p(s);
        return p.filename().compare(firmwareObjPath) == 0;
    };
    if (std::find_if(existingVersionPaths.begin(), existingVersionPaths.end(),
                     std::move(eqObjName)) != existingVersionPaths.end())
    {
        return "";
    }
    std::string path = firmwarePath;
    path.append("/").append(firmwareObjPath);
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
    extendedVersion(result);
}

void FirmwareInventory::firmwareReleaseDate(
    const uint8_t positionNum, const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    releaseDate(result);
}

void FirmwareInventory::firmwareManufacturer(
    const uint8_t positionNum, const uint8_t structLen, uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    manufacturer(result);
}
} // namespace smbios
} // namespace phosphor
