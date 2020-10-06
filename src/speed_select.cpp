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

#include "speed_select.hpp"

#include "cpuinfo.hpp"

#include <peci.h>

#include <boost/asio/steady_timer.hpp>
#include <xyz/openbmc_project/Control/Processor/CurrentOperatingConfig/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cpu/OperatingConfig/server.hpp>

#include <iostream>

namespace cpu_info
{
namespace sst
{

using BaseCurrentOperatingConfig =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Control::
                                    Processor::server::CurrentOperatingConfig>;

using OperatingConfig =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Inventory::
                                    Item::Cpu::server::OperatingConfig>;

class CurrentOperatingConfig : public BaseCurrentOperatingConfig
{
  public:
    using BaseCurrentOperatingConfig::appliedConfig;
    using BaseCurrentOperatingConfig::BaseCurrentOperatingConfig;
    using BaseCurrentOperatingConfig::baseSpeedPriorityEnabled;

    // Override the high-level setters which are called by the sdbus vtable
    // setter, so we can block any modification from D-Bus. We still need the
    // low-lovel setters to work so *we* can change property values.
    // Eventually these can be implemented to allow changing SST settings.

    sdbusplus::message::object_path
        appliedConfig(sdbusplus::message::object_path value) override
    {
        return appliedConfig();
    }
    bool baseSpeedPriorityEnabled(bool value) override
    {
        return baseSpeedPriorityEnabled();
    }
};

/**
 * Local container for all SST-related D-Bus objects associated with one CPU.
 */
struct CPUDBusState
{
    /**
     * Object describing the currently applied SST config - modifiable by
     * external applications.
     */
    std::unique_ptr<CurrentOperatingConfig> curConfig;
    /** Objects describing all available SST configs - not modifiable. */
    std::vector<std::unique_ptr<OperatingConfig>> availConfigs;
};

class PECIError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

constexpr uint64_t bit(int index)
{
    return (1ull << index);
}

/**
 * Construct a list of indexes of the set bits in the input value.
 * E.g. fn(0x7A) -> {1,3,4,5,6}
 *
 * @param[in]   mask    Bitmask to convert.
 *
 * @return  List of bit indexes.
 */
static std::vector<uint32_t> convertMaskToList(std::bitset<64> mask)
{
    std::vector<uint32_t> bitList;
    for (int i = 0; i < mask.size(); ++i)
    {
        if (mask.test(i))
        {
            bitList.push_back(i);
        }
    }
    return bitList;
}

static bool checkPECIStatus(EPECIStatus libStatus, uint8_t completionCode)
{
    if (libStatus != PECI_CC_SUCCESS || completionCode != PECI_DEV_CC_SUCCESS)
    {
        std::cerr << "PECI command failed."
                  << " Driver Status = " << libStatus << ","
                  << " Completion Code = " << static_cast<int>(completionCode)
                  << '\n';
        return false;
    }
    return true;
}

/**
 * Convenience RAII object for Wake-On-PECI (WOP) management, since PECI Config
 * Local accesses to the OS Mailbox require the package to pop up to PC2. Also
 * provides PCode OS Mailbox routine.
 *
 * Since multiple applications may be modifing WOP, we'll use this algorithm:
 * Whenever a PECI command fails with associated error code, set WOP bit and
 * retry command. Upon manager destruction, clear WOP bit only if we previously
 * set it.
 */
struct PECIManager
{
    int peciAddress;
    bool peciWoken;
    PECIManager(int address) : peciAddress(address), peciWoken(false)
    {}

    ~PECIManager()
    {
        // If we're being destroyed due to a PECIError, try to clear the mode
        // bit, but catch and ignore any duplicate error it might raise to
        // prevent termination.
        try
        {
            if (peciWoken)
            {
                setWakeOnPECI(false);
            }
        }
        catch (const PECIError& err)
        {}
    }

    static bool isSleeping(EPECIStatus libStatus, uint8_t completionCode)
    {
        // PECI completion code defined in peci-ioctl.h which is not available
        // for us to include.
        constexpr int PECI_DEV_CC_UNAVAIL_RESOURCE = 0x82;
        // Observed library returning DRIVER_ERR for reads and TIMEOUT for
        // writes while PECI is sleeping. Either way, the completion code from
        // PECI client should be reliable indicator of need to set WOP.
        return libStatus != PECI_CC_SUCCESS &&
               completionCode == PECI_DEV_CC_UNAVAIL_RESOURCE;
    }

