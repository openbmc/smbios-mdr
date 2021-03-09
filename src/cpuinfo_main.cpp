/*
// Copyright (c) 2020 Intel Corporation
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

#include "cpuinfo.hpp"
#include "cpuinfo_utils.hpp"
#include "speed_select.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include <iostream>
#include <optional>
#include <sstream>
#include <string>

extern "C"
{
#include <i2c/smbus.h>
#include <linux/i2c-dev.h>
}

#include <peci.h>

#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace cpu_info
{
static constexpr bool debug = false;
static constexpr const char* cpuInterfaceName =
    "xyz.openbmc_project.Inventory.Decorator.Asset";
static constexpr const char* cpuProcessName =
    "xyz.openbmc_project.Smbios.MDR_V2";

// constants for reading SSPEC or QDF string from PIROM
// Currently, they are the same for platforms with icx
static constexpr uint8_t defaultI2cBus = 13;
static constexpr uint8_t defaultI2cSlaveAddr0 = 0x50;
static constexpr uint8_t sspecRegAddr = 0xd;
static constexpr uint8_t sspecSize = 6;

using CPUInfoMap = boost::container::flat_map<size_t, std::shared_ptr<CPUInfo>>;

static CPUInfoMap cpuInfoMap = {};

static boost::container::flat_map<size_t,
                                  std::unique_ptr<sdbusplus::bus::match_t>>
    cpuUpdatedMatch = {};

static std::optional<std::string> readSSpec(uint8_t bus, uint8_t slaveAddr,
                                            uint8_t regAddr, size_t count)
{
    unsigned long funcs = 0;
    std::string devPath = "/dev/i2c-" + std::to_string(bus);

    int fd = ::open(devPath.c_str(), O_RDWR);
    if (fd < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error in open!",
            phosphor::logging::entry("PATH=%s", devPath.c_str()),
            phosphor::logging::entry("SLAVEADDR=0x%x", slaveAddr));
        return std::nullopt;
    }

    if (::ioctl(fd, I2C_FUNCS, &funcs) < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error in I2C_FUNCS!",
            phosphor::logging::entry("PATH=%s", devPath.c_str()),
            phosphor::logging::entry("SLAVEADDR=0x%x", slaveAddr));
        ::close(fd);
        return std::nullopt;
    }

    if (!(funcs & I2C_FUNC_SMBUS_READ_BYTE_DATA))
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "i2c bus does not support read!",
            phosphor::logging::entry("PATH=%s", devPath.c_str()),
            phosphor::logging::entry("SLAVEADDR=0x%x", slaveAddr));
        ::close(fd);
        return std::nullopt;
    }

    if (::ioctl(fd, I2C_SLAVE_FORCE, slaveAddr) < 0)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Error in I2C_SLAVE_FORCE!",
            phosphor::logging::entry("PATH=%s", devPath.c_str()),
            phosphor::logging::entry("SLAVEADDR=0x%x", slaveAddr));
        ::close(fd);
        return std::nullopt;
    }

    int value = 0;
    std::string sspec;
    sspec.reserve(count);

    for (size_t i = 0; i < count; i++)
    {
        value = ::i2c_smbus_read_byte_data(fd, regAddr + i);
        if (value < 0)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Error in i2c read!",
                phosphor::logging::entry("PATH=%s", devPath.c_str()),
                phosphor::logging::entry("SLAVEADDR=0x%x", slaveAddr));
            ::close(fd);
            return std::nullopt;
        }
        if (!std::isprint(static_cast<unsigned char>(value)))
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Non printable value in sspec, ignored.");
            continue;
        }
        // sspec always starts with S,
        // if not assume it is QDF string which starts at offset 2
        if (i == 0 && static_cast<unsigned char>(value) != 'S')
        {
            i = 1;
            continue;
        }
        sspec.push_back(static_cast<unsigned char>(value));
    }
    ::close(fd);
    return sspec;
}

static void setAssetProperty(
    const std::shared_ptr<sdbusplus::asio::connection>& conn, const int& cpu,
    const std::vector<std::pair<std::string, std::string>>& propValues)
{
    // cpuId from configuration is one based as
    // dbus object path used by smbios is 0 based
    const std::string objectPath = cpuPath + std::to_string(cpu - 1);
    for (const auto& prop : propValues)
    {
        conn->async_method_call(
            [](const boost::system::error_code ec) {
                if (ec)
                {
                    phosphor::logging::log<phosphor::logging::level::ERR>(
                        "Cannot get CPU property!");
                    return;
                }
            },
            cpuProcessName, objectPath.c_str(),
            "org.freedesktop.DBus.Properties", "Set", cpuInterfaceName,
            prop.first.c_str(), std::variant<std::string>{prop.second});
    }
}

static void createCpuUpdatedMatch(
    const std::shared_ptr<sdbusplus::asio::connection>& conn, const int cpu,
    const std::vector<std::pair<std::string, std::string>>& propValues)
{
    if (cpuUpdatedMatch[cpu])
    {
        return;
    }

    const std::string objectPath = cpuPath + std::to_string(cpu - 1);

    cpuUpdatedMatch.insert_or_assign(
        cpu,
        std::make_unique<sdbusplus::bus::match::match>(
            static_cast<sdbusplus::bus::bus&>(*conn),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(0, objectPath.c_str()),
            [conn, cpu, propValues](sdbusplus::message::message& msg) {
                sdbusplus::message::object_path objectName;
                boost::container::flat_map<
                    std::string,
                    boost::container::flat_map<
                        std::string, std::variant<std::string, uint64_t>>>
                    msgData;

                msg.read(objectName, msgData);

                // Check for xyz.openbmc_project.Inventory.Item.Cpu
                // interface match
                const auto& intfFound = msgData.find(cpuInterfaceName);
                if (msgData.end() != intfFound)
                {
                    setAssetProperty(conn, cpu, propValues);
                }
            }));
}

static void
    getProcessorInfo(boost::asio::io_service& io,
                     const std::shared_ptr<sdbusplus::asio::connection>& conn,
                     const size_t& cpu)
{
    if (cpuInfoMap.find(cpu) == cpuInfoMap.end() || cpuInfoMap[cpu] == nullptr)
    {
        std::cerr << "No information found for cpu " << cpu << "\n";
        return;
    }

    if (cpuInfoMap[cpu]->id != cpu)
    {
        std::cerr << "Incorrect CPU id " << (unsigned)cpuInfoMap[cpu]->id
                  << " expect " << cpu << "\n";
        return;
    }

    uint8_t cpuAddr = cpuInfoMap[cpu]->peciAddr;
    uint8_t i2cBus = cpuInfoMap[cpu]->i2cBus;
    uint8_t i2cDevice = cpuInfoMap[cpu]->i2cDevice;

    uint8_t cc = 0;
    CPUModel model{};
    uint8_t stepping = 0;

    // Wait for POST to complete to ensure that BIOS has time to enable the
    // PPIN. Before BIOS enables it, we would get a 0x90 CC on PECI.
    if (hostState != HostState::PostComplete
        || peci_GetCPUID(cpuAddr, &model, &stepping, &cc) != PECI_CC_SUCCESS)
    {
        // Start the PECI check loop
        auto waitTimer = std::make_shared<boost::asio::steady_timer>(io);
        waitTimer->expires_after(
            std::chrono::seconds(cpu_info::peciCheckInterval));

        waitTimer->async_wait(
            [waitTimer, &io, conn, cpu](const boost::system::error_code& ec) {
                if (ec)
                {
                    // operation_aborted is expected if timer is canceled
                    // before completion.
                    if (ec != boost::asio::error::operation_aborted)
                    {
                        phosphor::logging::log<phosphor::logging::level::ERR>(
                            "info update timer async_wait failed ",
                            phosphor::logging::entry("EC=0x%x", ec.value()));
                    }
                    return;
                }
                getProcessorInfo(io, conn, cpu);
            });
        return;
    }

    switch (model)
    {
        case icx:
        {
            // PPIN can be read through PCS 19
            static constexpr uint8_t u8Size = 4; // default to a DWORD
            static constexpr uint8_t u8PPINPkgIndex = 19;
            static constexpr uint16_t u16PPINPkgParamHigh = 2;
            static constexpr uint16_t u16PPINPkgParamLow = 1;
            uint64_t cpuPPIN = 0;
            uint32_t u32PkgValue = 0;

            int ret =
                peci_RdPkgConfig(cpuAddr, u8PPINPkgIndex, u16PPINPkgParamLow,
                                 u8Size, (uint8_t*)&u32PkgValue, &cc);
            if (0 != ret)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "peci read package config failed at address",
                    phosphor::logging::entry("PECIADDR=0x%x",
                                             (unsigned)cpuAddr),
                    phosphor::logging::entry("CC=0x%x", cc));
                u32PkgValue = 0;
            }

            cpuPPIN = u32PkgValue;
            ret = peci_RdPkgConfig(cpuAddr, u8PPINPkgIndex, u16PPINPkgParamHigh,
                                   u8Size, (uint8_t*)&u32PkgValue, &cc);
            if (0 != ret)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "peci read package config failed at address",
                    phosphor::logging::entry("PECIADDR=0x%x",
                                             (unsigned)cpuAddr),
                    phosphor::logging::entry("CC=0x%x", cc));
                cpuPPIN = 0;
                u32PkgValue = 0;
            }

            cpuPPIN |= static_cast<uint64_t>(u32PkgValue) << 32;

            std::vector<std::pair<std::string, std::string>> values;

            // set SerialNumber if cpuPPIN is valid
            if (0 != cpuPPIN)
            {
                std::stringstream stream;
                stream << std::hex << cpuPPIN;
                std::string serialNumber(stream.str());
                // cpuInfo->serialNumber(serialNumber);
                values.emplace_back(
                    std::make_pair("SerialNumber", serialNumber));
            }

            std::optional<std::string> sspec =
                readSSpec(i2cBus, i2cDevice, sspecRegAddr, sspecSize);

            // cpuInfo->model(sspec.value_or(""));
            values.emplace_back(std::make_pair("Model", sspec.value_or("")));

            /// \todo in followup patch
            // CPUInfo is created by this service
            // update the below logic, which is needed because smbios
            // service creates the cpu object
            createCpuUpdatedMatch(conn, cpu, values);
            setAssetProperty(conn, cpu, values);
            break;
        }
        default:
            phosphor::logging::log<phosphor::logging::level::INFO>(
                "in-compatible cpu for cpu asset info");
            break;
    }
}

/**
 * Get cpu and pirom address
 */
