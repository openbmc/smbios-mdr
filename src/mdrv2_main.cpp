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

namespace phosphor
{
namespace smbios
{

auto io = std::make_shared<boost::asio::io_context>();
auto connection = std::make_shared<sdbusplus::asio::connection>(*io);
auto objServer = std::make_shared<sdbusplus::asio::object_server>(connection);

std::shared_ptr<sdbusplus::asio::object_server> getObjectServer()
{
    return objServer;
}

} // namespace smbios
} // namespace phosphor

int main(void)
{
    using phosphor::smbios::connection;
    using phosphor::smbios::io;

    sdbusplus::server::manager_t objManager(*connection,
                                            "/xyz/openbmc_project/inventory");

    connection->request_name("xyz.openbmc_project.Smbios.MDR_V2");

    phosphor::smbios::MDRV2 mdrV2(connection, io, mdrDefaultFile,
                                  phosphor::smbios::defaultObjectPath,
                                  phosphor::smbios::defaultInventoryPath);

    io.run();

    return 0;
}
