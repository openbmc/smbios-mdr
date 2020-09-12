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

namespace phosphor
{
namespace cpu_info
{

static constexpr const char* cpuPath =
    "/xyz/openbmc_project/inventory/system/chassis/motherboard/cpu";
static constexpr const char* cpuInterfaceName =
    "xyz.openbmc_project.Inventory.Decorator.Asset";
static constexpr const char* cpuProcessName =
    "xyz.openbmc_project.Smbios.MDR_V2";

// constants for reading QDF string from PIROM
// Currently, they are the same for platforms with icx
// \todo: move into configuration file to be more robust
static constexpr uint8_t defaultI2cBus = 13;
static constexpr uint8_t defaultI2cSlaveAddr0 = 0x50;
static constexpr uint8_t sspecRegAddr = 0xf;
static constexpr uint8_t sspecSize = 6;
static constexpr uint8_t qdfSize = 4;

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
        value = ::i2c_smbus_read_byte_data(fd, regAddr++);
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
        sspec.push_back(static_cast<unsigned char>(value));
    }
    ::close(fd);
    return sspec;
}

static void setAssetProperty(
    std::shared_ptr<sdbusplus::asio::connection>& conn, const int& cpu,
    const std::vector<std::pair<std::string, std::string>>& propValues)
{
    // cpuId from configuration is one based as
    // dbus object path used by smbios is 0 based
    const std::string objectPath = cpuPath + std::to_string(cpu - 1);
    for (const auto& prop : propValues)
    {
        conn->async_method_call(
            [](const boost::system::error_code ec) {
                // Use "Set" method to set the property value.
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
    std::shared_ptr<sdbusplus::asio::connection>& conn, const int& cpu,
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
            [&conn, cpu, propValues](sdbusplus::message::message& msg) {
                std::string objectName;
                boost::container::flat_map<
                    std::string,
                    boost::container::flat_map<
                        std::string, std::variant<std::string, uint64_t>>>
                    msgData;

                msg.read(objectName, msgData);

                // Check for xyz.openbmc_project.Inventory.Item.Cpu
                // interface match
                auto intfFound = msgData.find(cpuInterfaceName);
                if (msgData.end() != intfFound)
                {
                    setAssetProperty(conn, cpu, propValues);
                }
            }));
}

static void getProcessorInfo(boost::asio::io_service& io,
                             std::shared_ptr<sdbusplus::asio::connection>& conn,
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

    bool success = false;
    if (peci_Ping(cpuAddr) == PECI_CC_SUCCESS)
    {
        uint8_t cc = 0;
        CPUModel model{};
        uint8_t stepping = 0;

        /// \todo in a follwup patch
        // CPUInfo will be updated as the centrol place for CPU information
        // std::shared_ptr<CPUInfo> cpuInfo =
        //    createCPUInfo(conn, cpu.second.first);
        // cpu.second.second = cpuInfo;
        if (peci_GetCPUID(cpuAddr, &model, &stepping, &cc) == PECI_CC_SUCCESS)
        {
            switch (model)
            {
                case icx:
                case icxd:
                case spr:
                {
                    // get processor ID
                    static constexpr uint8_t u8Size = 4; // default to a DWORD
                    static constexpr uint8_t u8PPINPkgIndex = 19;
                    static constexpr uint16_t u16PPINPkgParamHigh = 2;
                    static constexpr uint16_t u16PPINPkgParamLow = 1;
                    uint64_t cpuPPIN = 0;
                    uint32_t u32PkgValue = 0;

                    int ret = peci_RdPkgConfig(cpuAddr, u8PPINPkgIndex,
                                               u16PPINPkgParamLow, u8Size,
                                               (uint8_t*)&u32PkgValue, &cc);
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
                    ret = peci_RdPkgConfig(cpuAddr, u8PPINPkgIndex,
                                           u16PPINPkgParamHigh, u8Size,
                                           (uint8_t*)&u32PkgValue, &cc);
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

                    // assuming the slaveAddress will be incrementing like peci
                    // client address
                    std::optional<std::string> sspec =
                        readSSpec(i2cBus, i2cDevice, sspecRegAddr, sspecSize);
                    // QDF is 4 char length
                    if (sspec && sspec.value()[0] == 'Q')
                    {
                        sspec.value().resize(qdfSize);
                    }
                    // cpuInfo->model(sspec.value_or(""));
                    values.emplace_back(
                        std::make_pair("Model", sspec.value_or("")));

                    /// \todo in followup patch
                    // CPUInfo is created by this service
                    // update the below logic, which is needed because smbios
                    // service creates the cpu object
                    // cpuInfo creates dbus objects now, no need to use match
                    createCpuUpdatedMatch(conn, cpu, values);
                    setAssetProperty(conn, cpu, values);
                    break;
                }
                default:
                    phosphor::logging::log<phosphor::logging::level::INFO>(
                        "in-compatible cpu for cpu asset info");
                    break;
            }
            success = true;
        }
        else
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Cannot get CPUID!",
                phosphor::logging::entry("PECIADDR=0x%x", (unsigned)cpuAddr));
        }
    }

    if (!success)
    {
        // Start the PECI check loop
        auto waitTimer = std::make_shared<boost::asio::steady_timer>(io);
        waitTimer->expires_after(
            std::chrono::seconds(phosphor::cpu_info::peciCheckInterval));

        waitTimer->async_wait(
            [waitTimer, &io, &conn, cpu](const boost::system::error_code& ec) {
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
    }
}

/**
 * Get cpu and pirom address
 */
static void getCpuAddress(boost::asio::io_service& io,
                          std::shared_ptr<sdbusplus::asio::connection>& conn,
                          const std::string& service, const std::string& object,
                          const std::string& interface)
{
    conn->async_method_call(
        [&io, &conn](boost::system::error_code ec,
                     const boost::container::flat_map<
                         std::string,
                         std::variant<std::string, uint64_t, uint32_t, uint16_t,
                                      std::vector<std::string>>>& properties) {
            const uint64_t* value = NULL;
            uint8_t peciAddress = 0;
            uint8_t i2cBus = 0;
            uint8_t i2cDevice = 0;
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
                std::string path = cpuPath + std::to_string(cpu);
                cpuInfoMap.insert_or_assign(
                    cpu, std::make_shared<CPUInfo>(
                             static_cast<sdbusplus::bus::bus&>(*conn), path,
                             cpu, peciAddress, defaultI2cBus,
                             defaultI2cSlaveAddr0 + cpu - 1));
                // need to consider if it exists - not to re-create a new one
                if (i2cBus)
                {
                    cpuInfoMap[cpu]->i2cBus = static_cast<uint8_t>(i2cBus);
                }
                if (i2cDevice)
                {
                    cpuInfoMap[cpu]->i2cDevice =
                        static_cast<uint8_t>(i2cDevice);
                }
                // update cpuInfo
                getProcessorInfo(io, conn, cpu);
            }
        },
        service, object, "org.freedesktop.DBus.Properties", "GetAll",
        interface);
}

