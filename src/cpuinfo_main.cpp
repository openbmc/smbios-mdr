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

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>

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

struct ProcessorInfo
{
    uint64_t ppin;
    std::string sspec;
};

using CPUMap =
    boost::container::flat_map<size_t,
                               std::pair<int, std::shared_ptr<CPUInfo>>>;

static CPUMap cpuMap = {};

static std::unique_ptr<sdbusplus::bus::match_t> cpuUpdatedMatch = nullptr;

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

// PECI Client Address Map
static void getPECIAddrMap(CPUMap& cpuMap)
{
    int idx = 0;
    for (size_t i = MIN_CLIENT_ADDR; i <= MAX_CLIENT_ADDR; i++)
    {
        if (peci_Ping(i) == PECI_CC_SUCCESS)
        {
            cpuMap.emplace(std::make_pair(i, std::make_pair(idx, nullptr)));
            idx++;
        }
    }
}

static std::shared_ptr<CPUInfo>
    createCPUInfo(std::shared_ptr<sdbusplus::asio::connection>& conn,
                  const int& cpu)
{
    std::string path = cpuPath + std::to_string(cpu);
    std::shared_ptr<CPUInfo> cpuInfo = std::make_shared<CPUInfo>(
        static_cast<sdbusplus::bus::bus&>(*conn), path);
    return cpuInfo;
}

static void setAssetProperty(
    std::shared_ptr<sdbusplus::asio::connection>& conn, const int& cpu,
    const std::vector<std::pair<std::string, std::string>>& propValues)
{

    const std::string objectPath = cpuPath + std::to_string(cpu);
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
    if (cpuUpdatedMatch)
    {
        return;
    }

    const std::string objectPath = cpuPath + std::to_string(cpu);

    cpuUpdatedMatch = std::make_unique<sdbusplus::bus::match::match>(
        static_cast<sdbusplus::bus::bus&>(*conn),
        sdbusplus::bus::match::rules::interfacesAdded() +
            sdbusplus::bus::match::rules::argNpath(0, objectPath.c_str()),
        [&conn, cpu, propValues](sdbusplus::message::message& msg) {
            std::string objectName;
            boost::container::flat_map<
                std::string,
                boost::container::flat_map<std::string,
                                           std::variant<std::string, uint64_t>>>
                msgData;

            msg.read(objectName, msgData);

            // Check for xyz.openbmc_project.Inventory.Item.Cpu
            // interface match
            auto intfFound = msgData.find(cpuInterfaceName);
            if (msgData.end() != intfFound)
            {
                setAssetProperty(conn, cpu, propValues);
            }
        });
}

// constants for reading QDF string from PIROM
// Currently, they are the same for platforms with icx
// \todo: move into configuration file to be more robust
static constexpr uint8_t i2cBus = 13;
static constexpr uint8_t slaveAddr0 = 0x50;
static constexpr uint8_t regAddr = 0xf;
static constexpr uint8_t sspecSize = 4;

