#pragma once
#include "smbios_mdrv2.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace phosphor
{

namespace smbios
{

class SystemBoot
{
  public:
    SystemBoot() = delete;
    SystemBoot(const SystemBoot&) = delete;
    SystemBoot& operator=(const SystemBoot&) = delete;
    SystemBoot(SystemBoot&&) = delete;
    SystemBoot& operator=(SystemBoot&&) = delete;
    ~SystemBoot() = default;

    SystemBoot(std::shared_ptr<sdbusplus::asio::object_server>& objServer,
               struct SystemBootInfo* bootInfo,
               const std::string& smbiosInventoryPath) :
        server(objServer), bootInfoPtr(bootInfo),
        smbiosInventoryPathStr(smbiosInventoryPath)
    {
        infoUpdate();
    }

    void infoUpdate();

  private:
    std::shared_ptr<sdbusplus::asio::object_server>& server;

    std::shared_ptr<sdbusplus::asio::dbus_interface> objectInterface;

    struct SystemBootInfo* bootInfoPtr;

    const std::string& smbiosInventoryPathStr;
};

} // namespace smbios

} // namespace phosphor
