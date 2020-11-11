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

// Include the server headers to get the enum<->string conversion functions
#include <xyz/openbmc_project/State/Host/server.hpp>

#include <iostream>
#include <type_traits>
#include <utility>
#include <variant>

namespace cpu_info
{

using namespace sdbusplus::xyz::openbmc_project;
using PowerState = State::server::Host::HostState;

HostState hostState = HostState::Off;
PowerState powerState;
bool biosDone;

static boost::asio::io_context* asioContext;
static std::shared_ptr<sdbusplus::asio::connection> dbusConn;

void updateHostState()
{
    if (powerState == PowerState::Off)
    {
        hostState = HostState::Off;
    }
    else if (!biosDone)
    {
        hostState = HostState::PostInProgress;
    }
    else
    {
        hostState = HostState::PostComplete;
    }
    std::cout << "new host state: " << static_cast<int>(hostState) << "\n";
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
    // Type of first parameter to Handler, with const/& removed
    using PropertyType = std::remove_const_t<std::remove_reference_t<
        std::tuple_element_t<0, boost::callable_traits::args_t<Handler>>>>;
    // Base data types which we can handle by default
    using InterfaceVariant = typename sdbusplus::utility::dedup_variant<
        PropertyType, CustomVariantTypes..., bool, uint8_t, uint16_t, int16_t,
        uint32_t, int32_t, uint64_t, int64_t, size_t, ssize_t, double,
        std::string, sdbusplus::message::object_path>;
    InterfaceVariant propVariant{};

    // TODO error handling for initial read

    auto method = dbusConn->new_method_call(
        service, object, "org.freedesktop.DBus.Properties", "Get");
    method.append(interface, propertyName);

    sdbusplus::message::message reply;
    try
    {
        reply = dbusConn->call(method);
    }
    catch (const sdbusplus::exception_t& err)
    {
        // The service may be down now but we can still continue to add the
        // match since the service may work in the future.
        std::cerr << "Unable to read property " << propertyName
                  << ". Method call failure: " << err.what() << "\n";
    }

    reply.read(propVariant);

    if (const auto* val = std::get_if<PropertyType>(&propVariant))
    {
        handler(*val);
    }
    else
    {
        // Again, we can still proceed, in case the service starts working as
        // expected later on.
        std::cerr << "Unable to read property " << propertyName
                  << ". Interface didn't contain expected type\n";
    }

    // Leak the match. We want the match to exist for the lifetime of the
    // program so there is no need to manage the memory.
    new sdbusplus::bus::match_t(
        *dbusConn,
        sdbusplus::bus::match::rules::sender(service) +
            sdbusplus::bus::match::rules::propertiesChanged(object, interface),
        [handler = std::move(handler),
         propertyName](sdbusplus::message::message& reply) {
            // ignore interface name, it has to be correct
            reply.read(std::string());

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

void updateCoreBiosDone(bool newState)
{
    biosDone = newState;
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
    subscribeToProperty("xyz.openbmc_project.Host.Misc.Manager",
                        "/xyz/openbmc_project/misc/platform_state",
                        "xyz.openbmc_project.State.Host.Misc", "CoreBiosDone",
                        updateCoreBiosDone);
}

} // namespace cpu_info
