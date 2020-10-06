#!/bin/sh

# Utility to print all SST data present on D-Bus.
# Simply searches for all objects implementing known interfaces and prints out
# the property values on those interfaces.

BUSCTL='busctl'
XYZ='xyz.openbmc_project'
OBJECT_MAPPER="$XYZ.ObjectMapper /xyz/openbmc_project/object_mapper $XYZ.ObjectMapper"
CPU_INTF="$XYZ.Control.Processor.CurrentOperatingConfig"
CONFIG_INTF="$XYZ.Inventory.Item.Cpu.OperatingConfig"

trim_quotes() {
    trim_obj=${1%\"}
    trim_obj=${trim_obj#\"}
    echo $trim_obj
}

get_sub_tree_paths() {
    resp=$($BUSCTL call $OBJECT_MAPPER GetSubTreePaths sias "$1" 0 "$2" "$3" \
           | cut -d' ' -f3-)
    for obj in $resp
    do
        trim_quotes $obj
    done
}

get_service_from_object() {
    trim_quotes $($BUSCTL call $OBJECT_MAPPER GetObject sas "$1" "$2" "$3" \
                  | cut -d' ' -f3)
}

get_property_names() {
    service=$1
    object=$2
    intf=$3
    $BUSCTL introspect $service $object $intf \
        | awk '/property/ {print substr($1, 2)}'
}

get_property() {
    service=$1
    object=$2
    intf=$3
    prop=$4
    $BUSCTL get-property $service $object $intf $prop
}


cpu_paths=$(get_sub_tree_paths "/" 1 "$CPU_INTF")
for cpu_path in $cpu_paths
do
    service=$(get_service_from_object $cpu_path 1 $CPU_INTF)
    echo "Found SST on $cpu_path on $service"
    for prop in $(get_property_names $service $cpu_path $CPU_INTF)
    do
        echo "  $prop: $(get_property $service $cpu_path $CPU_INTF $prop)"
    done


    profiles=$(get_sub_tree_paths "$cpu_path" 1 "$CONFIG_INTF")
    for profile in $profiles
    do
        echo
        echo "  Found Profile $profile"
        for prop in $(get_property_names $service $profile $CONFIG_INTF)
        do
            echo "    $prop: $(get_property $service $profile $CONFIG_INTF $prop)"
        done
    done
done
