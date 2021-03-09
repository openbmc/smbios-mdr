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
#include "cpuinfo_utils.hpp"

#include <peci.h>

#include <boost/asio/steady_timer.hpp>
#include <xyz/openbmc_project/Common/Device/error.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Control/Processor/CurrentOperatingConfig/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cpu/OperatingConfig/server.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace cpu_info
{
namespace sst
{

class PECIError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

constexpr uint64_t bit(int index)
{
    return (1ull << index);
}

constexpr int extendedModel(CPUModel model)
{
    return (model >> 16) & 0xF;
}

constexpr bool modelSupportsDiscovery(CPUModel model)
{
    return extendedModel(model) >= extendedModel(icx);
}

constexpr bool modelSupportsControl(CPUModel model)
{
    return extendedModel(model) > extendedModel(icx);
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
    for (size_t i = 0; i < mask.size(); ++i)
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
    CPUModel cpuModel;
    int mbBus;

    PECIManager(int address, CPUModel model) :
        peciAddress(address), peciWoken(false), cpuModel(model)
    {
        mbBus = (model == icx) ? 14 : 31;
    }

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
        EPECIStatus libStatus =
            peci_WrPkgConfig(peciAddress, 5, enable ? 1 : 0, 0,
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
    static constexpr int mbSegment = 0;
    static constexpr int mbDevice = 30;
    static constexpr int mbFunction = 1;
    static constexpr int mbDataReg = 0xA0;
    static constexpr int mbInterfaceReg = 0xA4;
    static constexpr int mbRegSize = sizeof(uint32_t);

    enum class MailboxStatus
    {
        NoError = 0x0,
        InvalidCommand = 0x1,
        IllegalData = 0x16
    };

    /**
     * Send a single Write PCI Config Local command, targeting the PCU CR1
     * register block.
     *
     * @param[in]   regAddress  PCI Offset of register.
     * @param[in]   data        Data to write.
     */
    void wrMailboxReg(uint16_t regAddress, uint32_t data)
    {
        uint8_t completionCode;
        bool tryWaking = true;
        while (true)
        {
            EPECIStatus libStatus = peci_WrEndPointPCIConfigLocal(
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
    uint32_t rdMailboxReg(uint16_t regAddress)
    {
        uint8_t completionCode;
        uint32_t outputData;
        bool tryWaking = true;
        while (true)
        {
            EPECIStatus libStatus = peci_RdEndPointConfigPciLocal(
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
     * @param[out]  responseCode    Optional parameter to receive the
     *                              mailbox-level response status. If null, a
     *                              PECIError will be thrown for error status.
     *
     * @return  Data returned in mailbox. Value is undefined if command is a
     *          "setter".
     */
    uint32_t sendPECIOSMailboxCmd(uint8_t command, uint8_t subCommand,
                                  uint32_t inputData = 0,
                                  MailboxStatus* responseCode = nullptr)
    {
        // The simple mailbox algorithm just says to wait until the busy bit
        // is clear, but we'll give up after 10 tries. It's arbitrary but that's
        // quite long wall clock time.
        constexpr int mbRetries = 10;
        constexpr uint32_t mbBusyBit = bit(31);

        // Wait until RUN_BUSY == 0
        int attempts = mbRetries;
        while ((rdMailboxReg(mbInterfaceReg) & mbBusyBit) != 0 &&
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
        uint32_t interfaceReg =
            mbBusyBit | (static_cast<uint32_t>(subCommand) << 8) | command;
        wrMailboxReg(mbInterfaceReg, interfaceReg);

        // Wait until RUN_BUSY == 0
        attempts = mbRetries;
        do
        {
            interfaceReg = rdMailboxReg(mbInterfaceReg);
        } while ((interfaceReg & mbBusyBit) != 0 && --attempts > 0);
        if (attempts == 0)
        {
            throw PECIError("OS Mailbox failed to return");
        }

        // Read command return status or error code from interface register
        auto status = static_cast<MailboxStatus>(interfaceReg & 0xFF);
        if (responseCode != nullptr)
        {
            *responseCode = status;
        }
        else if (status != MailboxStatus::NoError)
        {
            throw PECIError(std::string("OS Mailbox returned with error: ") +
                            std::to_string(static_cast<int>(status)));
        }

        // Read command return data from the data register
        return rdMailboxReg(mbDataReg);
    }
};

/**
 * Base class for set of PECI OS Mailbox commands.
 * Constructing it runs the command and stores the value for use by derived
 * class accessor methods.
 */
template <uint8_t subcommand>
struct OsMailboxCommand
{
    enum ErrorPolicy
    {
        Throw,
        NoThrow
    };

    uint32_t value;
    PECIManager::MailboxStatus status;
    /**
     * Construct the command object with required PECI address and up to 4
     * optional 1-byte input data parameters.
     */
    OsMailboxCommand(PECIManager& pm, uint8_t param1 = 0, uint8_t param2 = 0,
                     uint8_t param3 = 0, uint8_t param4 = 0) :
        OsMailboxCommand(pm, ErrorPolicy::Throw, param1, param2, param3, param4)
    {}

    OsMailboxCommand(PECIManager& pm, ErrorPolicy errorPolicy,
                     uint8_t param1 = 0, uint8_t param2 = 0, uint8_t param3 = 0,
                     uint8_t param4 = 0)
    {
        PECIManager::MailboxStatus* callStatus =
            errorPolicy == Throw ? nullptr : &status;
        uint32_t param =
            (param4 << 24) | (param3 << 16) | (param2 << 8) | param1;
        value = pm.sendPECIOSMailboxCmd(0x7F, subcommand, param, callStatus);
    }

    /** Return whether the mailbox status indicated success or not. */
    bool success() const
    {
        return status == PECIManager::MailboxStatus::NoError;
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
        return (value >> lobit) & (bit(hibit - lobit + 1) - 1);                \
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

struct SetConfigTdpControl : OsMailboxCommand<0x2>
{
    using OsMailboxCommand::OsMailboxCommand;
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

struct SetLevel : OsMailboxCommand<0x8>
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

using BaseCurrentOperatingConfig =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Control::
                                    Processor::server::CurrentOperatingConfig>;

using BaseOperatingConfig =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Inventory::
                                    Item::Cpu::server::OperatingConfig>;

class OperatingConfig : public BaseOperatingConfig
{
  public:
    std::string path;
    int level;

  public:
    using BaseOperatingConfig::BaseOperatingConfig;
    OperatingConfig(sdbusplus::bus::bus& bus, int level_, std::string path_) :
        BaseOperatingConfig(bus, path_.c_str(), action::defer_emit),
        path(std::move(path_)), level(level_)
    {}
};

class CPUConfig : public BaseCurrentOperatingConfig
{
  private:
    /** Objects describing all available SST configs - not modifiable. */
    std::vector<std::unique_ptr<OperatingConfig>> availConfigs;
    sdbusplus::bus::bus& bus;
    const int peciAddress;
    const std::string path; ///< D-Bus path of CPU object
    const CPUModel cpuModel;
    const bool modificationAllowed;

    // Keep mutable copies of the properties so we can cache values that we
    // retrieve in the getters. We don't want to throw an error on a D-Bus
    // get-property call (extra error handling in clients), so by caching we can
    // hide any temporary hiccup in PECI communication.
    // These values can be changed by in-band software so we have to do a full
    // PECI read on every get-property, and can't assume that values will change
    // only when set-property is done.
    mutable int currentLevel;
    mutable bool bfEnabled;
    /**
     * Cached SST-TF enablement status. This is not exposed on D-Bus, but it's
     * needed because the command SetConfigTdpControl requires setting both
     * bits at once.
     */
    mutable bool tfEnabled;

    /**
     * Enforce common pre-conditions for D-Bus set property handlers.
     */
    void setPropertyCheckOrThrow()
    {
        if (!modificationAllowed)
        {
            throw sdbusplus::xyz::openbmc_project::Common::Error::NotAllowed();
        }
        if (hostState != HostState::PostComplete)
        {
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
    }

  public:
    CPUConfig(sdbusplus::bus::bus& bus_, int index, CPUModel model) :
        BaseCurrentOperatingConfig(bus_, generatePath(index).c_str(),
                                   action::defer_emit),
        bus(bus_), peciAddress(index + MIN_CLIENT_ADDR),
        path(generatePath(index)), cpuModel(model),
        modificationAllowed(modelSupportsControl(model)), currentLevel(0),
        bfEnabled(false), tfEnabled(false)
    {}

    //
    // D-Bus Property Overrides
    //

    sdbusplus::message::object_path appliedConfig() const override
    {
        // If CPU is powered off, return power-up default value of Level 0.
        int level = 0;
        if (hostState != HostState::Off)
        {
            // Otherwise, try to read current state
            try
            {
                PECIManager pm(peciAddress, cpuModel);
                currentLevel = GetLevelsInfo(pm).currentConfigTdpLevel();
            }
            catch (const PECIError& error)
            {
                std::cerr << "Failed to get SST-PP level: " << error.what()
                          << "\n";
            }
            level = currentLevel;
        }
        return generateConfigPath(level);
    }

    bool baseSpeedPriorityEnabled() const override
    {
        bool enabled = false;
        if (hostState != HostState::Off)
        {
            try
            {
                PECIManager pm(peciAddress, cpuModel);
                GetConfigTdpControl tdpControl(pm, currentLevel);
                bfEnabled = tdpControl.pbfEnabled();
                tfEnabled = tdpControl.factEnabled();
            }
            catch (const PECIError& error)
            {
                std::cerr << "Failed to get SST-BF status: " << error.what()
                          << "\n";
            }
            enabled = bfEnabled;
        }
        return enabled;
    }

    sdbusplus::message::object_path
        appliedConfig(sdbusplus::message::object_path value) override
    {
        setPropertyCheckOrThrow();

        const OperatingConfig* newConfig = nullptr;
        for (const auto& config : availConfigs)
        {
            if (config->path == value.str)
            {
                newConfig = config.get();
            }
        }

        if (newConfig == nullptr)
        {
            throw sdbusplus::xyz::openbmc_project::Common::Error::
                InvalidArgument();
        }

        try
        {
            PECIManager pm(peciAddress, cpuModel);
            SetLevel(pm, newConfig->level);
            currentLevel = newConfig->level;
        }
        catch (const PECIError& error)
        {
            std::cerr << "Failed to set new SST-PP level: " << error.what()
                      << "\n";
            throw sdbusplus::xyz::openbmc_project::Common::Device::Error::
                WriteFailure();
        }

        // return value not used
        return sdbusplus::message::object_path();
    }

    bool baseSpeedPriorityEnabled(bool value) override
    {
        setPropertyCheckOrThrow();

        try
        {
            PECIManager pm(peciAddress, cpuModel);
            uint32_t param = (value ? bit(17) : 0) | (tfEnabled ? bit(16) : 0);
            SetConfigTdpControl tdpControl(pm, 0, 0, param >> 16);
        }
        catch (const PECIError& error)
        {
            std::cerr << "Failed to set SST-BF status: " << error.what()
                      << "\n";
            throw sdbusplus::xyz::openbmc_project::Common::Device::Error::
                WriteFailure();
        }

        // return value not used
        return false;
    }

    //
    // Additions
    //

    OperatingConfig& newConfig(int level)
    {
        availConfigs.emplace_back(std::make_unique<OperatingConfig>(
            bus, level, generateConfigPath(level)));
        return *availConfigs.back();
    }

    std::string generateConfigPath(int level) const
    {
        return path + "/config" + std::to_string(level);
    }

    /**
     * Emit the interface added signals which were deferred. This is required
     * for ObjectMapper to pick up the objects, if we initially defered the
     * signal emitting.
     */
    void finalize()
    {
        emit_added();
        for (auto& config : availConfigs)
        {
            config->emit_added();
        }
    }

    static std::string generatePath(int index)
    {
        return cpuPath + std::to_string(index);
    }
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
 * @param[in,out]   conn    D-Bus ASIO connection.
 *
 * @return  Whether discovery was successfully finished.
 *
 * @throw PECIError     A PECI command failed on a CPU which had previously
 *                      responded to a command.
 */
static bool
    discoverCPUsAndConfigs(std::vector<std::unique_ptr<CPUConfig>>& cpuList,
                           boost::asio::io_context& ioc,
                           sdbusplus::asio::connection& conn)
{
    for (int i = MIN_CLIENT_ADDR; i <= MAX_CLIENT_ADDR; ++i)
    {
        // Let the event handler run any waiting tasks. If there is a lot of
        // PECI contention, SST discovery could take a long time. This lets us
        // get updates to hostState and handle any D-Bus requests.
        ioc.poll();

        if (hostState == HostState::Off)
        {
            return false;
        }

        // We could possibly check D-Bus for CPU presence and model, but PECI is
        // 10x faster and so much simpler.
        uint8_t cc, stepping;
        CPUModel cpuModel;
        EPECIStatus status = peci_GetCPUID(i, &cpuModel, &stepping, &cc);
        if (status == PECI_CC_TIMEOUT)
        {
            // Timing out indicates the CPU is present but PCS services not
            // working yet. Try again later.
            return false;
        }
        if (status != PECI_CC_SUCCESS || cc != PECI_DEV_CC_SUCCESS ||
            !modelSupportsDiscovery(cpuModel))
        {
            continue;
        }

        PECIManager peciManager(i, cpuModel);

        // Continue if processor does not support SST-PP
        GetLevelsInfo getLevelsInfo(peciManager);
        if (!getLevelsInfo.enabled())
        {
            continue;
        }

        // Generate D-Bus object path for this processor.
        int cpuIndex = i - MIN_CLIENT_ADDR;

        // Read the Turbo Ratio Limit Cores MSR which is used to generate the
        // Turbo Profile for each profile. This is a package scope MSR, so just
        // read thread 0.
        uint64_t trlCores;
        status = peci_RdIAMSR(i, 0, 0x1AE, &trlCores, &cc);
        if (!checkPECIStatus(status, cc))
        {
            throw PECIError("Failed to read TRL MSR");
        }

        // Create the per-CPU configuration object
        cpuList.emplace_back(
            std::make_unique<CPUConfig>(conn, cpuIndex, cpuModel));
        CPUConfig& cpu = *cpuList.back();

        bool foundCurrentLevel = false;

        for (int level = 0; level <= getLevelsInfo.configTdpLevels(); ++level)
        {
            // levels 1 and 2 are legacy/deprecated, originally used for AVX
            // license pre-granting. They may be reused for more levels in
            // future generations.
            // We can check if they are supported by running any command for
            // this level and checking the mailbox return status.
            GetConfigTdpControl tdpControl(
                peciManager, GetConfigTdpControl::ErrorPolicy::NoThrow, level);
            if (!tdpControl.success())
            {
                continue;
            }

            getSingleConfig(peciManager, level, cpu.newConfig(level), trlCores);

            if (level == getLevelsInfo.currentConfigTdpLevel())
            {
                foundCurrentLevel = true;
            }
        }

        if (!foundCurrentLevel)
        {
            // In case we didn't encounter a PECI error, but also didn't find
            // the config which is supposedly applied, we won't be able to
            // populate the CurrentOperatingConfig so we have to remove this CPU
            // from consideration.
            std::cerr << "CPU " << cpuIndex
                      << " claimed SST support but invalid configs\n";
            cpuList.pop_back();
            continue;
        }

        cpu.finalize();
    }

    return true;
}

void init(boost::asio::io_context& ioc,
          const std::shared_ptr<sdbusplus::asio::connection>& conn)
{
    static boost::asio::steady_timer peciRetryTimer(ioc);
    static std::vector<std::unique_ptr<CPUConfig>> cpus;
    static int peciErrorCount = 0;

    bool finished = false;
    try
    {
        DEBUG_PRINT << "Starting discovery\n";
        finished = discoverCPUsAndConfigs(cpus, ioc, *conn);
    }
    catch (const PECIError& err)
    {
        std::cerr << "PECI Error: " << err.what() << '\n';

        // In case of repeated failure to finish discovery, turn off this
        // feature altogether. Possible cause is that the CPU model does not
        // actually support the necessary mailbox commands.
        if (++peciErrorCount >= 50)
        {
            std::cerr << "Aborting SST discovery\n";
            return;
        }

        std::cerr << "Retrying SST discovery later\n";
    }

    DEBUG_PRINT << "Finished discovery attempt\n";

    // Retry later if no CPUs were available, or there was a PECI error.
    if (!finished)
    {
        // Drop any created interfaces to avoid presenting incomplete info
        cpus.clear();
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
}

} // namespace sst
} // namespace cpu_info
