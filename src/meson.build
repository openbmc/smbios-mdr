cpp_args_smbios = boost_args
if get_option('dimm-dbus')
  cpp_args_smbios = cpp_args_smbios + ['-DDIMM_DBUS']
endif

executable(
  'smbiosmdrv2app',
  'mdrv2.cpp',
  'mdrv2_main.cpp',
  'cpu.cpp',
  'dimm.cpp',
  'system.cpp',
  cpp_args: cpp_args_smbios,
  dependencies: [
    boost_dep,
    sdbusplus_dep,
    systemd_dep,
    phosphor_logging_dep,
    phosphor_dbus_interfaces_dep,
  ],
  implicit_include_directories: false,
  include_directories: root_inc,
  install_dir: get_option('bindir'),
  install: true,
)

if get_option('cpuinfo')
  i2c_dep = meson.get_compiler('cpp').find_library('i2c')
  peci_dep = dependency('peci', required:false)
  if not peci_dep.found()
    cmake = import('cmake')
    opt_var = cmake.subproject_options()
    opt_var.append_compile_args('c', '-Wno-pedantic')
    peci_proj = cmake.subproject('libpeci', options: opt_var)
    peci_dep = peci_proj.dependency('peci')
  endif

  executable(
    'cpuinfoapp',
    'cpuinfo_main.cpp',
    'speed_select.cpp',
    'cpuinfo_utils.cpp',
    cpp_args: boost_args,
    dependencies: [
      boost_dep,
      sdbusplus_dep,
      systemd_dep,
      phosphor_logging_dep,
      phosphor_dbus_interfaces_dep,
      i2c_dep,
      peci_dep,
    ],
    implicit_include_directories: false,
    include_directories: root_inc,
    install_dir: get_option('bindir'),
    install: true,
  )
endif

if get_option('smbios-ipmi-blob')
  subdir('smbios-ipmi-blobs')
endif
