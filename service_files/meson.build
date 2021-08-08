unit_files = [
  ['smbios-mdrv2.service', ''],
  ['xyz.openbmc_project.cpuinfo.service', 'cpuinfo'],
]

foreach u : unit_files
    if u[1] == '' or get_option(u[1])
        configure_file(
            copy: true,
            input: u[0],
            install: true,
            install_dir: systemd_system_unitdir,
            output: u[0],
        )
    endif
endforeach
