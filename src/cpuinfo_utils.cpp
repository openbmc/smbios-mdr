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

#include "cpuinfo_utils.hpp"

#include <xyz/openbmc_project/State/Host/server.hpp>
#include <xyz/openbmc_project/State/OperatingSystem/Status/server.hpp>

#include <iostream>
#include <type_traits>
#include <utility>
#include <variant>

namespace cpu_info
{

using namespace sdbusplus::xyz::openbmc_project;
using PowerState = State::server::Host::HostState;
using OSStatus = State::OperatingSystem::server::Status::OSStatus;

HostState hostState = HostState::Off;
PowerState powerState;
OSStatus osStatus;

static boost::asio::io_context* asioContext;
static std::shared_ptr<sdbusplus::asio::connection> dbusConn;

void updateHostState()
{
    if (powerState == PowerState::Off)
    {
        hostState = HostState::Off;
    }
    else if (osStatus == OSStatus::Inactive)
    {
        hostState = HostState::Booting;
    }
    else
    {
        hostState = HostState::Booted;
    }
}

/**
 * Register a handler to be called whenever the given property is changed. Also
 * call the handler once immediately with the current property value.
 *
 * Since this necessarily reads all properties in the given interface, type
 * information about the interface may need to be provided via
 * CustomVariantArgs.
 *
 * @tparam  CustomVariantTypes  Any property types contained in the interface
 *                              beyond the base data types (numeric and
 *                              string-like types) and Handler's param type.
 * @tparam  Handler     Automatically deduced. Must be a callable taking a
 *                      single parameter whose type matches the property.
 *
 * @param[in]   service     D-Bus service name.
 * @param[in]   object      D-Bus object name.
 * @param[in]   interface   D-Bus interface name.
 * @param[in]   propertyName    D-Bus property name. This is captured in a
 *                              closure so must stay alive for the duration of
 *                              the program.
 * @param[in]   handler     Callable to be called immediately and upon any
 *                          changes in the property value.
 */
template <typename... CustomVariantTypes, typename Handler>
static void subscribeToProperty(const char* service, const char* object,
                                const char* interface, const char* propertyName,
                                Handler&& handler)
{
    using PropertyType = std::remove_const_t<std::remove_reference_t<
        std::tuple_element_t<0, boost::callable_traits::args_t<Handler>>>>;
    using InterfaceVariant = typename sdbusplus::utility::dedup_variant<
        PropertyType, CustomVariantTypes..., bool, uint8_t, uint16_t, int16_t,
        uint32_t, int32_t, uint64_t, int64_t, size_t, ssize_t, double,
        std::string, sdbusplus::message::object_path>;
    InterfaceVariant propVariant{};

    auto method = dbusConn->new_method_call(
        service, object, "org.freedesktop.DBus.Properties", "Get");
    method.append(interface, propertyName);

    auto reply = dbusConn->call(method);
    reply.read(propVariant);

    if (auto val = std::get_if<PropertyType>(&propVariant))
    {
        handler(*val);
    }

    // Leak the match. We want the match to exist for the lifetime of the
    // program so there is no need to manage the memory.
    new sdbusplus::bus::match_t(
        *dbusConn,
        sdbusplus::bus::match::rules::sender(service) +
            sdbusplus::bus::match::rules::propertiesChanged(object, interface),
        [handler = std::move(handler),
         propertyName](sdbusplus::message::message& reply) {
            std::string matchInterface;

            // ignore, it has to be correct
            reply.read(matchInterface);

            std::vector<std::pair<std::string, InterfaceVariant>> changedProps;
            reply.read(changedProps);

            for (const auto& [prop, val] : changedProps)
            {
                if (prop == propertyName &&
                    std::holds_alternative<PropertyType>(val))
                {
                    handler(std::get<PropertyType>(val));
                }
            }
        });
}

void updatePowerState(const std::string& newState)
{
    powerState = State::server::Host::convertHostStateFromString(newState);
    updateHostState();
}

void updateBiosState(const std::string& newState)
{
    std::string fullEnum(newState);
    if (fullEnum.rfind("xyz.openbmc_project", 0) != 0)
    {
        fullEnum =
            "xyz.openbmc_project.State.OperatingSystem.Status.OSStatus." +
            newState;
    }
    osStatus =
        State::OperatingSystem::server::Status::convertOSStatusFromString(
            fullEnum);
    updateHostState();
}

void hostStateInit(boost::asio::io_context& ioc,
                   const std::shared_ptr<sdbusplus::asio::connection>& conn)
{
    asioContext = &ioc;
    dbusConn = conn;

    subscribeToProperty(
        "xyz.openbmc_project.State.Host", "/xyz/openbmc_project/state/host0",
        State::server::Host::interface, "CurrentHostState", updatePowerState);
    subscribeToProperty("xyz.openbmc_project.State.OperatingSystem",
                        "/xyz/openbmc_project/state/os",
                        State::OperatingSystem::server::Status::interface,
                        "OperatingSystemState", updateBiosState);
}

} // namespace cpu_info
