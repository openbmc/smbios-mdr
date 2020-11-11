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

#include <boost/asio/io_context.hpp>
#include <sdbusplus/asio/connection.hpp>

namespace cpu_info
{

/** Host states which are of interest just to cpuinfo use cases. */
enum class HostState
{
    /** Host CPU is powered off, no PECI communication is possible. */
    Off,
    /** Host CPU is powered on, but BIOS has not completed POST. */
    Booting,
    /** BIOS has completed POST. */
    Booted
};

/** Current host state - only valid after calling hostStateInit */
extern HostState hostState;

/**
 * Initialize the host state and register D-Bus match handlers to keep the state
 * current.
 *
 * @param[in,out]   ioc     ASIO IO context/service
 * @param[in,out]   conn    D-Bus ASIO connection.
 */
void hostStateInit(boost::asio::io_context& ioc,
                   const std::shared_ptr<sdbusplus::asio::connection>& conn);

}
