#include "tpm.hpp"

#include "mdrv2.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace phosphor
{
namespace smbios
{

void Tpm::tpmInfoUpdate(uint8_t* smbiosTableStorage,
                        const std::string& motherboard)
{
    storage = smbiosTableStorage;
    motherboardPath = motherboard;

    uint8_t* dataIn = storage;
    dataIn = getSMBIOSTypePtr(dataIn, tpmDeviceType);
    if (dataIn == nullptr)
    {
        return;
    }
    for (uint8_t index = 0; index < tpmId; index++)
    {
        dataIn = smbiosNextPtr(dataIn);
        if (dataIn == nullptr)
        {
            return;
        }
        dataIn = getSMBIOSTypePtr(dataIn, tpmDeviceType);
        if (dataIn == nullptr)
        {
            return;
        }
    }
    auto tpmInfo = reinterpret_cast<struct TPMInfo*>(dataIn);

    present(true);
    purpose(softwareversion::VersionPurpose::Other);
    tpmVendor(tpmInfo);
    tpmFirmwareVersion(tpmInfo);
    tpmDescription(tpmInfo->description, tpmInfo->length, dataIn);
    if (!motherboardPath.empty())
    {
        std::vector<std::tuple<std::string, std::string, std::string>> assocs;
        assocs.emplace_back("chassis", "trusted_components", motherboardPath);
        association::associations(assocs);
    }
}

void Tpm::tpmVendor(const struct TPMInfo* tpmInfo)
{
    constexpr int vendorIdLength = 4;
    // Specified as four ASCII characters, as defined by TCG Vendor ID
    char vendorId[vendorIdLength + 1];
    int i;
    for (i = 0; i < vendorIdLength && tpmInfo->vendor[i] != '\0'; i++)
    {
        if (std::isprint(tpmInfo->vendor[i]))
        {
            vendorId[i] = tpmInfo->vendor[i];
        }
        else
        {
            vendorId[i] = '.';
        }
    }
    vendorId[i] = '\0';
    manufacturer(vendorId);
}

void Tpm::tpmFirmwareVersion(const struct TPMInfo* tpmInfo)
{
    std::stringstream stream;

    if (tpmInfo->specMajor == tpmMajorVerion1)
    {
        auto ver = reinterpret_cast<const struct TPMVersionSpec1*>(
            &tpmInfo->firmwareVersion1);
        stream << ver->revMajor << "." << ver->revMinor;
    }
    else if (tpmInfo->specMajor == tpmMajorVerion2)
    {
        auto ver = reinterpret_cast<const struct TPMVersionSpec2*>(
            &tpmInfo->firmwareVersion1);
        stream << ver->revMajor << "." << ver->revMinor;
    }
    version(stream.str());
}

void Tpm::tpmDescription(const uint8_t positionNum, const uint8_t structLen,
                         uint8_t* dataIn)
{
    std::string result = positionToString(positionNum, structLen, dataIn);
    prettyName(result);
}
} // namespace smbios
} // namespace phosphor
