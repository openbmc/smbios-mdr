project(
  'smbios-mdr',
  'cpp',
  version: '0.1',
  meson_version: '>=0.55.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'cpp_rtti=false'
  ])

boost_args = [
  '-DBOOST_NO_RTTI',
  '-DBOOST_NO_TYPEID',
  '-DBOOST_ALL_NO_LIB',
  '-DBOOST_ASIO_DISABLE_THREADS',
]

root_inc = include_directories('include')

boost_dep = dependency('boost', required: false)

sdbusplus_dep = dependency('sdbusplus')
phosphor_dbus_interfaces_dep= dependency('phosphor-dbus-interfaces')
phosphor_logging_dep = dependency('phosphor-logging')

subdir('src')
subdir('service_files')
