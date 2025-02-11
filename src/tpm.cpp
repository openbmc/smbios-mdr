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

void Tpm::tpmInfoUpdate(void)
{
    uint8_t* dataIn = storage;
    dataIn = getSMBIOSTypePtr(dataIn, tpmDeviceType);
    if (dataIn == nullptr)
    {
        return;
    }

    auto tpmInfo = reinterpret_cast<struct TPMInfo*>(dataIn);

    present(true);
    purpose(softwareversionIntf::VersionPurpose::Other);
    tpmVendor(tpmInfo);
    tpmFirmwareVersion(tpmInfo);
    tpmDescription(tpmInfo->description, tpmInfo->length, dataIn);
}

void Tpm::tpmVendor(const struct TPMInfo* tpmInfo)
{
    // Specified as four ASCII characters, as defined by TCG Vendor ID
    char vendorId[5];
    constexpr int vendorIdLength = 4;
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
    if (tpmInfo->specMajor == 0x01)
    {
        auto ver = reinterpret_cast<const struct TPMVersionSpec1*>(
            &tpmInfo->firmwareVersion1);
        stream << ver->revMajor << "." << ver->revMinor;
    }
    else if (tpmInfo->specMajor == 0x02)
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
