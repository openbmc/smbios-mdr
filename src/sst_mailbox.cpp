// Copyright (c) 2022 Intel Corporation
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

#include "cpuinfo_utils.hpp"
#include "speed_select.hpp"

#include <iostream>

namespace cpu_info
{
namespace sst
{

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
    uint8_t peciAddress;
    bool peciWoken;
    CPUModel cpuModel;
    uint8_t mbBus;

    PECIManager(uint8_t address, CPUModel model) :
        peciAddress(address), peciWoken(false), cpuModel(model)
    {
        mbBus = (model == icx) ? mbBusICX : mbBusOther;
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
    static constexpr int mbBusICX = 14;
    static constexpr int mbBusOther = 31;
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
        DEBUG_PRINT << "Running OS Mailbox command "
                    << static_cast<int>(subcommand) << '\n';
        PECIManager::MailboxStatus* callStatus =
            errorPolicy == Throw ? nullptr : &status;
        uint32_t param = (static_cast<uint32_t>(param4) << 24) |
                         (static_cast<uint32_t>(param3) << 16) |
                         (static_cast<uint32_t>(param2) << 8) | param1;
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
    FIELD(unsigned, currentConfigTdpLevel, 23, 16)
    FIELD(unsigned, configTdpLevels, 15, 8)
    FIELD(unsigned, version, 7, 0)
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
    FIELD(unsigned, tdpRatio, 23, 16);
    FIELD(unsigned, pkgTdp, 14, 0);
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
    FIELD(unsigned, pm, 31, 24);
    FIELD(unsigned, pn, 23, 16);
    FIELD(unsigned, p1, 15, 8);
    FIELD(unsigned, p0, 7, 0);
};

struct GetTjmaxInfo : OsMailboxCommand<0x5>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(unsigned, tProchot, 7, 0);
};

struct PbfGetCoreMaskInfo : OsMailboxCommand<0x20>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(uint32_t, p1HiCoreMask, 31, 0);
};

struct PbfGetP1HiP1LoInfo : OsMailboxCommand<0x21>
{
    using OsMailboxCommand::OsMailboxCommand;
    FIELD(unsigned, p1Hi, 15, 8);
    FIELD(unsigned, p1Lo, 7, 0);
};

/**
 * Implementation of SSTInterface based on OS Mailbox interface supported on ICX
 * and SPR processors.
 * It's expected that an instance of this class will be created for each
 * "atomic" set of operations.
 */
class SSTMailbox : public SSTInterface
{
  private:
    uint8_t address;
    CPUModel model;
    PECIManager pm;

    static constexpr int mhzPerRatio = 100;

  public:
    SSTMailbox(uint8_t _address, CPUModel _model) :
        address(_address), model(_model),
        pm(static_cast<uint8_t>(address), model)
    {}
    ~SSTMailbox()
    {}

    bool ready() override
    {
        return true;
    }

    bool supportsControl() override
    {
        return model == spr;
    }

    unsigned int currentLevel() override
    {
        return GetLevelsInfo(pm).currentConfigTdpLevel();
    }
    unsigned int maxLevel() override
    {
        return GetLevelsInfo(pm).configTdpLevels();
    }
    bool ppEnabled() override
    {
        return GetLevelsInfo(pm).enabled();
    }

