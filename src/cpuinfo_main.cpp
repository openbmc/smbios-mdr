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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/container/flat_map.hpp>

#include <iostream>
#include <list>
#include <optional>
#include <sstream>
#include <string>

extern "C"
{
#include <i2c/smbus.h>
#include <linux/i2c-dev.h>
}

#if PECI_ENABLED
#include "speed_select.hpp"

#include <peci.h>
#endif

#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/object_server.hpp>

namespace cpu_info
{
static constexpr bool debug = false;
static constexpr const char* assetInterfaceName =
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

/**
 * Simple aggregate to define an external D-Bus property which needs to be set
 * by this application.
 */
struct CpuProperty
{
    std::string object;
    std::string interface;
    std::string name;
    std::string value;
};

/**
 * List of properties we want to set on other D-Bus objects. This list is kept
 * around so that if any target objects are removed+readded, then we can set the
 * values again.
 */
static std::list<CpuProperty> propertiesToSet;

static std::ostream& logStream(int cpu)
{
    return std::cerr << "[CPU " << cpu << "] ";
}

static void
    setCpuProperty(const std::shared_ptr<sdbusplus::asio::connection>& conn,
                   size_t cpu, const std::string& interface,
                   const std::string& propName, const std::string& propVal);
static void
    setDbusProperty(const std::shared_ptr<sdbusplus::asio::connection>& conn,
                    size_t cpu, const CpuProperty& newProp);
static void createCpuUpdatedMatch(
    const std::shared_ptr<sdbusplus::asio::connection>& conn, size_t cpu);

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

    if (sspec.size() < 4)
    {
        return std::nullopt;
    }

    return sspec;
}

/**
 * Higher level SSpec logic.
 * This handles retrying the PIROM reads until two subsequent reads are
 * successful and return matching data. When we have confidence that the data
 * read is correct, then set the property on D-Bus.
 */
static void
    tryReadSSpec(const std::shared_ptr<sdbusplus::asio::connection>& conn,
                 size_t cpuIndex)
{
    static int failedReads = 0;

    auto cpuInfoIt = cpuInfoMap.find(cpuIndex);
    if (cpuInfoIt == cpuInfoMap.end())
    {
        return;
    }
    auto cpuInfo = cpuInfoIt->second;

    std::optional<std::string> newSSpec =
        readSSpec(cpuInfo->i2cBus, cpuInfo->i2cDevice, sspecRegAddr, sspecSize);
    logStream(cpuInfo->id) << "SSpec read status: "
                           << static_cast<bool>(newSSpec) << "\n";
    if (newSSpec && newSSpec == cpuInfo->sSpec)
    {
        setCpuProperty(conn, cpuInfo->id, assetInterfaceName, "Model",
                       *newSSpec);
        return;
    }

    // If this read failed, back off for a little longer so that hopefully the
    // transient condition affecting PIROM reads will pass, but give up after
    // several consecutive failures. But if this read looked OK, try again
    // sooner to confirm it.
    int retrySeconds;
    if (newSSpec)
    {
        retrySeconds = 1;
        failedReads = 0;
        cpuInfo->sSpec = *newSSpec;
    }
    else
    {
        retrySeconds = 5;
        if (++failedReads > 10)
        {
            logStream(cpuInfo->id) << "PIROM Read failed too many times\n";
            return;
        }
    }

    auto sspecTimer = std::make_shared<boost::asio::steady_timer>(
        conn->get_io_context(), std::chrono::seconds(retrySeconds));
    sspecTimer->async_wait(
        [sspecTimer, conn, cpuIndex](boost::system::error_code ec) {
        if (ec)
        {
            return;
        }
        tryReadSSpec(conn, cpuIndex);
    });
}

/**
 * Add a D-Bus property to the global list, and attempt to set it by calling
 * `setDbusProperty`.
 *
 * @param[in,out]   conn        D-Bus connection.
 * @param[in]       cpu         1-based CPU index.
 * @param[in]       interface   Interface to set.
 * @param[in]       propName    Property to set.
 * @param[in]       propVal     Value to set.
 */
static void
    setCpuProperty(const std::shared_ptr<sdbusplus::asio::connection>& conn,
                   size_t cpu, const std::string& interface,
                   const std::string& propName, const std::string& propVal)
{
    // cpuId from configuration is one based as
    // dbus object path used by smbios is 0 based
    const std::string objectPath = cpuPath + std::to_string(cpu - 1);

    // Can switch to emplace_back if you define a CpuProperty constructor.
    propertiesToSet.push_back(
        CpuProperty{objectPath, interface, propName, propVal});

    setDbusProperty(conn, cpu, propertiesToSet.back());
}

/**
 * Set a D-Bus property which is already contained in the global list, and also
 * setup a D-Bus match to make sure the target property stays correct.
 *
 * @param[in,out]   conn    D-Bus connection.
 * @param[in]       cpu     1-baesd CPU index.
 * @param[in]       newProp Property to set.
 */
static void
    setDbusProperty(const std::shared_ptr<sdbusplus::asio::connection>& conn,
                    size_t cpu, const CpuProperty& newProp)
{
    createCpuUpdatedMatch(conn, cpu);
    conn->async_method_call(
        [](const boost::system::error_code ec) {
        if (ec)
        {
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Cannot set CPU property!");
            return;
        }
    },
        cpuProcessName, newProp.object.c_str(),
        "org.freedesktop.DBus.Properties", "Set", newProp.interface,
        newProp.name, std::variant<std::string>{newProp.value});
}

/**
 * Set up a D-Bus match (if one does not already exist) to watch for any new
 * interfaces on the cpu object. When new interfaces are added, re-send all
 * properties targeting that object/interface.
 *
 * @param[in,out]   conn    D-Bus connection.
 * @param[in]       cpu     1-based CPU index.
 */
