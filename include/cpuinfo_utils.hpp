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

} // namespace cpu_info