    /**
     * Send a single PECI PCS write to modify the Wake-On-PECI mode bit
     */
    void setWakeOnPECI(bool enable)
    {
        uint8_t completionCode;
        auto libStatus = peci_WrPkgConfig(peciAddress, 5, enable ? 1 : 0, 0,
                                          sizeof(uint32_t), &completionCode);
        if (!checkPECIStatus(libStatus, completionCode))
        {
            throw PECIError("Failed to set Wake-On-PECI mode bit");
        }

        if (enable)
        {
            peciWoken = true;
        }
    }

    // PCode OS Mailbox interface register locations
    // Valid for ICX and SPR
    static constexpr int mbSegment = 0;
    static constexpr int mbBus = 14;
    static constexpr int mbDevice = 30;
    static constexpr int mbFunction = 1;
    static constexpr int mbDataReg = 0xA0;
    static constexpr int mbInterfaceReg = 0xA4;
    static constexpr int mbRegSize = sizeof(uint32_t);

    /**
     * Send a single Write PCI Config Local command, targeting the PCU CR1
     * register block.
     *
     * @param[in]   regAddress  PCI Offset of register.
     * @param[in]   data        Data to write.
     */
    void wrMailboxReg(int regAddress, uint32_t data)
    {
        uint8_t completionCode;
        bool tryWaking = true;
        while (true)
        {
            auto libStatus = peci_WrEndPointPCIConfigLocal(
                peciAddress, mbSegment, mbBus, mbDevice, mbFunction, regAddress,
                mbRegSize, data, &completionCode);
            if (tryWaking && isSleeping(libStatus, completionCode))
            {
                setWakeOnPECI(true);
                tryWaking = false;
                continue;
            }
            else if (!checkPECIStatus(libStatus, completionCode))
            {
                throw PECIError("Failed to write mailbox reg");
            }
            break;
        }
    }

    /**
     * Send a single Read PCI Config Local command, targeting the PCU CR1
     * register block.
     *
     * @param[in]   regAddress  PCI offset of register.
     *
     * @return  Register value
     */
    uint32_t rdMailboxReg(int regAddress)
    {
        uint8_t completionCode;
        uint32_t outputData;
        bool tryWaking = true;
        while (true)
        {
            auto libStatus = peci_RdEndPointConfigPciLocal(
                peciAddress, mbSegment, mbBus, mbDevice, mbFunction, regAddress,
                mbRegSize, reinterpret_cast<uint8_t*>(&outputData),
                &completionCode);
            if (tryWaking && isSleeping(libStatus, completionCode))
            {
                setWakeOnPECI(true);
                tryWaking = false;
                continue;
            }
            if (!checkPECIStatus(libStatus, completionCode))
            {
                throw PECIError("Failed to read mailbox reg");
            }
            break;
        }
        return outputData;
    }

