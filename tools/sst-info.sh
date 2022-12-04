#!/bin/bash

# Utility to print all SST data present on D-Bus.
# Simply searches for all objects implementing known interfaces and prints out
# the property values on those interfaces.

set -e

BUSCTL='busctl'
XYZ='xyz.openbmc_project'
OBJECT_MAPPER="$XYZ.ObjectMapper /xyz/openbmc_project/object_mapper $XYZ.ObjectMapper"
CPU_INTF="$XYZ.Control.Processor.CurrentOperatingConfig"
CONFIG_INTF="$XYZ.Inventory.Item.Cpu.OperatingConfig"

function trim_quotes() {
    trim_obj=${1%\"}
    trim_obj=${trim_obj#\"}
    echo "$trim_obj"
}

function get_sub_tree_paths() {
    resp=$($BUSCTL call "$OBJECT_MAPPER" GetSubTreePaths sias "$1" 0 "$2" "$3" \
        | cut -d' ' -f3-)
    for obj in $resp
    do
        trim_quotes "$obj"
    done
}

function get_service_from_object() {
    trim_quotes "$($BUSCTL call "$OBJECT_MAPPER" GetObject sas "$1" "$2" "$3" \
        | cut -d' ' -f3)"
}

function get_property_names() {
    service=$1
    object=$2
    intf=$3
    $BUSCTL introspect "$service" "$object" "$intf" \
        | awk '/property/ {print substr($1, 2)}'
}

function get_property() {
    service=$1
    object=$2
    intf=$3
    prop=$4
    $BUSCTL get-property "$service" "$object" "$intf" "$prop"
}

function set_property() {
    service=$1
    object=$2
    intf=$3
    prop=$4
    signature=$5
    value=$6
    $BUSCTL set-property "$service" "$object" "$intf" "$prop" \
        "$signature" "$value"
}

function show() {
    cpu_paths=$(get_sub_tree_paths "/" 1 "$CPU_INTF")
    for cpu_path in $cpu_paths
    do
        service=$(get_service_from_object "$cpu_path" 1 "$CPU_INTF")
        echo "Found SST on $cpu_path on $service"
        for prop in $(get_property_names "$service" "$cpu_path" "$CPU_INTF")
        do
            echo "  $prop: $(get_property "$service" "$cpu_path" "$CPU_INTF" "$prop")"
        done


        profiles=$(get_sub_tree_paths "$cpu_path" 1 "$CONFIG_INTF")
        for profile in $profiles
        do
            echo
            echo "  Found Profile $profile"
            for prop in $(get_property_names "$service" "$profile" "$CONFIG_INTF")
            do
                echo "    $prop: $(get_property "$service" "$profile" "$CONFIG_INTF" "$prop")"
            done
        done
    done
}

function set_cpu_prop() {
    cpu_basename=$1
    prop=$2
    signature=$3
    value=$4


    cpu_paths=$(get_sub_tree_paths "/" 1 "$CPU_INTF")
    for cpu_path in $cpu_paths
    do
        if [[ $cpu_path != *$cpu_basename ]]
        then
            continue
        fi

        if [[ "$prop" == "AppliedConfig" ]]
        then
            value=$cpu_path/$value
        fi

        service=$(get_service_from_object "$cpu_path" 1 "$CPU_INTF")
        set_property "$service" "$cpu_path" "$CPU_INTF" "$prop" "$signature" "$value"
        return 0
    done

    echo "$cpu_basename not found"
    return 1
}

if [[ ${DEBUG:=0} -eq 1 ]]
then
    set -x
fi

action=${1:-show}

case "$action" in
    show) show ;;
    set-config) set_cpu_prop "$2" AppliedConfig o "$3" ;;
    set-bf) set_cpu_prop "$2" BaseSpeedPriorityEnabled b "$3" ;;
    *)
        echo "Usage:"
        echo "$0 (show|set-config|set-bf) [ARGS...]"
        echo ""
        echo "show (Default action) - show info"
        echo "set-config cpuN configM - Set applied operating config for cpuN to configM"
        echo "set-bf cpuN val - Set SST-BF enablement for cpuN to val (boolean)"
        ;;
esac
