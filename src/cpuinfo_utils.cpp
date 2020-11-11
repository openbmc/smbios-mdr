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
static PowerState powerState = PowerState::Off;
static bool biosDone = false;

static std::shared_ptr<sdbusplus::asio::connection> dbusConn;

static void updateHostState()
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
    DEBUG_PRINT << "new host state: " << static_cast<int>(hostState) << "\n";
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
 * @param[in]   propertyName    D-Bus property name.
 * @param[in]   handler     Callable to be called immediately and upon any
 *                          changes in the property value.
 * @param[out]  propertiesChangedMatch  Optional pointer to receive a D-Bus
 *                                      match object, if you need to manage its
 *                                      lifetime.
 * @param[out]  interfacesAddedMatch    Optional pointer to receive a D-Bus
 *                                      match object, if you need to manage its
 *                                      lifetime.
 */
template <typename... CustomVariantTypes, typename Handler>
static void subscribeToProperty(
    const char* service, const char* object, const char* interface,
    const char* propertyName, Handler&& handler,
    sdbusplus::bus::match_t** propertiesChangedMatch = nullptr,
    sdbusplus::bus::match_t** interfacesAddedMatch = nullptr)
{
    // Type of first parameter to Handler, with const/& removed
    using PropertyType = std::remove_const_t<std::remove_reference_t<
        std::tuple_element_t<0, boost::callable_traits::args_t<Handler>>>>;
    // Base data types which we can handle by default
    using InterfaceVariant = typename sdbusplus::utility::dedup_variant<
        PropertyType, CustomVariantTypes..., bool, uint8_t, uint16_t, int16_t,
        uint32_t, int32_t, uint64_t, int64_t, size_t, ssize_t, double,
        std::string, sdbusplus::message::object_path>;

    auto method = dbusConn->new_method_call(
        service, object, "org.freedesktop.DBus.Properties", "Get");
    method.append(interface, propertyName);

    InterfaceVariant propVariant;
    try
    {
        sdbusplus::message::message reply;
        reply = dbusConn->call(method);
        reply.read(propVariant);
    }
    catch (const sdbusplus::exception_t& err)
    {
        // The service may be down now but we can still continue to add the
        // match since the service may work in the future.
        DEBUG_PRINT << "Unable to read property " << propertyName << ". "
                    << err.what() << "\n";
    }

    if (const auto* val = std::get_if<PropertyType>(&propVariant))
    {
        handler(*val);
    }
    else
    {
        // Again, we can still proceed, in case the service starts working as
        // expected later on.
        DEBUG_PRINT << "Unable to read property " << propertyName
                    << ". Interface didn't contain expected type\n";
    }

    using ChangedPropertiesType =
        std::vector<std::pair<std::string, InterfaceVariant>>;

    // This is a bit tricky (maybe there's a better way?). But we have 1 user
    // Handler that we want to call from two different match callbacks. To be
    // totally generic, if user Handler is a mutable lambda, then we don't want
    // to copy it, and we can't move it into both match callbacks. So we make a
    // common property handler lambda here which owns the user Handler, and is
    // dynamically allocated and shared between the two match callbacks.
    auto* commonPropHandler =
        new auto([propertyName = std::string(propertyName),
                  handler = std::forward<Handler>(handler)](
                     const ChangedPropertiesType& changedProps) {
            for (const auto& [changedProp, newValue] : changedProps)
            {
                if (changedProp == propertyName &&
                    std::holds_alternative<PropertyType>(newValue))
                {
                    const auto& actualVal = std::get<PropertyType>(newValue);
                    DEBUG_PRINT << "New value for " << propertyName << ": "
                                << actualVal << "\n";
                    handler(actualVal);
                }
            }
        });
    // Turn the dynamically allocated common lambda into a shared_ptr (little
    // tricky since the lambda type is "unspeakable").
    auto sharedPropHandler =
        std::shared_ptr<std::remove_reference_t<decltype(*commonPropHandler)>>(
            commonPropHandler);

    // Set up a match for PropertiesChanged signal
    auto propMatch = new sdbusplus::bus::match_t(
        *dbusConn,
        sdbusplus::bus::match::rules::sender(service) +
            sdbusplus::bus::match::rules::propertiesChanged(object, interface),
        [sharedPropHandler](sdbusplus::message::message& reply) {
            // ignore interface name, it has to be correct
            reply.read(std::string());

            ChangedPropertiesType changedProps;
            reply.read(changedProps);

            DEBUG_PRINT << "PropertiesChanged handled\n";
            (*sharedPropHandler)(changedProps);
        });

    // Set up a match for the InterfacesAdded signal from the service's
    // ObjectManager. This is useful in the case where the object is not added
    // yet, and when it's added they choose to not emit PropertiesChanged. So in
    // order to see the initial value when it comes, we need to watch this too.
    auto intfMatch = new sdbusplus::bus::match_t(
        *dbusConn,
        sdbusplus::bus::match::rules::sender(service) +
            sdbusplus::bus::match::rules::interfacesAdded(),
        [object = std::string(object), interface = std::string(interface),
         sharedPropHandler](sdbusplus::message::message& reply) {
            sdbusplus::message::object_path changedObject;
            reply.read(changedObject);
            if (changedObject != object)
            {
                return;
            }

            std::vector<std::pair<std::string, ChangedPropertiesType>>
                changedInterfaces;
            reply.read(changedInterfaces);

            for (const auto& [changedInterface, changedProps] :
                 changedInterfaces)
            {
                if (changedInterface != interface)
                {
                    continue;
                }

                DEBUG_PRINT << "InterfacesAdded handled\n";
                (*sharedPropHandler)(changedProps);
            }
        });

    if (propertiesChangedMatch != nullptr)
    {
        *propertiesChangedMatch = propMatch;
    }

    if (interfacesAddedMatch != nullptr)
    {
        *interfacesAddedMatch = intfMatch;
    }
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

void hostStateSetup(const std::shared_ptr<sdbusplus::asio::connection>& conn)
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }

    dbusConn = conn;

    // Leak the returned match objects. We want them to run forever.
    subscribeToProperty(
        "xyz.openbmc_project.State.Host", "/xyz/openbmc_project/state/host0",
        State::server::Host::interface, "CurrentHostState", updatePowerState);
    subscribeToProperty("xyz.openbmc_project.Host.Misc.Manager",
                        "/xyz/openbmc_project/misc/platform_state",
                        "xyz.openbmc_project.State.Host.Misc", "CoreBiosDone",
                        updateCoreBiosDone);

    initialized = true;
}

} // namespace cpu_info
