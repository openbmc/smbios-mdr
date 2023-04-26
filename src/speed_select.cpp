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

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <xyz/openbmc_project/Common/Device/error.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Control/Processor/CurrentOperatingConfig/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cpu/OperatingConfig/server.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace cpu_info
{
namespace sst
{

// Specialize char to print the integer value instead of ascii. We basically
// never want to print a single ascii char.
std::ostream& operator<<(std::ostream& os, uint8_t value)
{
    return os << static_cast<int>(value);
}

bool checkPECIStatus(EPECIStatus libStatus, uint8_t completionCode)
{
    if (libStatus != PECI_CC_SUCCESS || completionCode != PECI_DEV_CC_SUCCESS)
    {
        std::cerr << "PECI command failed."
                  << " Driver Status = " << libStatus << ","
                  << " Completion Code = " << completionCode << '\n';
        return false;
    }
    return true;
}

std::vector<uint32_t> convertMaskToList(std::bitset<64> mask)
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

static std::vector<BackendProvider>& getProviders()
{
    static auto* providers = new std::vector<BackendProvider>;
    return *providers;
}

void registerBackend(BackendProvider providerFn)
{
    getProviders().push_back(providerFn);
}

std::unique_ptr<SSTInterface> getInstance(uint8_t address, CPUModel model)
{
    DEBUG_PRINT << "Searching for provider for " << address << ", model "
                << std::hex << model << std::dec << '\n';
    for (const auto& provider : getProviders())
    {
        try
        {
            auto interface = provider(address, model);
            DEBUG_PRINT << "returned " << interface << '\n';
            if (interface)
            {
                return interface;
            }
        }
        catch (...)
        {}
    }
    DEBUG_PRINT << "No supported backends found\n";
    return nullptr;
}

using BaseCurrentOperatingConfig =
    sdbusplus::server::object_t<sdbusplus::server::xyz::openbmc_project::
                                    control::processor::CurrentOperatingConfig>;

using BaseOperatingConfig =
    sdbusplus::server::object_t<sdbusplus::server::xyz::openbmc_project::
                                    inventory::item::cpu::OperatingConfig>;

class OperatingConfig : public BaseOperatingConfig
{
  public:
    std::string path;
    unsigned int level;

  public:
    using BaseOperatingConfig::BaseOperatingConfig;
    OperatingConfig(sdbusplus::bus_t& bus, unsigned int level_,
                    std::string path_) :
        BaseOperatingConfig(bus, path_.c_str(), action::defer_emit),
        path(std::move(path_)), level(level_)
    {}
};

class CPUConfig : public BaseCurrentOperatingConfig
{
  private:
    /** Objects describing all available SST configs - not modifiable. */
    std::vector<std::unique_ptr<OperatingConfig>> availConfigs;
    sdbusplus::bus_t& bus;
    const uint8_t peciAddress;
    const std::string path; ///< D-Bus path of CPU object
    const CPUModel cpuModel;

    // Keep mutable copies of the properties so we can cache values that we
    // retrieve in the getters. We don't want to throw an error on a D-Bus
    // get-property call (extra error handling in clients), so by caching we can
    // hide any temporary hiccup in PECI communication.
    // These values can be changed by in-band software so we have to do a full
    // PECI read on every get-property, and can't assume that values will change
    // only when set-property is done.
    mutable unsigned int currentLevel;
    mutable bool bfEnabled;

    /**
     * Enforce common pre-conditions for D-Bus set property handlers.
     */
    void setPropertyCheckOrThrow(SSTInterface& sst)
    {
        if (!sst.supportsControl())
        {
            throw sdbusplus::xyz::openbmc_project::Common::Error::NotAllowed();
        }
        if (hostState != HostState::postComplete || !sst.ready())
        {
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
    }

  public:
    CPUConfig(sdbusplus::bus_t& bus_, uint8_t index, CPUModel model) :
        BaseCurrentOperatingConfig(bus_, generatePath(index).c_str(),
                                   action::defer_emit),
        bus(bus_), peciAddress(index + MIN_CLIENT_ADDR),
        path(generatePath(index)), cpuModel(model), currentLevel(0),
        bfEnabled(false)
    {}

    //
    // D-Bus Property Overrides
    //

    sdbusplus::message::object_path appliedConfig() const override
    {
        DEBUG_PRINT << "Reading AppliedConfig\n";
        if (hostState != HostState::off)
        {
            // Otherwise, try to read current state
            auto sst = getInstance(peciAddress, cpuModel);
            if (!sst || !sst->ready())
            {
                std::cerr << __func__
                          << ": Failed to get SST provider instance\n";
            }
            else
            {
                try
                {
                    currentLevel = sst->currentLevel();
                }
                catch (const PECIError& error)
                {
                    std::cerr << "Failed to get SST-PP level: " << error.what()
                              << "\n";
                }
            }
        }
        return generateConfigPath(currentLevel);
    }

    bool baseSpeedPriorityEnabled() const override
    {
        DEBUG_PRINT << "Reading BaseSpeedPriorityEnabled\n";
        if (hostState != HostState::off)
        {
            auto sst = getInstance(peciAddress, cpuModel);
            if (!sst || !sst->ready())
            {
                std::cerr << __func__
                          << ": Failed to get SST provider instance\n";
            }
            else
            {
                try
                {
                    bfEnabled = sst->bfEnabled(currentLevel);
                }
                catch (const PECIError& error)
                {
                    std::cerr << "Failed to get SST-BF status: " << error.what()
                              << "\n";
                }
            }
        }
        return bfEnabled;
    }

    sdbusplus::message::object_path
        appliedConfig(sdbusplus::message::object_path value) override
    {
        DEBUG_PRINT << "Writing AppliedConfig\n";
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

        auto sst = getInstance(peciAddress, cpuModel);
        if (!sst)
        {
            std::cerr << __func__ << ": Failed to get SST provider instance\n";
            return sdbusplus::message::object_path();
        }
        setPropertyCheckOrThrow(*sst);
        try
        {
            sst->setCurrentLevel(newConfig->level);
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

    bool baseSpeedPriorityEnabled(bool /* value */) override
    {
        DEBUG_PRINT << "Writing BaseSpeedPriorityEnabled not allowed\n";
        throw sdbusplus::xyz::openbmc_project::Common::Error::NotAllowed();
        // return value not used
        return false;
    }

    //
    // Additions
    //

    OperatingConfig& newConfig(unsigned int level)
    {
        availConfigs.emplace_back(std::make_unique<OperatingConfig>(
            bus, level, generateConfigPath(level)));
        return *availConfigs.back();
    }

    std::string generateConfigPath(unsigned int level) const
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
 * @param[in,out]   sst         Interface to SST backend.
 * @param[in]       level       Config TDP level to retrieve.
 * @param[out]      config      D-Bus interface to update.
 */
static void getSingleConfig(SSTInterface& sst, unsigned int level,
                            OperatingConfig& config)
{
    config.powerLimit(sst.tdp(level));
    DEBUG_PRINT << " TDP = " << config.powerLimit() << '\n';

    config.availableCoreCount(sst.coreCount(level));
    DEBUG_PRINT << " coreCount = " << config.availableCoreCount() << '\n';

    config.baseSpeed(sst.p1Freq(level));
    DEBUG_PRINT << " baseSpeed = " << config.baseSpeed() << '\n';

    config.maxSpeed(sst.p0Freq(level));
    DEBUG_PRINT << " maxSpeed = " << config.maxSpeed() << '\n';

    config.maxJunctionTemperature(sst.prochotTemp(level));
    DEBUG_PRINT << " procHot = " << config.maxJunctionTemperature() << '\n';

    // Construct BaseSpeedPrioritySettings
    std::vector<std::tuple<uint32_t, std::vector<uint32_t>>> baseSpeeds;
    if (sst.bfSupported(level))
    {
        std::vector<uint32_t> totalCoreList, loFreqCoreList, hiFreqCoreList;
        totalCoreList = sst.enabledCoreList(level);
        hiFreqCoreList = sst.bfHighPriorityCoreList(level);
        std::set_difference(
            totalCoreList.begin(), totalCoreList.end(), hiFreqCoreList.begin(),
            hiFreqCoreList.end(),
            std::inserter(loFreqCoreList, loFreqCoreList.begin()));

        baseSpeeds = {{sst.bfHighPriorityFreq(level), hiFreqCoreList},
                      {sst.bfLowPriorityFreq(level), loFreqCoreList}};
    }
    config.baseSpeedPrioritySettings(baseSpeeds);

    config.turboProfile(sst.sseTurboProfile(level));
}

/**
 * Retrieve all SST configuration info for all discoverable CPUs, and publish
 * the info on new D-Bus objects on the given bus connection.
 *
 * @param[in,out]   ioc     ASIO context.
 * @param[in,out]   conn    D-Bus ASIO connection.
 *
 * @return  Whether discovery was successfully finished.
 *
 * @throw PECIError     A PECI command failed on a CPU which had previously
 *                      responded to a command.
 */
static bool discoverCPUsAndConfigs(boost::asio::io_context& ioc,
                                   sdbusplus::asio::connection& conn)
{
    // Persistent list - only populated after complete/successful discovery
    static std::vector<std::unique_ptr<CPUConfig>> cpus;
    cpus.clear();

    // Temporary staging list. In case there is any failure, these temporary
    // objects will get dropped to avoid presenting incomplete info until the
    // next discovery attempt.
    std::vector<std::unique_ptr<CPUConfig>> cpuList;

    for (uint8_t i = MIN_CLIENT_ADDR; i <= MAX_CLIENT_ADDR; ++i)
    {
        // Let the event handler run any waiting tasks. If there is a lot of
        // PECI contention, SST discovery could take a long time. This lets us
        // get updates to hostState and handle any D-Bus requests.
        ioc.poll();

        if (hostState == HostState::off)
        {
            return false;
        }

        unsigned int cpuIndex = i - MIN_CLIENT_ADDR;
        DEBUG_PRINT << "Discovering CPU " << cpuIndex << '\n';

        // We could possibly check D-Bus for CPU presence and model, but PECI is
        // 10x faster and so much simpler.
        uint8_t cc, stepping;
        CPUModel cpuModel;
        EPECIStatus status = peci_GetCPUID(i, &cpuModel, &stepping, &cc);
        if (status == PECI_CC_TIMEOUT)
        {
            // Timing out indicates the CPU is present but PCS services not
            // working yet. Try again later.
            throw PECIError("Get CPUID timed out");
        }
        if (status == PECI_CC_CPU_NOT_PRESENT)
        {
            continue;
        }
        if (status != PECI_CC_SUCCESS || cc != PECI_DEV_CC_SUCCESS)
        {
            std::cerr << "GetCPUID returned status " << status
                      << ", cc = " << cc << '\n';
            continue;
        }

        std::unique_ptr<SSTInterface> sst = getInstance(i, cpuModel);

        if (!sst)
        {
            // No supported backend for this CPU.
            continue;
        }

        if (!sst->ready())
        {
            // Supported CPU but it can't be queried yet. Try again later.
            std::cerr << "sst not ready yet\n";
            return false;
        }

        if (!sst->ppEnabled())
        {
            // Supported CPU but the specific SKU doesn't support SST-PP.
            std::cerr << "CPU doesn't support SST-PP\n";
            continue;
        }

        // Create the per-CPU configuration object
        cpuList.emplace_back(
            std::make_unique<CPUConfig>(conn, cpuIndex, cpuModel));
        CPUConfig& cpu = *cpuList.back();

        bool foundCurrentLevel = false;

        for (unsigned int level = 0; level <= sst->maxLevel(); ++level)
        {
            DEBUG_PRINT << "checking level " << level << ": ";
            // levels 1 and 2 were legacy/deprecated, originally used for AVX
            // license pre-granting. They may be reused for more levels in
            // future generations. So we need to check for discontinuities.
            if (!sst->levelSupported(level))
            {
                DEBUG_PRINT << "not supported\n";
                continue;
            }

            DEBUG_PRINT << "supported\n";

            getSingleConfig(*sst, level, cpu.newConfig(level));

            if (level == sst->currentLevel())
            {
                foundCurrentLevel = true;
            }
        }

        DEBUG_PRINT << "current level is " << sst->currentLevel() << '\n';

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
    }

    cpuList.swap(cpus);
    std::for_each(cpus.begin(), cpus.end(), [](auto& cpu) { cpu->finalize(); });
    return true;
}

/**
 * Attempt discovery process, and if it fails, wait for 10 seconds to try again.
 */
static void discoverOrWait()
{
    static boost::asio::steady_timer peciRetryTimer(dbus::getIOContext());
    static int peciErrorCount = 0;
    bool finished = false;

    // This function may be called from hostStateHandler or by retrying itself.
    // In case those overlap, cancel any outstanding retry timer.
    peciRetryTimer.cancel();

    try
    {
        DEBUG_PRINT << "Starting discovery\n";
        finished = discoverCPUsAndConfigs(dbus::getIOContext(),
                                          *dbus::getConnection());
    }
    catch (const PECIError& err)
    {
        std::cerr << "PECI Error: " << err.what() << '\n';

        // In case of repeated failure to finish discovery, turn off this
        // feature altogether. Possible cause is that the CPU model does not
        // actually support the necessary commands.
        if (++peciErrorCount >= 50)
        {
            std::cerr << "Aborting SST discovery\n";
            return;
        }

        std::cerr << "Retrying SST discovery later\n";
    }

    DEBUG_PRINT << "Finished discovery attempt: " << finished << '\n';

    // Retry later if no CPUs were available, or there was a PECI error.
    if (!finished)
    {
        peciRetryTimer.expires_after(std::chrono::seconds(10));
        peciRetryTimer.async_wait([](boost::system::error_code ec) {
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                {
                    std::cerr << "SST PECI Retry Timer failed: " << ec << '\n';
                }
                return;
            }
            discoverOrWait();
        });
    }
}

static void hostStateHandler(HostState prevState, HostState)
{
    if (prevState == HostState::off)
    {
        // Start or re-start discovery any time the host moves out of the
        // powered off state.
        discoverOrWait();
    }
}

void init()
{
    addHostStateCallback(hostStateHandler);
}

} // namespace sst
} // namespace cpu_info
