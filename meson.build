project(
  'smbios-mdr',
  'cpp',
  version: '0.1',
  meson_version: '>=0.55.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'werror=true',
  ])

boost_args = [
    '-DBOOST_SYSTEM_NO_DEPRECATED',
    '-DBOOST_ERROR_CODE_HEADER_ONLY',
    '-DBOOST_NO_RTTI',
    '-DBOOST_NO_TYPEID',
    '-DBOOST_ALL_NO_LIB',
    '-DBOOST_ASIO_DISABLE_THREADS',
]

root_inc = include_directories('include')

boost_dep = dependency('boost', required: false)
if not boost_dep.found()
     subproject('boost', required: false)
     boost_dep = declare_dependency(
         include_directories: 'subprojects/boost_1_71_0',
     )
endif

systemd_dep = dependency('systemd')
systemd_system_unitdir = systemd_dep.get_pkgconfig_variable(
    'systemdsystemunitdir',
    define_variable: ['prefix', get_option('prefix')])

sdbusplus_dep = dependency('sdbusplus',
  fallback: ['sdbusplus', 'sdbusplus_dep'])
phosphor_dbus_interfaces_dep= dependency(
  'phosphor-dbus-interfaces',
  fallback: ['phosphor-dbus-interfaces', 'phosphor_dbus_interfaces_dep'])
phosphor_logging_dep = dependency('phosphor-logging',
  fallback: ['phosphor-logging', 'phosphor_logging_deps'])

subdir('src')
subdir('service_files')
