/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once
#include "cpu.hpp"
#include "dimm.hpp"
#include "pcieslot.hpp"
#include "smbios_mdrv2.hpp"
#include "system.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/container/flat_map.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/timer.hpp>
#include <xyz/openbmc_project/Smbios/MDR_V2/server.hpp>

sdbusplus::asio::object_server& getObjectServer(void);

using RecordVariant =
    std::variant<std::string, uint64_t, uint32_t, uint16_t, uint8_t>;
namespace phosphor
{
namespace smbios
{

static constexpr const char* mdrV2Path = "/xyz/openbmc_project/Smbios/MDR_V2";
static constexpr const char* smbiosPath = "/xyz/openbmc_project/Smbios";
static constexpr const char* smbiosInterfaceName =
    "xyz.openbmc_project.Smbios.GetRecordType";
static constexpr const char* mapperBusName = "xyz.openbmc_project.ObjectMapper";
static constexpr const char* mapperPath = "/xyz/openbmc_project/object_mapper";
static constexpr const char* mapperInterface =
    "xyz.openbmc_project.ObjectMapper";
static constexpr const char* systemInterfacePath =
    "/xyz/openbmc_project/inventory/system";
static constexpr const char* systemInterface =
    "xyz.openbmc_project.Inventory.Item.System";
constexpr const int limitEntryLen = 0xff;

class MDRV2 :
    sdbusplus::server::object_t<
        sdbusplus::server::xyz::openbmc_project::smbios::MDRV2>
{
  public:
    MDRV2() = delete;
    MDRV2(const MDRV2&) = delete;
    MDRV2& operator=(const MDRV2&) = delete;
    MDRV2(MDRV2&&) = delete;
    MDRV2& operator=(MDRV2&&) = delete;
    ~MDRV2() = default;

    MDRV2(sdbusplus::bus_t& bus, const char* path,
          boost::asio::io_context& io) :
        sdbusplus::server::object_t<
            sdbusplus::server::xyz::openbmc_project::smbios::MDRV2>(bus, path),
        bus(bus), timer(io), smbiosInterface(getObjectServer().add_interface(
                                 smbiosPath, smbiosInterfaceName))
    {

        smbiosDir.agentVersion = smbiosAgentVersion;
        smbiosDir.dirVersion = 1;
        smbiosDir.dirEntries = 1;
        directoryEntries(smbiosDir.dirEntries);
        smbiosDir.status = 1;
        smbiosDir.remoteDirVersion = 0;

        std::copy(smbiosTableId.begin(), smbiosTableId.end(),
                  smbiosDir.dir[smbiosDirIndex].common.id.dataInfo);

        smbiosDir.dir[smbiosDirIndex].dataStorage = smbiosTableStorage;

        agentSynchronizeData();

        smbiosInterface->register_method("GetRecordType", [this](size_t type) {
            return getRecordType(type);
        });
        smbiosInterface->initialize();
    }

    std::vector<uint8_t> getDirectoryInformation(uint8_t dirIndex) override;

    std::vector<uint8_t> getDataInformation(uint8_t idIndex) override;

    bool sendDirectoryInformation(uint8_t dirVersion, uint8_t dirIndex,
                                  uint8_t returnedEntries,
                                  uint8_t remainingEntries,
                                  std::vector<uint8_t> dirEntry) override;

    std::vector<uint8_t> getDataOffer() override;

    bool sendDataInformation(uint8_t idIndex, uint8_t flag, uint32_t dataLen,
                             uint32_t dataVer, uint32_t timeStamp) override;

    int findIdIndex(std::vector<uint8_t> dataInfo) override;

    bool agentSynchronizeData() override;

    std::vector<uint32_t>
        synchronizeDirectoryCommonData(uint8_t idIndex, uint32_t size) override;

    uint8_t directoryEntries(uint8_t value) override;

    std::vector<boost::container::flat_map<std::string, RecordVariant>>
        getRecordType(size_t type);

  private:
    boost::asio::steady_timer timer;

    sdbusplus::bus_t& bus;

    Mdr2DirStruct smbiosDir;

    bool readDataFromFlash(MDRSMBIOSHeader* mdrHdr, uint8_t* data);
    bool checkSMBIOSVersion(uint8_t* dataIn);

    const std::array<uint8_t, 16> smbiosTableId{
        40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 0x42};
    uint8_t smbiosTableStorage[smbiosTableStorageSize];

    bool smbiosIsUpdating(uint8_t index);
    bool smbiosIsAvailForUpdate(uint8_t index);
    inline uint8_t smbiosValidFlag(uint8_t index);
    void systemInfoUpdate(void);

    int getTotalCpuSlot(void);
    int getTotalDimmSlot(void);
    int getTotalPcieSlot(void);
    std::vector<std::unique_ptr<Cpu>> cpus;
    std::vector<std::unique_ptr<Dimm>> dimms;
    std::vector<std::unique_ptr<Pcie>> pcies;
    std::unique_ptr<System> system;
    std::shared_ptr<sdbusplus::asio::dbus_interface> smbiosInterface;
};

} // namespace smbios
} // namespace phosphor