    /**
     * Send command on PCode OS Mailbox interface.
     *
     * @param[in]   command     Main command ID.
     * @param[in]   subCommand  Sub command ID.
     * @param[in]   inputData   Data to put in mailbox. Is always written, but
     *                          will be ignored by PCode if command is a
     *                          "getter".
     *
     * @return  Data returned in mailbox. Value is undefined if command is a
     *          "setter".
     */
    uint32_t sendPECIOSMailboxCmd(int command, int subCommand,
                                  uint32_t inputData = 0)
    {
        // The simple mailbox algorithm just says to wait until the busy bit
        // is clear, but we'll give up after 10 tries. It's arbitrary but that's
        // quite long wall clock time.
        constexpr int mbRetries = 10;
        constexpr uint32_t mbBusyBit = bit(31);

        // Wait until RUN_BUSY == 0
        int attempts = mbRetries;
        while ((rdMailboxReg(mbInterfaceReg) & mbBusyBit) == 1 &&
               --attempts > 0)
            ;
        if (attempts == 0)
        {
            throw PECIError("OS Mailbox failed to become free");
        }

        // Write required command specific input data to data register
        wrMailboxReg(mbDataReg, inputData);

        // Write required command specific command/sub-command values and set
        // RUN_BUSY bit in interface register.
        uint32_t interfaceReg = mbBusyBit | (subCommand << 8) | command;
        wrMailboxReg(mbInterfaceReg, interfaceReg);

        // Wait until RUN_BUSY == 0
        attempts = mbRetries;
        do
        {
            interfaceReg = rdMailboxReg(mbInterfaceReg);
        } while ((interfaceReg & mbBusyBit) == 1 && --attempts > 0);
        if (attempts == 0)
        {
            throw PECIError("OS Mailbox failed to return");
        }

        // Read command return status or error code from interface register
        // TODO: What is PCODE response codes??  NO_ERROR; ILLEGAL_DATA;
        // INVALID_COMMAND ... these are not defined anywhere
        // assert((interfaceReg & 0xFF) == ??);

        // Read command return data from the data register
        return rdMailboxReg(mbDataReg);
    }
};

/**
 * Base class for set of PECI OS Mailbox commands.
 * Constructing it runs the command and stores the value for use by derived
 * class accessor methods.
 */
template <int subcommand>
struct OsMailboxCommand
{
    uint32_t value;
    /**
     * Construct the command object with required PECI address and up to 4
     * optional 1-byte input data parameters.
     */
    OsMailboxCommand(PECIManager& pm, uint8_t param1 = 0, uint8_t param2 = 0,
                     uint8_t param3 = 0, uint8_t param4 = 0)
    {
        uint32_t param =
            (param4 << 24) | (param3 << 16) | (param2 << 8) | param1;
        value = pm.sendPECIOSMailboxCmd(0x7F, subcommand, param);
    }
};

/**
 * Macro to define a derived class accessor method.
 *
 * @param[in]   type    Return type of accessor method.
 * @param[in]   name    Name of accessor method.
 * @param[in]   hibit   Most significant bit of field to access.
 * @param[in]   lobit   Least significant bit of field to access.
 */
#define FIELD(type, name, hibit, lobit)                                        \
    type name() const                                                          \
    {                                                                          \
        return (value >> lobit) & ((1ull << (hibit - lobit + 1)) - 1);         \
    }

struct GetLevelsInfo : OsMailboxCommand<0x0>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(bool, enabled, 31, 31)
    FIELD(bool, lock, 24, 24)
    FIELD(int, currentConfigTdpLevel, 23, 16)
    FIELD(int, configTdpLevels, 15, 8)
    FIELD(int, version, 7, 0)
};

struct GetConfigTdpControl : OsMailboxCommand<0x1>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(bool, pbfEnabled, 17, 17);
    FIELD(bool, factEnabled, 16, 16);
    FIELD(bool, pbfSupport, 1, 1);
    FIELD(bool, factSupport, 0, 0);
};

struct GetTdpInfo : OsMailboxCommand<0x3>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, tdpRatio, 23, 16);
    FIELD(int, pkgTdp, 14, 0);
};

struct GetCoreMask : OsMailboxCommand<0x6>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(uint32_t, coresMask, 31, 0);
};

struct GetTurboLimitRatios : OsMailboxCommand<0x7>
{
    using OsMailboxCommand::OsMailboxCommand;
};

struct GetRatioInfo : OsMailboxCommand<0xC>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, pm, 31, 24);
    FIELD(int, pn, 23, 16);
    FIELD(int, p1, 15, 8);
    FIELD(int, p0, 7, 0);
};

struct GetTjmaxInfo : OsMailboxCommand<0x5>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, tProchot, 7, 0);
};

struct PbfGetCoreMaskInfo : OsMailboxCommand<0x20>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(uint32_t, p1HiCoreMask, 31, 0);
};

struct PbfGetP1HiP1LoInfo : OsMailboxCommand<0x21>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, p1Hi, 15, 8);
    FIELD(int, p1Lo, 7, 0);
};

/**
 * Retrieve the SST parameters for a single config and fill the values into the
 * properties on the D-Bus interface.
 *
 * @param[in,out]   peciManager     PECI context to use.
 * @param[in]       level           Config TDP level to retrieve.
 * @param[out]      config          D-Bus interface to update.
 * @param[in]       trlCores        Turbo ratio limit core ranges from MSR
 *                                  0x1AE. This is constant across all configs
 *                                  in a CPU.
 */