    bool levelSupported(unsigned int level) override
    {
        GetConfigTdpControl tdpControl(
            pm, GetConfigTdpControl::ErrorPolicy::NoThrow,
            static_cast<uint8_t>(level));
        return tdpControl.success();
    }
    bool bfSupported(unsigned int level) override
    {
        return GetConfigTdpControl(pm, static_cast<uint8_t>(level))
            .pbfSupport();
    }
    bool tfSupported(unsigned int level) override
    {
        return GetConfigTdpControl(pm, static_cast<uint8_t>(level))
            .factSupport();
    }
    bool bfEnabled(unsigned int level) override
    {
        return GetConfigTdpControl(pm, static_cast<uint8_t>(level))
            .pbfEnabled();
    }
    bool tfEnabled(unsigned int level) override
    {
        return GetConfigTdpControl(pm, static_cast<uint8_t>(level))
            .factEnabled();
    }
    unsigned int tdp(unsigned int level) override
    {
        return GetTdpInfo(pm, static_cast<uint8_t>(level)).pkgTdp();
    }
    unsigned int coreCount(unsigned int level) override
    {
        return enabledCoreList(level).size();
    }
    std::vector<unsigned int> enabledCoreList(unsigned int level) override
    {
        uint64_t coreMaskLo =
            GetCoreMask(pm, static_cast<uint8_t>(level), 0).coresMask();
        uint64_t coreMaskHi =
            GetCoreMask(pm, static_cast<uint8_t>(level), 1).coresMask();
        std::bitset<64> coreMask = (coreMaskHi << 32 | coreMaskLo);
        return convertMaskToList(coreMask);
    }
    std::vector<TurboEntry> sseTurboProfile(unsigned int level) override
    {
        // Read the Turbo Ratio Limit Cores MSR which is used to generate the
        // Turbo Profile for each profile. This is a package scope MSR, so just
        // read thread 0.
        uint64_t trlCores;
        uint8_t cc;
        EPECIStatus status = peci_RdIAMSR(static_cast<uint8_t>(address), 0,
                                          0x1AE, &trlCores, &cc);
        if (!checkPECIStatus(status, cc))
        {
            throw PECIError("Failed to read TRL MSR");
        }

        std::vector<TurboEntry> turboSpeeds;
        uint64_t limitRatioLo =
            GetTurboLimitRatios(pm, static_cast<uint8_t>(level), 0, 0).value;
        uint64_t limitRatioHi =
            GetTurboLimitRatios(pm, static_cast<uint8_t>(level), 1, 0).value;
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
        return turboSpeeds;
    }
    unsigned int p1Freq(unsigned int level) override
    {
        return GetRatioInfo(pm, static_cast<uint8_t>(level)).p1() * mhzPerRatio;
    }
    unsigned int p0Freq(unsigned int level) override
    {
        return GetRatioInfo(pm, static_cast<uint8_t>(level)).p0() * mhzPerRatio;
    }
    unsigned int prochotTemp(unsigned int level) override
    {
        return GetTjmaxInfo(pm, static_cast<uint8_t>(level)).tProchot();
    }
    std::vector<unsigned int>
        bfHighPriorityCoreList(unsigned int level) override
    {
        uint64_t coreMaskLo =
            PbfGetCoreMaskInfo(pm, static_cast<uint8_t>(level), 0)
                .p1HiCoreMask();
        uint64_t coreMaskHi =
            PbfGetCoreMaskInfo(pm, static_cast<uint8_t>(level), 1)
                .p1HiCoreMask();
        std::bitset<64> hiFreqCoreList = (coreMaskHi << 32) | coreMaskLo;
        return convertMaskToList(hiFreqCoreList);
    }
    unsigned int bfHighPriorityFreq(unsigned int level) override
    {
        return PbfGetP1HiP1LoInfo(pm, static_cast<uint8_t>(level)).p1Hi() *
               mhzPerRatio;
    }
    unsigned int bfLowPriorityFreq(unsigned int level) override
    {
        return PbfGetP1HiP1LoInfo(pm, static_cast<uint8_t>(level)).p1Lo() *
               mhzPerRatio;
    }

    void setBfEnabled(bool enable) override
    {
        GetConfigTdpControl getTDPControl(pm);
        bool tfEnabled = false;
        uint8_t param = (enable ? bit(1) : 0) | (tfEnabled ? bit(0) : 0);
        SetConfigTdpControl(pm, 0, 0, param);
    }
    void setTfEnabled(bool enable) override
    {
        // TODO: use cached BF value
        bool bfEnabled = false;
        uint8_t param = (bfEnabled ? bit(1) : 0) | (enable ? bit(0) : 0);
        SetConfigTdpControl(pm, 0, 0, param);
    }
    void setCurrentLevel(unsigned int level) override
    {
        SetLevel(pm, static_cast<uint8_t>(level));
    }
};

static std::unique_ptr<SSTInterface> createMailbox(uint8_t address,
                                                   CPUModel model)
{
    DEBUG_PRINT << "createMailbox\n";
    if (model == icx || model == icxd || model == spr)
    {
        return std::make_unique<SSTMailbox>(address, model);
    }

    return nullptr;
}

SSTProviderRegistration(createMailbox);

} // namespace sst
} // namespace cpu_info
