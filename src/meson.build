cpp_args_smbios = boost_args
if get_option('dimm-dbus')
  cpp_args_smbios += ['-DDIMM_DBUS']
endif

if get_option('dimm-only-locator')
  cpp_args_smbios += ['-DDIMM_ONLY_LOCATOR']
endif

executable(
  'smbiosmdrv2app',
  'mdrv2.cpp',
  'mdrv2_main.cpp',
  'cpu.cpp',
  'dimm.cpp',
  'system.cpp',
  'pcieslot.cpp',
  cpp_args: cpp_args_smbios,
  dependencies: [
    boost_dep,
    sdbusplus_dep,
    phosphor_logging_dep,
    phosphor_dbus_interfaces_dep,
  ],
  implicit_include_directories: false,
  include_directories: root_inc,
  install: true,
)

if get_option('cpuinfo')
  cpp = meson.get_compiler('cpp')
  # i2c-tools provides no pkgconfig so we need to find it manually
  i2c_dep = cpp.find_library('i2c')

  peci_dep = cpp.find_library('libpeci', required: false)
  if not peci_dep.found()
    cmake = import('cmake')
    peci_proj = cmake.subproject('libpeci')
    peci_dep = peci_proj.dependency('peci')
  endif

  executable(
    'cpuinfoapp',
    'cpuinfo_main.cpp',
    'speed_select.cpp',
    'sst_mailbox.cpp',
    'cpuinfo_utils.cpp',
    cpp_args: boost_args,
    dependencies: [
      boost_dep,
      sdbusplus_dep,
      phosphor_logging_dep,
      phosphor_dbus_interfaces_dep,
      i2c_dep,
      peci_dep,
    ],
    implicit_include_directories: false,
    include_directories: root_inc,
    install: true,
  )
endif

if get_option('smbios-ipmi-blob')
  subdir('smbios-ipmi-blobs')
endif