static void getSingleConfig(PECIManager& peciManager, int level,
                            OperatingConfig& config, uint64_t trlCores)
{
    constexpr int mhzPerRatio = 100;

    // PowerLimit <= GET_TDP_INFO.PKG_TDP
    config.powerLimit(GetTdpInfo(peciManager, level).pkgTdp());

    // AvailableCoreCount <= GET_CORE_MASK.CORES_MASK
    uint64_t coreMaskLo = GetCoreMask(peciManager, level, 0).coresMask();
    uint64_t coreMaskHi = GetCoreMask(peciManager, level, 1).coresMask();
    std::bitset<64> coreMask = (coreMaskHi << 32) | coreMaskLo;
    config.availableCoreCount(coreMask.count());

    // BaseSpeed <= GET_RATIO_INFO.P1
    GetRatioInfo getRatioInfo(peciManager, level);
    config.baseSpeed(getRatioInfo.p1() * mhzPerRatio);

    // MaxSpeed <= GET_RATIO_INFO.P0
    config.maxSpeed(getRatioInfo.p0() * mhzPerRatio);

    // MaxJunctionTemperature <= GET_TJMAX_INFO.T_PROCHOT
    config.maxJunctionTemperature(GetTjmaxInfo(peciManager, level).tProchot());

    // Construct BaseSpeedPrioritySettings
    GetConfigTdpControl getConfigTdpControl(peciManager, level);
    std::vector<std::tuple<uint32_t, std::vector<uint32_t>>> baseSpeeds;
    if (getConfigTdpControl.pbfSupport())
    {
        coreMaskLo = PbfGetCoreMaskInfo(peciManager, level, 0).p1HiCoreMask();
        coreMaskHi = PbfGetCoreMaskInfo(peciManager, level, 1).p1HiCoreMask();
        std::bitset<64> hiFreqCoreMask = (coreMaskHi << 32) | coreMaskLo;

        std::vector<uint32_t> hiFreqCoreList, loFreqCoreList;
        hiFreqCoreList = convertMaskToList(hiFreqCoreMask);
        loFreqCoreList = convertMaskToList(coreMask & ~hiFreqCoreMask);

        PbfGetP1HiP1LoInfo pbfGetP1HiP1LoInfo(peciManager, level);
        baseSpeeds = {
            {pbfGetP1HiP1LoInfo.p1Hi() * mhzPerRatio, hiFreqCoreList},
            {pbfGetP1HiP1LoInfo.p1Lo() * mhzPerRatio, loFreqCoreList}};
    }
    config.baseSpeedPrioritySettings(baseSpeeds);

    // Construct TurboProfile
    std::vector<std::tuple<uint32_t, size_t>> turboSpeeds;

    // Only read the SSE ratios (don't need AVX2/AVX512).
    uint64_t limitRatioLo = GetTurboLimitRatios(peciManager, level, 0, 0).value;
    uint64_t limitRatioHi = GetTurboLimitRatios(peciManager, level, 1, 0).value;
    uint64_t limitRatios = (limitRatioHi << 32) | limitRatioLo;

    constexpr int maxTFBuckets = 8;
    for (int i = 0; i < maxTFBuckets; ++i)
    {
        size_t bucketCount = trlCores & 0xFF;
        int bucketSpeed = limitRatios & 0xFF;
        if (bucketCount != 0 && bucketSpeed != 0)
        {
            turboSpeeds.push_back({bucketSpeed * mhzPerRatio, bucketCount});
        }
        trlCores >>= 8;
        limitRatios >>= 8;
    }
    config.turboProfile(turboSpeeds);
}

/**
 * Retrieve all SST configuration info for all discoverable CPUs, and publish
 * the info on new D-Bus objects on the given bus connection.
 *
 * @param[out]  cpuList     List to append info about discovered CPUs,
 *                          including pointers to D-Bus objects to keep them
 *                          alive. No items may be added to list in case host
 *                          system is powered off and no CPUs are accessible.
 * @param[in,out]   ioc     ASIO IO context/service
 * @param[in,out]   conn    D-Bus ASIO connection.
 *
 * @throw PECIError     A PECI command failed on a CPU which had previously
 *                      responded to a Ping.
 */
