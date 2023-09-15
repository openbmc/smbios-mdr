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

int main()
{
    auto io = std::make_shared<boost::asio::io_context>();
    auto connection = std::make_shared<sdbusplus::asio::connection>(*io);
    auto objServer =
        std::make_shared<sdbusplus::asio::object_server>(connection);

    sdbusplus::server::manager_t objManager(*connection,
                                            "/xyz/openbmc_project/inventory");

    connection->request_name("xyz.openbmc_project.Smbios.MDR_V2");

    auto mdrV2 = std::make_shared<phosphor::smbios::MDRV2>(
        io, connection, objServer, mdrDefaultFile,
        phosphor::smbios::defaultObjectPath,
        phosphor::smbios::defaultInventoryPath);

    io->run();

    return 0;
}
