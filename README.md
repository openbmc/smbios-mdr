# SMBIOS Parser

The main application in this repo is `smbiosmdrv2app`, capable of parsing a
binary [SMBIOS][1] table and publishing the system information on D-Bus, to be
consumed by other OpenBMC applications.

The SMBIOS table is usually sent to the BMC by the host firmware (BIOS). The
system designer can theoretically choose any transport and mechanism for sending
the SMBIOS data, but there are at least two implementation today:

## MDRv2

The primary API is a set of Intel OEM IPMI commands called Managed Data Region
version 2 (MDRv2), which provides a means for host firmware to send data through
the VGA shared memory region. MDRv2 has a concept of multiple agents, each
maintaining a "directory" containing directory entries (aka data sets). The host
can query for the existence and version of directories to determine when it
needs to send an updated SMBIOS table.

`intel-ipmi-oem` implements the [IPMI command handlers][2], routing commands and
data to the correct agent (e.g. `smbios-mdr`). The [D-Bus interface][3] between
the IPMI handler and `smbios-mdr` is largely a mirror of IPMI commands.

## phosphor-ipmi-blobs

[`phosphor-ipmi-blobs`][4] is an alternative implementation of a generic IPMI
blob transfer API. Compared to MDRv2, it is simpler and easier to use, but also
transfers the data in-band with the IPMI commands and therefore slower than
using a shared memory region (which may or may not be a concern).

`phosphor-ipmi-blobs` provides a blob manager shared library for `ipmid` which
implements the IPMI commands. In turn, it loads blob handler libraries that each
implement support for specific blobs. Here in `smbios-mdr` we provide such a
blob handler for the `/smbios` blob. It works by writing the data into
`/var/lib/smbios/smbios2` (the local persistent cache for the SMBIOS table) and
calling the `AgentSynchronizeData` D-Bus method to trigger `smbios-mdr` to
reload and parse the table from that file.

# Intel CPU Info

`cpuinfoapp` is an Intel-specific application that uses I2C and PECI to gather
more details about Xeon CPUs that aren't included in the SMBIOS table for some
reason. It also implements discovery and control for Intel Speed Select
Technology (SST).

[1]: https://www.dmtf.org/standards/smbios
[2]:
  https://github.com/openbmc/intel-ipmi-oem/blob/84c203d2b74680e9dd60d1c48a2f6ca8f58462bf/src/smbiosmdrv2handler.cpp#L1272
[3]:
  https://github.com/openbmc/phosphor-dbus-interfaces/blob/d1484a1499bc241316853934e6e8b735166deee2/yaml/xyz/openbmc_project/Smbios/MDR_V2.interface.yaml
[4]: https://github.com/openbmc/phosphor-ipmi-blobs