static void discoverCPUsAndConfigs(std::vector<CPUDBusState>& cpuList,
                                   boost::asio::io_context& ioc,
                                   sdbusplus::asio::connection& conn)
{
    for (int i = MIN_CLIENT_ADDR; i <= MAX_CLIENT_ADDR; ++i)
    {
        // We could possibly check D-Bus for CPU presence and model, but PECI is
        // 10x faster and so much simpler. Currently only ICX is supported here.
        uint8_t cc, stepping;
        CPUModel cpuModel;
        auto status = peci_GetCPUID(i, &cpuModel, &stepping, &cc);
        if (status != PECI_CC_SUCCESS || cc != PECI_DEV_CC_SUCCESS ||
            cpuModel != icx)
        {
            continue;
        }

        PECIManager peciManager(i);

        // Continue if processor does not support SST-PP
        GetLevelsInfo getLevelsInfo(peciManager);
        if (!getLevelsInfo.enabled())
        {
            continue;
        }

        // Generate D-Bus object path for this processor.
        int cpuIndex = i - MIN_CLIENT_ADDR;
        std::string cpuPath =
            phosphor::cpu_info::cpuPath + std::to_string(cpuIndex);

        // Read the Turbo Ratio Limit Cores MSR which is used to generate the
        // Turbo Profile for each profile. This is a package scope MSR, so just
        // read thread 0.
        uint64_t trlCores;
        status = peci_RdIAMSR(i, 0, 0x1AE, &trlCores, &cc);
        if (!checkPECIStatus(status, cc))
        {
            throw PECIError("Failed to read TRL MSR");
        }

        // Create container to keep D-Bus server objects
        cpuList.emplace_back();
        CPUDBusState& cpu = cpuList.back();

        bool baseSpeedPriorityEnabled = false;
        std::string currentConfig("/");

        for (int level = 0; level <= getLevelsInfo.configTdpLevels(); ++level)
        {
            // levels 1 and 2 are legacy/deprecated in ice lake
            // originally used for AVX license pre-granting
            // but they may be reused for more levels in future generations.
            if (level == 1 || level == 2)
                continue;

            // Create a separate object representing this config
            auto configPath = cpuPath + "/config" + std::to_string(level);

            cpu.availConfigs.emplace_back(
                std::make_unique<OperatingConfig>(conn, configPath.c_str()));

            getSingleConfig(peciManager, level, *cpu.availConfigs.back(),
                            trlCores);

            // Save BF enablement state of current profile
            if (level == getLevelsInfo.currentConfigTdpLevel())
            {
                GetConfigTdpControl tdpControl(peciManager, level);
                baseSpeedPriorityEnabled = tdpControl.pbfEnabled();
                currentConfig = configPath;
            }
        }

        // Create the per-CPU configuration object on the CPU object
        // The server::object_t wrapper does not have a constructor which passes
        // along property initializing values, so instead we need to tell it to
        // defer emitting InterfacesAdded. If we emit the object added signal
        // with an invalid object_path value, dbus-broker will kick us off the
        // bus and we'll crash.
        cpu.curConfig = std::make_unique<CurrentOperatingConfig>(
            conn, cpuPath.c_str(), CurrentOperatingConfig::action::defer_emit);
        cpu.curConfig->appliedConfig(currentConfig, false);
        cpu.curConfig->baseSpeedPriorityEnabled(baseSpeedPriorityEnabled,
                                                false);
        cpu.curConfig->emit_added();
    }
}

void init(boost::asio::io_context& ioc,
          const std::shared_ptr<sdbusplus::asio::connection>& conn)
{
    static boost::asio::steady_timer peciRetryTimer(ioc);
    static std::vector<CPUDBusState> cpus;

    try
    {
        discoverCPUsAndConfigs(cpus, ioc, *conn);
    }
    catch (const PECIError& err)
    {
        std::cerr << "PECI Error: " << err.what() << '\n';
        std::cerr << "Retrying SST discovery later\n";
        // Drop any created interfaces to avoid presenting incomplete info
        cpus.clear();
    }

    // Retry later if no CPUs were available, or there was a PECI error.
    if (cpus.empty())
    {
        peciRetryTimer.expires_after(std::chrono::seconds(10));
        peciRetryTimer.async_wait([&ioc, conn](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "SST PECI Retry Timer failed: " << ec << '\n';
                return;
            }
            init(ioc, conn);
        });
    }
    else
    {
        // TODO: Kick off normal 1m periodic timer for checking current level.
    }
}

} // namespace sst
} // namespace cpu_info
