/*
// Copyright (c) 2018 Intel Corporation
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
*/

#include "mdrv2.hpp"

#include <boost/asio/io_context.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

boost::asio::io_context io;
auto connection = std::make_shared<sdbusplus::asio::connection>(io);
auto objServer = sdbusplus::asio::object_server(connection);

sdbusplus::asio::object_server& getObjectServer(void)
{
    return objServer;
}

// Hello World: This change is just to trigger a new Jenkins run: 2
int main(void)
{
    sdbusplus::bus_t& bus = static_cast<sdbusplus::bus_t&>(*connection);
    sdbusplus::server::manager_t objManager(bus,
                                            "/xyz/openbmc_project/inventory");

    bus.request_name("xyz.openbmc_project.Smbios.MDR_V2");

    phosphor::smbios::MDRV2 mdrV2(bus, phosphor::smbios::mdrV2Path, io);

    io.run();

    return 0;
}
