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

#include "system.hpp"

#include "mdrv2.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

static constexpr const char* biosActiveObjPath =
    "/xyz/openbmc_project/software/bios_active";
static constexpr const char* biosVersionIntf =
    "xyz.openbmc_project.Software.Version";
static constexpr const char* biosVersionProp = "Version";

namespace phosphor
{
namespace smbios
{

std::string System::uuid(std::string /* value */)
{
    uint8_t* dataIn = storage;
    dataIn = getSMBIOSTypePtr(dataIn, systemType);
    if (dataIn != nullptr)
    {
        auto systemInfo = reinterpret_cast<struct SystemInfo*>(dataIn);
        std::stringstream stream;
        stream << std::setfill('0') << std::hex;
        stream << std::setw(8) << systemInfo->uuid.timeLow;
        stream << "-";
        stream << std::setw(4) << systemInfo->uuid.timeMid;
        stream << "-";
        stream << std::setw(4) << systemInfo->uuid.timeHiAndVer;
        stream << "-";
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.clockSeqHi);
        stream << std::setw(2)
               << static_cast<int>(systemInfo->uuid.clockSeqLow);
        stream << "-";
        static_assert(sizeof(systemInfo->uuid.node) == 6);
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.node[0]);
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.node[1]);
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.node[2]);
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.node[3]);
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.node[4]);
        stream << std::setw(2) << static_cast<int>(systemInfo->uuid.node[5]);

        return sdbusplus::server::xyz::openbmc_project::common::UUID::uuid(
            stream.str());
    }

    return sdbusplus::server::xyz::openbmc_project::common::UUID::uuid(
        "00000000-0000-0000-0000-000000000000");
}

static std::string getService(sdbusplus::bus_t& bus,
                              const std::string& objectPath,
                              const std::string& interface)
{
    auto method = bus.new_method_call("xyz.openbmc_project.ObjectMapper",
                                      "/xyz/openbmc_project/object_mapper",
                                      "xyz.openbmc_project.ObjectMapper",
                                      "GetObject");

    method.append(objectPath);
    method.append(std::vector<std::string>({interface}));

    std::vector<std::pair<std::string, std::vector<std::string>>> response;

    try
    {
        auto reply = bus.call(method);
        reply.read(response);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Error in mapper method call - {ERROR}, SERVICE - "
                   "{SERVICE}, PATH - {PATH}",
                   "ERROR", e.what(), "SERVICE", objectPath.c_str(), "PATH",
                   interface.c_str());

        return std::string{};
    }

    return response[0].first;
}

static void setProperty(sdbusplus::bus_t& bus, const std::string& objectPath,
                        const std::string& interface,
                        const std::string& propertyName,
                        const std::string& value)
{
    auto service = getService(bus, objectPath, interface);
    if (service.empty())
    {
        return;
    }

    auto method = bus.new_method_call(service.c_str(), objectPath.c_str(),
                                      "org.freedesktop.DBus.Properties", "Set");
    method.append(interface.c_str(), propertyName.c_str(),
                  std::variant<std::string>{value});

    bus.call_noreply(method);
}

std::string System::version(std::string /* value */)
{
    std::string result = "No BIOS Version";
    uint8_t* dataIn = storage;
    dataIn = getSMBIOSTypePtr(dataIn, biosType);
    if (dataIn != nullptr)
    {
        auto biosInfo = reinterpret_cast<struct BIOSInfo*>(dataIn);
        std::string tempS = positionToString(biosInfo->biosVersion,
                                             biosInfo->length, dataIn);
        if (std::find_if(tempS.begin(), tempS.end(),
                         [](char ch) { return !isprint(ch); }) != tempS.end())
        {
            std::ofstream smbiosFile(smbiosFilePath, std::ios_base::trunc);
            if (!smbiosFile.good())
            {
                phosphor::logging::log<phosphor::logging::level::ERR>(
                    "Open MDRV2 table file failure");
                return result;
            }
            smbiosFile.clear();
            smbiosFile.close();
            phosphor::logging::log<phosphor::logging::level::ERR>(
                "Find non-print char, delete the broken MDRV2 table file!");
            return sdbusplus::server::xyz::openbmc_project::inventory::
                decorator::Revision::version(result);
        }
        result = tempS;

        setProperty(bus, biosActiveObjPath, biosVersionIntf, biosVersionProp,
                    result);
    }
    lg2::info("VERSION INFO - BIOS - {VER}", "VER", result);

    return sdbusplus::server::xyz::openbmc_project::inventory::decorator::
        Revision::version(result);
}

} // namespace smbios
} // namespace phosphor
