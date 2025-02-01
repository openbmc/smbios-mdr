# Map of unit file to meson option which controls whether file gets installed
unit_files = [
    ['smbios-mdrv2.service', ''],
    ['xyz.openbmc_project.cpuinfo.service', 'cpuinfo'],
]

systemd_dep = dependency('systemd')
systemd_system_unitdir = systemd_dep.get_variable(
    'systemd_system_unit_dir',
    pkgconfig_define: ['rootprefix', get_option('prefix')],
)

foreach u : unit_files
    if u[1] == '' or get_option(u[1]).allowed()
        install_data(u[0], install_dir: systemd_system_unitdir)
    endif
endforeach
