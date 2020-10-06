#!/usr/bin/env python3

# This tool runs on the host CPU and gathers all SST related configuration from
# the BMC (Redfish) and from the linux driver, and compares them to catch any
# errors or disagreement. Only required arguments are the details to start a
# Redfish session.
#
# This was tested running on a live Arch Linux ISO environment. Any Linux
# installation should work, but best to get the latest tools and kernel driver.
#
# Required dependencies:
# * DMTF's redfish python library. This is available in pip.
# * intel-speed-select tool from the kernel source tree
#   (tools/power/x86/intel-speed-select), and available in the PATH.

import redfish

import argparse
import json
import re
import subprocess
import sys

linux_cpu_map = dict()
success = True

def get_linux_output():
    cmd = "/usr/bin/env intel-speed-select --debug --format json perf-profile info".split()
    process = subprocess.run(cmd, capture_output=True, text=True)
    process.check_returncode()
    result = json.loads(process.stderr)

    global linux_cpu_map
    linux_cpu_map = dict()
    for line in process.stdout.split('\n'):
        match = re.search("logical_cpu:(\d+).*punit_core:(\d+)", line)
        if not match:
            continue
        logical_thread = int(match.group(1))
        physical_core = int(match.group(2))
        linux_cpu_map[logical_thread] = physical_core

    cmd = "/usr/bin/env intel-speed-select --format json perf-profile get-config-current-level".split()
    process = subprocess.run(cmd, capture_output=True, text=True)
    current_level = json.loads(process.stderr)

    for proc, data in current_level.items():
        result[proc].update(data)

    return result


def compare(redfish_val, linux_val, description):
    err = ""
    if redfish_val != linux_val:
        err = "!! MISMATCH !!"
        global status
        success = False
    print(f"{description}: {err}")
    print(f"  Redfish: {redfish_val}")
    print(f"  Linux: {linux_val}")


def get_linux_package(linux_data, redfish_id):
    match = re.match("cpu(\d+)", redfish_id)
    if not match:
        raise RuntimeError(f"Redfish CPU name is unexpected: {redfish_id}")
    num = match.group(1)
    matching_keys = []
    for key in linux_data.keys():
        if re.match(f"^package-{num}:.*", key):
            matching_keys.append(key)
    if len(matching_keys) != 1:
        raise RuntimeError(f"Unexpected number of matching linux objects for {redfish_id}")
    return linux_data[matching_keys[0]]


def compare_config(redfish_config, linux_config):
    print(f"--Checking {redfish_config['Id']}--")
    compare(redfish_config["BaseSpeedMHz"], int(linux_config["base-frequency(MHz)"]), "Base Speed")

    actual_hp_p1 = actual_lp_p1 = 0
    actual_hp_cores = set()
    for bf in redfish_config["BaseSpeedPrioritySettings"]:
        if not actual_hp_p1 or bf["BaseSpeedMHz"] > actual_hp_p1:
            actual_hp_p1 = bf["BaseSpeedMHz"]
            actual_hp_cores = set(bf["CoreIDs"])
        if not actual_lp_p1 or bf["BaseSpeedMHz"] < actual_lp_p1:
            actual_lp_p1 = bf["BaseSpeedMHz"]

    exp_hp_p1 = exp_lp_p1 = 0
    exp_hp_cores = set()
    if "speed-select-base-freq-properties" in linux_config:
        exp_bf_props = linux_config["speed-select-base-freq-properties"]
        exp_hp_p1 = int(exp_bf_props["high-priority-base-frequency(MHz)"])
        exp_hp_cores = set(map(lambda x: linux_cpu_map[x],
                              map(int, exp_bf_props["high-priority-cpu-list"].split(","))))
        exp_lp_p1 = int(exp_bf_props["low-priority-base-frequency(MHz)"])

    compare(actual_hp_p1, exp_hp_p1, "SST-BF High Priority P1 Freq")
    compare(actual_hp_cores, exp_hp_cores, "SST-BF High Priority Core List")
    compare(actual_lp_p1, exp_lp_p1, "SST-BF Low Priority P1 Freq")


    compare(redfish_config["MaxJunctionTemperatureCelsius"],
            int(linux_config["tjunction-max(C)"]),
            "Junction Temperature")
    compare(redfish_config["MaxSpeedMHz"],
            int(linux_config["turbo-ratio-limits-sse"]["bucket-0"]["max-turbo-frequency(MHz)"]),
            "SSE Max Turbo Speed")
    compare(redfish_config["TDPWatts"],
            int(linux_config["thermal-design-power(W)"]),
            "TDP")
    compare(redfish_config["TotalAvailableCoreCount"],
            int(linux_config["enable-cpu-count"])//2,
            "Enabled Core Count")

    actual_turbo = [(x["ActiveCoreCount"], x["MaxSpeedMHz"]) for x in redfish_config["TurboProfile"]]
    linux_turbo = linux_config["turbo-ratio-limits-sse"]
    exp_turbo = []
    for bucket_key in sorted(linux_turbo.keys()):
        bucket = linux_turbo[bucket_key]
        exp_turbo.append((int(bucket["core-count"]), int(bucket["max-turbo-frequency(MHz)"])))
    compare(actual_turbo, exp_turbo, "SSE Turbo Profile")