static void getProcessorInfo(std::shared_ptr<sdbusplus::asio::connection>& conn,
                             sdbusplus::asio::object_server& objServer,
                             CPUMap& cpuMap)
{

    for (auto& cpu : cpuMap)
    {
        uint8_t cc = 0;
        CPUModel model{};
        uint8_t stepping = 0;

        /// \todo in a follwup patch
        // CPUInfo will be updated as the centrol place for CPU information
        // std::shared_ptr<CPUInfo> cpuInfo =
        //    createCPUInfo(conn, cpu.second.first);
        // cpu.second.second = cpuInfo;

        if (peci_GetCPUID(cpu.first, &model, &stepping, &cc) != PECI_CC_SUCCESS)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Cannot get CPUID!",
                phosphor::logging::entry("PECIADDR=0x%x", cpu.first));
            continue;
        }

        switch (model)
        {
            case icx:
            {
                // get processor ID
                static constexpr uint8_t u8Size = 4; // default to a DWORD
                static constexpr uint8_t u8PPINPkgIndex = 19;
                static constexpr uint16_t u16PPINPkgParamHigh = 2;
                static constexpr uint16_t u16PPINPkgParamLow = 1;
                uint64_t cpuPPIN = 0;
                uint32_t u32PkgValue = 0;

                int ret = peci_RdPkgConfig(cpu.first, u8PPINPkgIndex,
                                           u16PPINPkgParamLow, u8Size,
                                           (uint8_t*)&u32PkgValue, &cc);
                if (0 != ret)
                {
                    phosphor::logging::log<phosphor::logging::level::ERR>(
                        "peci read package config failed at address",
                        phosphor::logging::entry("PECIADDR=0x%x", cpu.first),
                        phosphor::logging::entry("CC=0x%x", cc));
                    u32PkgValue = 0;
                }

                cpuPPIN = u32PkgValue;
                ret = peci_RdPkgConfig(cpu.first, u8PPINPkgIndex,
                                       u16PPINPkgParamHigh, u8Size,
                                       (uint8_t*)&u32PkgValue, &cc);
                if (0 != ret)
                {
                    phosphor::logging::log<phosphor::logging::level::ERR>(
                        "peci read package config failed at address",
                        phosphor::logging::entry("PECIADDR=0x%x", cpu.first),
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
                std::optional<std::string> sspec = readSSpec(
                    i2cBus, static_cast<uint8_t>(slaveAddr0 + cpu.second.first),
                    regAddr, sspecSize);
                // cpuInfo->model(sspec.value_or(""));
                values.emplace_back(
                    std::make_pair("Model", sspec.value_or("")));

                /// \todo in followup patch
                // CPUInfo is created by this service
                // update the below logic, which is needed because smbios
                // service creates the cpu object
                createCpuUpdatedMatch(conn, cpu.second.first, values);
                setAssetProperty(conn, cpu.second.first, values);
                break;
            }
            default:
                phosphor::logging::log<phosphor::logging::level::INFO>(
                    "in-compatible cpu for cpu asset info");
                break;
        }
    }
}

static bool isPECIAvailable(void)
{
    for (size_t i = MIN_CLIENT_ADDR; i <= MAX_CLIENT_ADDR; i++)
    {
        if (peci_Ping(i) == PECI_CC_SUCCESS)
        {
            return true;
        }
    }
    return false;
}

static void
    peciAvailableCheck(boost::asio::steady_timer& peciWaitTimer,
                       boost::asio::io_service& io,
                       std::shared_ptr<sdbusplus::asio::connection>& conn,
                       sdbusplus::asio::object_server& objServer)
{
    bool peciAvailable = isPECIAvailable();
    if (peciAvailable)
    {
        // get the PECI client address list
        getPECIAddrMap(cpuMap);
        getProcessorInfo(conn, objServer, cpuMap);
    }
    if (!peciAvailable || !cpuMap.size())
    {
        peciWaitTimer.expires_after(
            std::chrono::seconds(6 * peciCheckInterval));
        peciWaitTimer.async_wait([&peciWaitTimer, &io, &conn, &objServer](
                                     const boost::system::error_code& ec) {
            if (ec)
            {
                // operation_aborted is expected if timer is canceled
                // before completion.
                if (ec != boost::asio::error::operation_aborted)
                {
                    phosphor::logging::log<phosphor::logging::level::ERR>(
                        "PECI Available Check async_wait failed",
                        phosphor::logging::entry("EC=0x%x", ec.value()));
                }
                return;
            }
            peciAvailableCheck(peciWaitTimer, io, conn, objServer);
        });
    }
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
    boost::asio::steady_timer peciWaitTimer(
        io, std::chrono::seconds(phosphor::cpu_info::peciCheckInterval));
    peciWaitTimer.async_wait([&peciWaitTimer, &io, &conn,
                              &server](const boost::system::error_code& ec) {
        if (ec)
        {
            // operation_aborted is expected if timer is canceled
            // before completion.
            if (ec != boost::asio::error::operation_aborted)
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "PECI Available Check async_wait failed ",
                    phosphor::logging::entry("EC=0x%x", ec.value()));
            }
            return;
        }
        phosphor::cpu_info::peciAvailableCheck(peciWaitTimer, io, conn, server);
    });

    io.run();

    return 0;
}