static void
    getCpuAddress(boost::asio::io_service& io,
                  const std::shared_ptr<sdbusplus::asio::connection>& conn,
                  const std::string& service, const std::string& object,
                  const std::string& interface)
{
    conn->async_method_call(
        [&io, conn](boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        std::variant<std::string, uint64_t, uint32_t, uint16_t,
                                     std::vector<std::string>>>& properties) {
            const uint64_t* value = nullptr;
            uint8_t peciAddress = 0;
            uint8_t i2cBus = defaultI2cBus;
            uint8_t i2cDevice;
            bool i2cDeviceFound = false;
            size_t cpu = 0;

            if (ec)
            {
                std::cerr << "DBUS response error " << ec.value() << ": "
                          << ec.message() << "\n";
                return;
            }

            for (const auto& property : properties)
            {
                std::cerr << "property " << property.first << "\n";
                if (property.first == "Address")
                {
                    value = std::get_if<uint64_t>(&property.second);
                    if (value != nullptr)
                    {
                        peciAddress = static_cast<uint8_t>(*value);
                    }
                }
                if (property.first == "CpuID")
                {
                    value = std::get_if<uint64_t>(&property.second);
                    if (value != nullptr)
                    {
                        cpu = static_cast<size_t>(*value);
                    }
                }
                if (property.first == "PiromI2cAddress")
                {
                    value = std::get_if<uint64_t>(&property.second);
                    if (value != nullptr)
                    {
                        i2cDevice = static_cast<uint8_t>(*value);
                        i2cDeviceFound = true;
                    }
                }
                if (property.first == "PiromI2cBus")
                {
                    value = std::get_if<uint64_t>(&property.second);
                    if (value != nullptr)
                    {
                        i2cBus = static_cast<uint8_t>(*value);
                    }
                }
            }

            ///\todo replace this with present + power state
            if (cpu != 0 && peciAddress != 0)
            {
                if (!i2cDeviceFound)
                {
                    i2cDevice = defaultI2cSlaveAddr0 + cpu - 1;
                }
                cpuInfoMap.insert_or_assign(
                    cpu, std::make_shared<CPUInfo>(cpu, peciAddress, i2cBus,
                                                   i2cDevice));

                getProcessorInfo(io, conn, cpu);
            }
        },
        service, object, "org.freedesktop.DBus.Properties", "GetAll",
        interface);
}

