#include "system_boot.hpp"

namespace phosphor
{
namespace smbios
{

void SystemBoot::infoUpdate()
{
    // get system path name
    std::string path = smbiosInventoryPathStr;

    // populate info to dbus
    objectInterface = server->add_interface(
        path, "xyz.openbmc_project.Inventory.Item.SystemBoot");
    objectInterface->register_property("StatusCode", bootInfoPtr->statusCode);
    objectInterface->register_property("BootCount", bootInfoPtr->bootCount);

    bool result = objectInterface->initialize();
    if (!result)
    {
        lg2::error("Failed to initialize System boot object");
    }
}

} // namespace smbios
} // namespace phosphor
