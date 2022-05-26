// Copyright (c) 2021 Intel Corporation
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

#include <boost/asio/io_context.hpp>
#include <sdbusplus/asio/connection.hpp>

#define DEBUG_PRINT                                                            \
    if constexpr (false)                                                       \
    std::cerr

namespace cpu_info
{

/** Host states which are of interest just to cpuinfo use cases. */
enum class HostState
{
    /** Host CPU is powered off. */
    off,
    /** Host CPU is powered on, but BIOS has not completed POST. */
    postInProgress,
    /** BIOS has completed POST. */
    postComplete
};

/** Current host state - initialized to "off" */
extern HostState hostState;

/**
 * Register D-Bus match handlers to keep hostState current. The D-Bus logic is
 * entirely asynchronous, so hostState will never change from "off" until after
 * this function is called and the asio event loop is running.
 *
 * @param[in]   conn    D-Bus ASIO connection.
 */
void hostStateSetup(const std::shared_ptr<sdbusplus::asio::connection>& conn);

/**
 * Callback which is run whenever the HostState changes. First parameter is the
 * old state, and second parameter is the new current state.
 */
using HostStateHandler = std::function<void(HostState, HostState)>;
void addHostStateCallback(HostStateHandler cb);

constexpr uint64_t bit(uint8_t index)
{
    return (1ull << index);
}

/**
 * Extract a bitfield from an input data by shifting and masking.
 *
 * @tparam Dest Destination type - mostly useful to avoid an extra static_cast
 *              at the call site. Defaults to the Src type if unspecified.
 * @tparam Src  Automatically deduced from the first positional parameter.
 *
 * @param data  Input data.
 * @param loBit 0-based index of the least significant bit to return.
 * @param hiBit 0-based index of the most significant bit to return.
 */
template <typename Dest = std::monostate, typename Src>
auto mask(Src data, unsigned int loBit, unsigned int hiBit)
{
    assert(hiBit >= loBit);
    uint64_t d = data;
    d >>= loBit;
    d &= (1ull << (hiBit - loBit + 1)) - 1;
    if constexpr (std::is_same_v<Dest, std::monostate>)
    {
        return static_cast<Src>(d);
    }
    else
    {
        return static_cast<Dest>(d);
    }
}

namespace dbus
{
boost::asio::io_context& getIOContext();
std::shared_ptr<sdbusplus::asio::connection> getConnection();
} // namespace dbus

} // namespace cpu_info
