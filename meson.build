project(
    'smbios-mdr',
    'cpp',
    version: '0.1',
    meson_version: '>=1.1.1',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=true',
        'cpp_rtti=false',
    ],
)

boost_args = ['-DBOOST_ALL_NO_LIB', '-DBOOST_ASIO_DISABLE_THREADS']

root_inc = include_directories('include')

boost_dep = dependency('boost')

sdbusplus_dep = dependency('sdbusplus')
phosphor_dbus_interfaces_dep = dependency('phosphor-dbus-interfaces')
phosphor_logging_dep = dependency('phosphor-logging')

subdir('src')
subdir('service_files')
