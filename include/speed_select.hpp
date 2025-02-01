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
#pragma once

#include <peci.h>

#include <boost/asio/io_context.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <bitset>
#include <iostream>

namespace cpu_info
{
namespace sst
{

/**
 * Initialize SST subsystem.
 *
 * This will schedule work to be done when the host is ready, in order to
 * retrieve all SST configuration info for all discoverable CPUs, and publish
 * the info on new D-Bus objects on the given bus connection.
 */
void init();

class PECIError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

bool checkPECIStatus(EPECIStatus libStatus, uint8_t completionCode);

constexpr int extendedModel(CPUModel model)
{
    return (model >> 16) & 0xF;
}

/**
 * Construct a list of indexes of the set bits in the input value.
 * E.g. fn(0x7A) -> {1,3,4,5,6}
 *
 * @param[in]   mask    Bitmask to convert.
 *
 * @return  List of bit indexes.
 */
std::vector<uint32_t> convertMaskToList(std::bitset<64> mask);

using TurboEntry = std::tuple<uint32_t, size_t>;

/**
 * Abstract interface that must be implemented by backends, allowing discovery
 * and control of a single CPU package.
 */
class SSTInterface
{
  public:
    virtual ~SSTInterface() {}

    /**
     * Whether the interface is ready to be used, or we need to wait longer. The
     * backend may need to wait e.g. for the host BIOS to initialize the
     * interface.
     */
    virtual bool ready() = 0;

    /** Whether the processor supports the control ("set") functions. */
    virtual bool supportsControl() = 0;

    /** Whether SST-PP is enabled on the processor. */
    virtual bool ppEnabled() = 0;
    /** Return the current SST-PP configuration level */
    virtual unsigned int currentLevel() = 0;
    /** Return the maximum valid SST-PP configuration level */
    virtual unsigned int maxLevel() = 0;

    /**
     * Whether the given level is supported. The level indices may be
     * discontinuous, so this function should be used before querying deeper
     * properties of a level.
     */
    virtual bool levelSupported(unsigned int level) = 0;
    /** Whether SST-BF is supported in a given level. */
    virtual bool bfSupported(unsigned int level) = 0;
    /** Whether SST-TF is supported in a given level. */
    virtual bool tfSupported(unsigned int level) = 0;
    /** Whether SST-BF is enabled in a given level. */
    virtual bool bfEnabled(unsigned int level) = 0;
    /** Whether SST-TF is enabled in a given level. */
    virtual bool tfEnabled(unsigned int level) = 0;
    /** Return the package Thermal Design Power in Watts for a given level. */
    virtual unsigned int tdp(unsigned int level) = 0;
    /** Return the number of cores enabled in a given level. */
    virtual unsigned int coreCount(unsigned int level) = 0;
    /** Return the list of enabled logical core indices for a given level. */
    virtual std::vector<unsigned int> enabledCoreList(unsigned int level) = 0;
    /**
     * Return the list of TurboEntrys which define the SSE turbo profile for a
     * given level.
     */
    virtual std::vector<TurboEntry> sseTurboProfile(unsigned int level) = 0;
    /** Return the base frequency (P1) for a given level. */
    virtual unsigned int p1Freq(unsigned int level) = 0;
    /** Return the maximum single-core frequency (P0) for a given level. */
    virtual unsigned int p0Freq(unsigned int level) = 0;
    /**
     * Return the DTS max or external Prochot temperature in degrees Celsius
     * for a given level.
     */
    virtual unsigned int prochotTemp(unsigned int level) = 0;
    /**
     * Return the list of logical core indices which have high priority when
     * SST-BF is enabled for a given level.
     */
    virtual std::vector<unsigned int> bfHighPriorityCoreList(
        unsigned int level) = 0;
    /** Return the high priority base frequency for a given level. */
    virtual unsigned int bfHighPriorityFreq(unsigned int level) = 0;
    /** Return the low priority base frequency for a given level. */
    virtual unsigned int bfLowPriorityFreq(unsigned int level) = 0;

    /** Enable or disable SST-BF for the current configuration. */
    virtual void setBfEnabled(bool enable) = 0;
    /** Enable or disable SST-TF for the current configuration. */
    virtual void setTfEnabled(bool enable) = 0;
    /** Change the current configuration to the given level. */
    virtual void setCurrentLevel(unsigned int level) = 0;
};

/**
 * Policy for whether the SST interface should wake up an idle CPU to complete
 * requested operations. Waking should be used sparingly to avoid excess CPU
 * power draw, so the policy depends on the context.
 */
enum WakePolicy
{
    /**
     * If CPU rejects the request due to being in a low-power state, enable
     * wake-on-PECI on the CPU and retry. Wake-on-PECI is disabled for the CPU
     * when the SST interface is destroyed.
     */
    wakeAllowed,

    /**
     * If CPU rejects the request due to being in a low-power state, it results
     * in a PECIError exception.
     */
    dontWake
};

/**
 * BackendProvider represents a function which may create an SSTInterface given
 * a CPU PECI address, and the CPU Model information. Usually the CPUModel is
 * sufficient to determine if the backend is supported.
 * Backend should return nullptr to indicate it doesn't support a given CPU.
 * The SST upper layer will call the registered backend provider functions in
 * arbitrary order until one of them returns a non-null pointer.
 */
using BackendProvider =
    std::function<std::unique_ptr<SSTInterface>(uint8_t, CPUModel, WakePolicy)>;

/**
 * Backends should use 1 instance of the SSTProviderRegistration macro at file
 * scope to register their provider function. This static struct instance
 * register the backend before main() is run, and prevents the upper layer from
 * having to know about which backends exist.
 */
#define SSTProviderRegistration(fn)                                            \
    struct fn##Register                                                        \
    {                                                                          \
        fn##Register()                                                         \
        {                                                                      \
            std::cerr << "Registering SST Provider " #fn << std::endl;         \
            registerBackend(fn);                                               \
        }                                                                      \
    } fn##Instance;
void registerBackend(BackendProvider);

} // namespace sst
} // namespace cpu_info
