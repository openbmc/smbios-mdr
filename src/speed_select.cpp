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

#include <xyz/openbmc_project/Control/Processor/CurrentOperatingConfig/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cpu/OperatingConfig/server.hpp>

#include <cstdio>
#include <iostream>

using BaseCurrentOperatingConfigIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Control::
                                    Processor::server::CurrentOperatingConfig>;

using OperatingConfigIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Inventory::
                                    Item::Cpu::server::OperatingConfig>;

class CurrentOperatingConfigIntf : public BaseCurrentOperatingConfigIntf
{
  public:
    using BaseCurrentOperatingConfigIntf::appliedConfig;
    using BaseCurrentOperatingConfigIntf::BaseCurrentOperatingConfigIntf;
    using BaseCurrentOperatingConfigIntf::baseSpeedPriorityEnabled;
    using BaseCurrentOperatingConfigIntf::turboProfileEnabled;

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
    bool turboProfileEnabled(bool value) override
    {
        return turboProfileEnabled();
    }
};

/** Local container for CPU D-Bus interface mappings. */
struct CPU
{
    /** D-Bus interface for current SST config data */
    std::shared_ptr<CurrentOperatingConfigIntf> curConfig;
    /** All available SST profiles D-Bus interfaces */
    std::vector<std::shared_ptr<OperatingConfigIntf>> availConfigs;
};

using CPUList = std::vector<CPU>;

class PECIError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/**
 * Construct a list of indexes of the set bits in the input value.
 * E.g. fn(0x7A) -> {1,3,4,5,6}
 *
 * @param[in]   mask    Bitmask to convert.
 *
 * @return  List of bit indexes.
 */
static std::vector<int> convertMaskToList(uint64_t mask)
{
    std::vector<int> bits;
    for (int i = 0; i < sizeof(mask) * CHAR_BIT; ++i)
    {
        if ((mask & (1ULL << i)) != 0)
        {
            bits.push_back(i);
        }
    }
    return bits;
}

bool checkPECIStatus(EPECIStatus libStatus, uint8_t completionCode)
{
    if (libStatus != PECI_CC_SUCCESS || completionCode != PECI_DEV_CC_SUCCESS)
    {
        std::cerr << "PECI command failed."
                  << " Driver Status = " << libStatus << ","
                  << " Completion Code = " << static_cast<int>(completionCode)
                  << std::endl;
        return false;
    }
    return true;
}

/**
 * Convenience RAII object for Wake-On-PECI management, since PECI Config Local
 * accesses to the OS Mailbox require the package to pop up to PC2.
 * Destructor ensures that mode bit is cleared. Theoretically, the constructor
 * could also take an exclusive lock on PECI bus and set the mode bit, but
 * unfortunately there is no reliable way to get exclusive access, so for now
 * we'll settle for setting the mode bit right before it's needed and hoping it
 * stays set.
 * Also provides PCode OS Mailbox routine.
 */
struct PECIManager
{
    int peciAddress;
    PECIManager(int address) : peciAddress(address)
    {}

    ~PECIManager()
    {
        // If we're being destroyed due to a PECIError, try to clear the mode
        // bit, but catch and ignore any duplicate error it might raise to
        // prevent termination.
        try
        {
            setWakeOnPECI(false);
        }
        catch (const PECIError& err)
        {}
    }

    /**
     * Send a single PECI PCS write to modify the Wake-On-PECI mode bit
     */
    void setWakeOnPECI(bool enable)
    {
        uint8_t completionCode;
        auto driverStatus = peci_WrPkgConfig(peciAddress, 5, enable ? 1 : 0, 0,
                                             sizeof(uint32_t), &completionCode);
        if (!checkPECIStatus(driverStatus, completionCode))
        {
            throw PECIError("Failed to set Wake-On-PECI mode bit");
        }
    }

    // PCode OS Mailbox interface register locations
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
        setWakeOnPECI(true);

