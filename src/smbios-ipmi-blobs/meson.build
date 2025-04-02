ipmi_blob_dep = dependency('phosphor-ipmi-blobs')
libipmid_dep = dependency('libipmid').partial_dependency(includes: true)

smbiosstore_common_deps = [ipmi_blob_dep, libipmid_dep, sdbusplus_dep]

shared_module(
    'smbiosstore',
    'main.cpp',
    'handler.cpp',
    dependencies: [
        smbiosstore_common_deps,
        gtest,
        gmock,
        phosphor_logging_dep,
    ],
    include_directories: root_inc,
    install: true,
    install_dir: get_option('libdir') / 'blob-ipmid',
)

if get_option('tests').allowed()
    subdir('test')
endif
