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

#include <sdbusplus/asio/connection.hpp>
#include <boost/asio/io_context.hpp>

namespace cpu_info
{
namespace sst
{

/**
 * Retrieve all SST configuration info for all discoverable CPUs, and publish
 * the info on new D-Bus objects on the given bus connection.
 *
 * This function may block until all discovery is completed (many seconds), or
 * it may schedule the work to be done at a later time (on the given ASIO
 * context) if CPUs are not currently available, and may also schedule periodic
 * work to be done after initial discovery is completed.
 *
 * @param[in,out]   ioc     ASIO IO context/service
 * @param[in,out]   conn    D-Bus ASIO connection.
 */
void init(boost::asio::io_context& ioc,
          const std::shared_ptr<sdbusplus::asio::connection>& conn);

} // namespace sst
} // namespace cpu_info