        uint8_t completionCode;
        auto driverStatus = peci_WrEndPointPCIConfigLocal(
            peciAddress, mbSegment, mbBus, mbDevice, mbFunction, regAddress,
            mbRegSize, data, &completionCode);
        if (!checkPECIStatus(driverStatus, completionCode))
        {
            throw PECIError("Failed to write mailbox reg");
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
        setWakeOnPECI(true);

        uint8_t completionCode;
        uint32_t outputData;
        auto driverStatus = peci_RdEndPointConfigPciLocal(
            peciAddress, mbSegment, mbBus, mbDevice, mbFunction, regAddress,
            mbRegSize, reinterpret_cast<uint8_t*>(&outputData),
            &completionCode);
        if (!checkPECIStatus(driverStatus, completionCode))
        {
            throw PECIError("Failed to read mailbox reg");
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
        // Wait until RUN_BUSY == 0
        int attempts = 10;
        while ((rdMailboxReg(mbInterfaceReg) & (1u << 31)) != 0 &&
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
        uint32_t interfaceReg = (1u << 31) | (subCommand << 8) | command;
        wrMailboxReg(mbInterfaceReg, interfaceReg);

        // Wait until RUN_BUSY == 0
        attempts = 10;
        do
        {
            interfaceReg = rdMailboxReg(mbInterfaceReg);
        } while ((interfaceReg & (1u << 31)) != 0 && --attempts > 0);
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
template <int command, int subcommand>
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
        value = pm.sendPECIOSMailboxCmd(command, subcommand, param);
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

struct GetLevelsInfo : OsMailboxCommand<0x7F, 0x0>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(bool, lock, 24, 24)
    FIELD(int, currentConfigTdpLevel, 23, 16)
    FIELD(int, configTdpLevels, 15, 8)
    FIELD(int, version, 7, 0)
};

struct GetConfigTdpControl : OsMailboxCommand<0x7F, 0x1>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(bool, pbfEnabled, 17, 17);
    FIELD(bool, factEnabled, 16, 16);
    FIELD(bool, pbfSupport, 1, 1);
    FIELD(bool, factSupport, 0, 0);
};

struct GetTdpInfo : OsMailboxCommand<0x7F, 0x3>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, tdpRatio, 23, 16);
    FIELD(int, pkgTdp, 14, 0);
};

struct GetCoreMask : OsMailboxCommand<0x7F, 0x6>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(uint32_t, coresMask, 31, 0);
};

struct GetRatioInfo : OsMailboxCommand<0x7F, 0xC>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, pm, 31, 24);
    FIELD(int, pn, 23, 16);
    FIELD(int, p1, 15, 8);
    FIELD(int, p0, 7, 0);
};

struct GetTjmaxInfo : OsMailboxCommand<0x7F, 0x5>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, tProchot, 7, 0);
};

struct PbfGetCoreMaskInfo : OsMailboxCommand<0x7F, 0x20>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(uint32_t, p1HiCoreMask, 31, 0);
};

struct PbfGetP1HiP1LoInfo : OsMailboxCommand<0x7F, 0x21>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(int, p1Hi, 15, 8);
    FIELD(int, p1Lo, 7, 0);
};

struct GetFactHpTurboLimitNumcores : OsMailboxCommand<0x7F, 0x10>
{
    using OsMailboxCommand::OsMailboxCommand;
};

struct GetFactHpTurboLimitRatios : OsMailboxCommand<0x7F, 0x11>
{
    using OsMailboxCommand::OsMailboxCommand;
};

/**
 * Retrieve the SST parameters for a single config and fill the values into the
 * properties on the D-Bus interface.
 *
 * @param[in,out]   peciManager     PECI context to use.
 * @param[in]       level           Config TDP level to retrieve.
 * @param[in,out]   config          D-Bus interface to update.
 */