def get_level_from_config_id(config_id):
    match = re.match("config(\d+)", config_id)
    if not match:
        raise RuntimeError(f"Invalid config name {config_id}")
    return match.group(1)


def main():
    parser = argparse.ArgumentParser(description="Compare Redfish SST properties against Linux tools")
    parser.add_argument("hostname")
    parser.add_argument("--username", "-u", default="root")
    parser.add_argument("--password", "-p", default="0penBmc")
    args = parser.parse_args()

    linux_data = get_linux_output()

    bmc = redfish.redfish_client(base_url=f"https://{args.hostname}",
            username=args.username, password=args.password)
    bmc.login(auth="session")

    # Load the ProcessorCollection
    resp = json.loads(bmc.get("/redfish/v1/Systems/system/Processors").text)
    for proc_member in resp["Members"]:
        proc_resp = json.loads(bmc.get(proc_member["@odata.id"]).text)
        proc_id = proc_resp["Id"]
        print()
        print(f"----Checking Processor {proc_id}----")

        if proc_resp["Status"]["State"] == "Absent":
            print("Not populated")
            continue

        # Get subset of intel-speed-select data which applies to this CPU
        pkg_data = get_linux_package(linux_data, proc_id)

        # Check currently applied config
        applied_config = proc_resp["AppliedOperatingConfig"]["@odata.id"].split('/')[-1]
        current_level = get_level_from_config_id(applied_config)
        compare(current_level, pkg_data["get-config-current_level"], "Applied Config")

        exp_cur_level_data = pkg_data[f"perf-profile-level-{current_level}"]

        # Check whether SST-BF is enabled
        bf_enabled = proc_resp["BaseSpeedPriorityState"].lower()
        exp_bf_enabled = exp_cur_level_data["speed-select-base-freq"]
        if exp_bf_enabled == "unsupported":
            exp_bf_enabled = "disabled"
        compare(bf_enabled, exp_bf_enabled, "SST-BF Enabled?")

        # Check high speed core list
        hscores = set(proc_resp["HighSpeedCoreIDs"])
        exp_hscores = set()
        if "speed-select-base-freq-properties" in exp_cur_level_data:
            exp_hscores = exp_cur_level_data["speed-select-base-freq-properties"]["high-priority-cpu-list"]
            exp_hscores = set([linux_cpu_map[int(x)] for x in exp_hscores.split(",")])
        compare(hscores, exp_hscores, "High Speed Core List")

        # Load the OperatingConfigCollection
        resp = json.loads(bmc.get(proc_resp["OperatingConfigs"]["@odata.id"]).text)

        # Check number of available configs
        profile_keys = list(filter(lambda x: x.startswith("perf-profile-level"), pkg_data.keys()))
        compare(resp["Members@odata.count"], int(len(profile_keys)), "Number of profiles")

        for config_member in resp["Members"]:
            # Load each OperatingConfig and compare all its contents
            config_resp = json.loads(bmc.get(config_member["@odata.id"]).text)
            level = get_level_from_config_id(config_resp["Id"])
            exp_level_data = pkg_data[f"perf-profile-level-{level}"]
            compare_config(config_resp, exp_level_data)

    print()
    if success:
        print("Everything matched! :)")
        return 0
    else:
        print("There were mismatches, please check output :(")
        return 1

if __name__ == "__main__":
    sys.exit(main())
