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
#include <iostream>
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

int main(void)
{

    connection->async_method_call(
        [](boost::system::error_code ec, const std::string& motherboard) {
            if (ec)
            {
                std::cerr << "error with aync_method_call\n";
                return;
            }
            if (motherboard.empty())
            {
                return;
            }
            cpuPath = motherboard + "/cpu";
            dimmPath = motherboard + "/dimm";
            systemPath = motherboard + "/bios";
        },
        "xyz.openbmc_project.EntityManager",
        "/xyz/openbmc_project/EntityManager",
        "xyz.openbmc_project.EntityManager", "ReScan");

    sdbusplus::bus::bus& bus = static_cast<sdbusplus::bus::bus&>(*connection);
    sdbusplus::server::manager::manager objManager(
        bus, "/xyz/openbmc_project/inventory");

    bus.request_name("xyz.openbmc_project.Smbios.MDR_V2");

    phosphor::smbios::MDR_V2 mdrV2(bus, phosphor::smbios::mdrV2Path, io);

    io.run();

    return 0;
}