static void getSingleConfig(PECIManager& peciManager, int level,
                            std::shared_ptr<OperatingConfigIntf> config)
{
    // PowerLimit <= GET_TDP_INFO.PKG_TDP
    config->powerLimit(GetTdpInfo(peciManager, level).pkgTdp());

    // AvailableCoreCount <= GET_CORE_MASK.CORES_MASK
    uint64_t coreMaskLo = GetCoreMask(peciManager, level, 0).coresMask();
    uint64_t coreMaskHi = GetCoreMask(peciManager, level, 1).coresMask();
    uint64_t coreMask = (coreMaskHi << 32) | coreMaskLo;
    config->availableCoreCount(convertMaskToList(coreMask).size());

    // BaseSpeed <= GET_RATIO_INFO.P1
    GetRatioInfo getRatioInfo(peciManager, level);
    config->baseSpeed(getRatioInfo.p1() * 100);

    // MaxSpeed <= GET_RATIO_INFO.P0
    config->maxSpeed(getRatioInfo.p0() * 100);

    // MaxJunctionTemperature <= GET_TJMAX_INFO.T_PROCHOT
    config->maxJunctionTemperature(GetTjmaxInfo(peciManager, level).tProchot());

    // Construct BaseSpeedPrioritySettings
    GetConfigTdpControl getConfigTdpControl(peciManager, level);
    std::vector<std::tuple<int, std::vector<int>>> baseSpeeds;
    if (getConfigTdpControl.pbfSupport())
    {
        // TODO: Convert the Punit core IDs to logical IDs. Will need to read
        // RESOLVED_CORES register per-CPU (ICX: B31/D30/F3/0xD0) to construct
        // the mapping.
        coreMaskLo = PbfGetCoreMaskInfo(peciManager, level, 0).p1HiCoreMask();
        coreMaskHi = PbfGetCoreMaskInfo(peciManager, level, 1).p1HiCoreMask();
        uint64_t hiFreqCoreMask = (coreMaskHi << 32) | coreMaskLo;

        std::vector<int> hiFreqCoreList, loFreqCoreList;
        hiFreqCoreList = convertMaskToList(hiFreqCoreMask);
        loFreqCoreList = convertMaskToList(coreMask & ~hiFreqCoreMask);

        PbfGetP1HiP1LoInfo pbfGetP1HiP1LoInfo(peciManager, level);
        baseSpeeds = {{pbfGetP1HiP1LoInfo.p1Hi() * 100, hiFreqCoreList},
                      {pbfGetP1HiP1LoInfo.p1Lo() * 100, loFreqCoreList}};
    }
    config->baseSpeedPrioritySettings(baseSpeeds);

    // Construct TurboProfile
    std::vector<std::tuple<int, size_t>> turboSpeeds;
    if (getConfigTdpControl.factSupport())
    {
        uint64_t coreCountHi, coreCountLo;
        coreCountLo = GetFactHpTurboLimitNumcores(peciManager, level, 0).value;
        coreCountHi = GetFactHpTurboLimitNumcores(peciManager, level, 1).value;
        uint64_t coreCounts = (coreCountHi << 32) | coreCountLo;

        // Only read the SSE ratios (don't need AVX2/AVX512).
        uint64_t limitRatioLo =
            GetFactHpTurboLimitRatios(peciManager, level, 0, 0).value;
        uint64_t limitRatioHi =
            GetFactHpTurboLimitRatios(peciManager, level, 1, 0).value;
        uint64_t limitRatios = (limitRatioHi << 32) | limitRatioLo;

        constexpr int maxTFBuckets = 8;
        for (int i = 0; i < maxTFBuckets; ++i)
        {
            size_t bucketCount = coreCounts & 0xFF;
            int bucketSpeed = limitRatios & 0xFF;
            if (bucketCount != 0 && bucketSpeed != 0)
            {
                turboSpeeds.push_back({bucketSpeed * 100, bucketCount});
            }
            coreCounts >>= 8;
            limitRatios >>= 8;
        }
    }
    config->turboProfile(turboSpeeds);
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
static void
    discoverCPUsAndConfigs(CPUList& cpuList, boost::asio::io_context& ioc,
                           std::shared_ptr<sdbusplus::asio::connection> conn)
{
    for (int i = MIN_CLIENT_ADDR; i <= MAX_CLIENT_ADDR; ++i)
    {
        // We could check D-Bus for CPU presence, but a PECI Ping is 10x faster
        // and so much simpler.
        if (peci_Ping(i) != PECI_CC_SUCCESS)
        {
            continue;
        }

        cpuList.emplace_back();
        CPU& cpu = cpuList.back();
        PECIManager peciManager(i);

        int cpuIndex = i - MIN_CLIENT_ADDR;
        std::string cpuPath =
            phosphor::cpu_info::cpuPath + std::to_string(cpuIndex);

        GetLevelsInfo getLevelsInfo(peciManager);

        bool baseSpeedPriorityEnabled = false;
        bool turboProfileEnabled = false;
        std::string currentConfig("/");

        for (int level = 0; level <= getLevelsInfo.configTdpLevels(); ++level)
        {
            // levels 1 and 2 are legacy/deprecated in ice lake
            // originally used for AVX license pre-granting
            // but they may be reused for more levels in SPR+
            if (level == 1 || level == 2)
                continue;

            // Create a separate object representing this config
            auto configPath = cpuPath + "/config" + std::to_string(level);

            auto operatingConfig = std::make_shared<OperatingConfigIntf>(
                *conn, configPath.c_str());
            cpu.availConfigs.push_back(operatingConfig);

            getSingleConfig(peciManager, level, operatingConfig);

            // Save BF/TF enablement state of current profile
            if (level == getLevelsInfo.currentConfigTdpLevel())
            {
                GetConfigTdpControl tdpControl(peciManager);
                baseSpeedPriorityEnabled = tdpControl.pbfEnabled();
                turboProfileEnabled = tdpControl.factEnabled();
                currentConfig = configPath;
            }
        }

        // Create the per-CPU configuration object on the CPU object
        // The server::object_t wrapper does not have a constructor which passes
        // along property initializing values, so instead we need to tell it to
        // defer emitting InterfacesAdded. If we emit the object added signal
        // with an invalid object_path value, dbus-broker will kick us off the
        // bus and we'll crash.
        cpu.curConfig = std::make_shared<CurrentOperatingConfigIntf>(
            *conn, cpuPath.c_str(),
            CurrentOperatingConfigIntf::action::defer_emit);
        cpu.curConfig->appliedConfig(currentConfig, false);
        cpu.curConfig->baseSpeedPriorityEnabled(baseSpeedPriorityEnabled,
                                                false);
        cpu.curConfig->turboProfileEnabled(turboProfileEnabled, false);
        cpu.curConfig->emit_added();
    }
}

void getSSTConfigInfo(boost::asio::io_context& ioc,
                      std::shared_ptr<sdbusplus::asio::connection> conn)
{
    static boost::asio::steady_timer peciRetryTimer(ioc);
    static CPUList cpus;

    try
    {
        discoverCPUsAndConfigs(cpus, ioc, conn);
    }
    catch (const PECIError& err)
    {
        std::cerr << "PECI Error: " << err.what() << std::endl;
        std::cerr << "Retrying SST discovery later" << std::endl;
        cpus.clear();
    }

    if (cpus.empty())
    {
        peciRetryTimer.expires_after(std::chrono::seconds(10));
        peciRetryTimer.async_wait([&ioc, conn](boost::system::error_code ec) {
            if (ec)
            {
                std::cerr << "SST PECI Retry Timer failed: " << ec << std::endl;
                return;
            }
            getSSTConfigInfo(ioc, conn);
        });
    }
    else
    {
        // TODO: Kick off normal 1m periodic timer for checking current level.
    }
}
