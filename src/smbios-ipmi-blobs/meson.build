ipmi_blob_dep = dependency('phosphor-ipmi-blobs')
libipmid_dep = dependency('libipmid')

smbiosstoreblob_pre = declare_dependency(
  include_directories: [root_inc, include_directories('.')],
  dependencies: [
    ipmi_blob_dep,
    phosphor_logging_dep,
    libipmid_dep,
  ])

smbiosstoreblob_lib = static_library(
  'smbiosstore',
  'handler.cpp',
  implicit_include_directories: false,
  dependencies: smbiosstoreblob_pre)

smbiosstoreblob_dep = declare_dependency(
  link_with: smbiosstoreblob_lib,
  dependencies: smbiosstoreblob_pre)

shared_module(
  'smbiosstore',
  'main.cpp',
  implicit_include_directories: false,
  dependencies: smbiosstoreblob_dep,
  install: true,
  install_dir: get_option('libdir') / 'blob-ipmid')

if not get_option('tests').disabled()
  subdir('test')
endif