static void createCpuUpdatedMatch(
    const std::shared_ptr<sdbusplus::asio::connection>& conn, size_t cpu)
{
    static boost::container::flat_map<size_t,
                                      std::unique_ptr<sdbusplus::bus::match_t>>
        cpuUpdatedMatch;

    if (cpuUpdatedMatch[cpu])
    {
        return;
    }

    const std::string objectPath = cpuPath + std::to_string(cpu - 1);

    cpuUpdatedMatch.insert_or_assign(
        cpu,
        std::make_unique<sdbusplus::bus::match_t>(
            static_cast<sdbusplus::bus_t&>(*conn),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(0, objectPath.c_str()),
            [conn, cpu](sdbusplus::message_t& msg) {
        sdbusplus::message::object_path objectName;
        boost::container::flat_map<
            std::string, boost::container::flat_map<
                             std::string, std::variant<std::string, uint64_t>>>
            msgData;

        msg.read(objectName, msgData);

        // Go through all the property changes, and retry all of them
        // targeting this object/interface which was just added.
        for (const CpuProperty& prop : propertiesToSet)
        {
            if (prop.object == objectName && msgData.contains(prop.interface))
            {
                setDbusProperty(conn, cpu, prop);
            }
        }
    }));
}

#if PECI_ENABLED
static void getPPIN(boost::asio::io_service& io,
                    const std::shared_ptr<sdbusplus::asio::connection>& conn,
                    const size_t& cpu)
{
    if (cpuInfoMap.find(cpu) == cpuInfoMap.end() || cpuInfoMap[cpu] == nullptr)
    {
        std::cerr << "No information found for cpu " << cpu << "\n";
        return;
    }

    std::shared_ptr<CPUInfo> cpuInfo = cpuInfoMap[cpu];

    if (cpuInfo->id != cpu)
    {
        std::cerr << "Incorrect CPU id " << (unsigned)cpuInfo->id << " expect "
                  << cpu << "\n";
        return;
    }

    uint8_t cpuAddr = cpuInfo->peciAddr;

    uint8_t cc = 0;
    CPUModel model{};
    uint8_t stepping = 0;

    // Wait for POST to complete to ensure that BIOS has time to enable the
    // PPIN. Before BIOS enables it, we would get a 0x90 CC on PECI.
    if (hostState != HostState::postComplete ||
        peci_GetCPUID(cpuAddr, &model, &stepping, &cc) != PECI_CC_SUCCESS)
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
            getPPIN(io, conn, cpu);
        });
        return;
    }

    switch (model)
    {
        case icx:
        case icxd:
        case spr:
        case emr:
        case gnr:
        case gnrd:
        case srf:
        {
            // PPIN can be read through PCS 19
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

            // set SerialNumber if cpuPPIN is valid
            if (0 != cpuPPIN)
            {
                std::stringstream stream;
                stream << std::hex << cpuPPIN;
                std::string serialNumber(stream.str());
                cpuInfo->publishUUID(*conn, serialNumber);
            }
            break;
        }
        default:
            phosphor::logging::log<phosphor::logging::level::INFO>(
                "in-compatible cpu for cpu asset info");
            break;
    }
}
#endif

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
        std::optional<uint8_t> peciAddress;
        uint8_t i2cBus = defaultI2cBus;
        std::optional<uint8_t> i2cDevice;
        std::optional<size_t> cpu;

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

        if (!cpu || !peciAddress)
        {
            return;
        }

        if (!i2cDevice)
        {
            i2cDevice = defaultI2cSlaveAddr0 + *cpu - 1;
        }

        auto key = cpuInfoMap.find(*cpu);

        if (key != cpuInfoMap.end())
        {
            cpuInfoMap.erase(key);
        }

        cpuInfoMap.emplace(*cpu, std::make_shared<CPUInfo>(*cpu, *peciAddress,
                                                           i2cBus, *i2cDevice));

        tryReadSSpec(conn, *cpu);

#if PECI_ENABLED
        getPPIN(io, conn, *cpu);
#endif
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
    static std::unique_ptr<sdbusplus::bus::match_t> cpuConfigMatch =
        std::make_unique<sdbusplus::bus::match_t>(
            *conn,
            "type='signal',interface='org.freedesktop.DBus.Properties',member='"
            "PropertiesChanged',arg0='xyz.openbmc_project."
            "Configuration.XeonCPU'",
            [&io, conn, &objServer](sdbusplus::message_t& /* msg */) {
        std::cerr << "get cpu configuration match\n";
        static boost::asio::steady_timer filterTimer(io);
        filterTimer.expires_after(std::chrono::seconds(configCheckInterval));

        filterTimer.async_wait(
            [&io, conn, &objServer](const boost::system::error_code& ec) {
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

int main()
{
    // setup connection to dbus
    boost::asio::io_service& io = cpu_info::dbus::getIOContext();
    std::shared_ptr<sdbusplus::asio::connection> conn =
        cpu_info::dbus::getConnection();

    // CPUInfo Object
    conn->request_name(cpu_info::cpuInfoObject);
    sdbusplus::asio::object_server server =
        sdbusplus::asio::object_server(conn);
    sdbusplus::bus_t& bus = static_cast<sdbusplus::bus_t&>(*conn);
    sdbusplus::server::manager_t objManager(bus,
                                            "/xyz/openbmc_project/inventory");

    cpu_info::hostStateSetup(conn);

#if PECI_ENABLED
    cpu_info::sst::init();
#endif

    // shared_ptr conn is global for the service
    // const reference of conn is passed to async calls
    cpu_info::getCpuConfiguration(io, conn, server);

    io.run();

    return 0;
}