/**
 * D-Bus client: to get platform specific configs
 */
static int
    getCpuConfiguration(boost::asio::io_service& io,
                        std::shared_ptr<sdbusplus::asio::connection>& conn,
                        sdbusplus::asio::object_server& objServer)
{
    // Get the Cpu configuration
    // In case it's not available, set a match for it
    static std::unique_ptr<sdbusplus::bus::match::match> cpuConfigMatch =
        std::make_unique<sdbusplus::bus::match::match>(
            *conn,
            "type='signal',interface='org.freedesktop.DBus.Properties',member='"
            "PropertiesChanged',arg0namespace='xyz.openbmc_project."
            "Configuration.XeonCPU'",
            [&io, &conn, &objServer](sdbusplus::message::message& msg) {
                std::cerr << "get cpu configuration match\n";
                static boost::asio::steady_timer filterTimer(io);
                filterTimer.expires_after(
                    std::chrono::seconds(configCheckInterval));

                filterTimer.async_wait(
                    [&io, &conn,
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
                        cpuConfigMatch.reset();
                        getCpuConfiguration(io, conn, objServer);
                    });
            });

    conn->async_method_call(
        [&io, &conn, &objServer](
            boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
#if DEBUG
            std::cerr << "async_method_call callback\n";
#endif
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

            cpuConfigMatch.reset();

            for (const auto& object : subtree)
            {
                for (const auto& service : object.second)
                {
                    for (const auto& interface : service.second)
                    {
                        if (interface ==
                            "xyz.openbmc_project.Configuration.XeonCPU")
                        {
                            getCpuAddress(io, conn, service.first, object.first,
                                          interface);
                            break;
                        }
                    }
                }
            }
            std::cerr << "getCpuConfiguration callback complete\n";
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Configuration.XeonCPU"});

    return 0;
}

} // namespace cpu_info
} // namespace phosphor

int main(int argc, char* argv[])
{
    // setup connection to dbus
    boost::asio::io_service io;
    std::shared_ptr<sdbusplus::asio::connection> conn =
        std::make_shared<sdbusplus::asio::connection>(io);

    // CPUInfo Object
    conn->request_name(phosphor::cpu_info::cpuInfoObject);
    sdbusplus::asio::object_server server =
        sdbusplus::asio::object_server(conn);
    sdbusplus::bus::bus& bus = static_cast<sdbusplus::bus::bus&>(*conn);
    sdbusplus::server::manager::manager objManager(
        bus, "/xyz/openbmc_project/inventory");

    // Start the PECI check loop
    // start get configuration
    phosphor::cpu_info::getCpuConfiguration(io, conn, server);

    io.run();

    return 0;
}