/**
 * D-Bus client: to get platform specific configs
 */
static void getCpuConfiguration(
    boost::asio::io_service& io,
    const std::shared_ptr<sdbusplus::asio::connection>& conn,
    sdbusplus::asio::object_server& objServer)
{
    // Get the Cpu configuration
    // In case it's not available, set a match for it
    static std::unique_ptr<sdbusplus::bus::match::match> cpuConfigMatch =
        std::make_unique<sdbusplus::bus::match::match>(
            *conn,
            "type='signal',interface='org.freedesktop.DBus.Properties',member='"
            "PropertiesChanged',arg0='xyz.openbmc_project."
            "Configuration.XeonCPU'",
            [&io, conn, &objServer](sdbusplus::message::message& msg) {
                std::cerr << "get cpu configuration match\n";
                static boost::asio::steady_timer filterTimer(io);
                filterTimer.expires_after(
                    std::chrono::seconds(configCheckInterval));

                filterTimer.async_wait(
                    [&io, conn,
                     &objServer](const boost::system::error_code& ec) {
                        if (ec == boost::asio::error::operation_aborted)
                        {
                            return; // we're being canceled
                        }
                        else if (ec)
                        {
                            std::cerr << "Error: " << ec.message() << "\n";
                            return;
                        }
                        getCpuConfiguration(io, conn, objServer);
                    });
            });

    conn->async_method_call(
        [&io, conn](
            boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if constexpr (debug)
                std::cerr << "async_method_call callback\n";

            if (ec)
            {
                std::cerr << "error with async_method_call\n";
                return;
            }
            if (subtree.empty())
            {
                // No config data yet, so wait for the match
                return;
            }

            for (const auto& object : subtree)
            {
                for (const auto& service : object.second)
                {
                    getCpuAddress(io, conn, service.first, object.first,
                                  "xyz.openbmc_project.Configuration.XeonCPU");
                }
            }
            if constexpr (debug)
                std::cerr << "getCpuConfiguration callback complete\n";

            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Configuration.XeonCPU"});
}

} // namespace cpu_info

int main(int argc, char* argv[])
{
    // setup connection to dbus
    boost::asio::io_service io;
    std::shared_ptr<sdbusplus::asio::connection> conn =
        std::make_shared<sdbusplus::asio::connection>(io);

    // CPUInfo Object
    conn->request_name(cpu_info::cpuInfoObject);
    sdbusplus::asio::object_server server =
        sdbusplus::asio::object_server(conn);
    sdbusplus::bus::bus& bus = static_cast<sdbusplus::bus::bus&>(*conn);
    sdbusplus::server::manager::manager objManager(
        bus, "/xyz/openbmc_project/inventory");

    cpu_info::hostStateSetup(conn);

    cpu_info::sst::init(io, conn);

    // shared_ptr conn is global for the service
    // const reference of conn is passed to async calls
    cpu_info::getCpuConfiguration(io, conn, server);

    io.run();

    return 0;
}
