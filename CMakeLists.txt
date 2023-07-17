cmake_minimum_required (VERSION 3.2 FATAL_ERROR)
include (ExternalProject)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set (DBUS_OBJECT_NAME "xyz/openbmc_project/Smbios/MDR_V2")
set (DBUS_INTF_NAME "xyz.openbmc_project.Smbios.MDR_V2")

# disable now to wa boost bug chriskohlhoff/asio#533
# set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories (${DBUSINTERFACE_LIBRARIES})

find_package (PkgConfig REQUIRED)

pkg_check_modules (SYSTEMD libsystemd REQUIRED)
include_directories (${SYSTEMD_INCLUDE_DIRS})
link_directories (${SYSTEMD_LIBRARY_DIRS})

# import sdbusplus
pkg_check_modules (SDBUSPLUSPLUS sdbusplus REQUIRED)
include_directories (${SDBUSPLUSPLUS_INCLUDE_DIRS})
link_directories (${SDBUSPLUSPLUS_LIBRARY_DIRS})
find_program (SDBUSPLUSPLUS sdbus++)

# phosphor-dbus-interfaces
pkg_check_modules (DBUSINTERFACE phosphor-dbus-interfaces REQUIRED)
include_directories (${DBUSINTERFACE_INCLUDE_DIRS})
link_directories (${DBUSINTERFACE_LIBRARY_DIRS})

# boost
include_directories (SYSTEM ${CMAKE_BINARY_DIR}/boost-src)
set (CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/boost-src ${CMAKE_PREFIX_PATH})
add_definitions (-DBOOST_ERROR_CODE_HEADER_ONLY)
add_definitions (-DBOOST_SYSTEM_NO_DEPRECATED)
add_definitions (-DBOOST_ALL_NO_LIB)
add_definitions (-DBOOST_NO_RTTI)
add_definitions (-DBOOST_NO_TYPEID)
add_definitions (-DBOOST_ASIO_DISABLE_THREADS)

set (SRC_FILES src/mdrv2.cpp src/mdrv2_main.cpp src/cpu.cpp src/dimm.cpp
     src/system.cpp src/pcieslot.cpp)

include_directories (${CMAKE_CURRENT_BINARY_DIR})

option (GRPC_BLOB "Enable gRPC Interface" ON)

if (GRPC_BLOB)
    pkg_check_modules (GRPC grpc REQUIRED)
    include_directories (${GRPC_INCLUDE_DIRS})
    link_directories (${GRPC_LIBRARY_DIRS})

    pkg_check_modules (GRPCPLUSPLUS grpc++ REQUIRED)
    include_directories (${GRPCPLUSPLUS_INCLUDE_DIRS})
    link_directories (${GRPCPLUSPLUS_LIBRARY_DIRS})
endif ()

add_executable (smbiosmdrv2app ${SRC_FILES})
target_link_libraries (smbiosmdrv2app ${SYSTEMD_LIBRARIES})
target_link_libraries (smbiosmdrv2app ${DBUSINTERFACE_LIBRARIES})
target_link_libraries (smbiosmdrv2app ${SDBUSPLUSPLUS_LIBRARIES})
target_link_libraries (smbiosmdrv2app phosphor_logging)

if (GRPC_BLOB)
    target_link_libraries (smbiosmdrv2app ${GRPC_LIBRARIES})
    target_link_libraries (smbiosmdrv2app ${GRPCPLUSPLUS_LIBRARIES})
endif ()

option (DIMM_DBUS "Expose DIMM D-Bus Interface" ON)

if (DIMM_DBUS)
    target_compile_definitions (smbiosmdrv2app PRIVATE DIMM_DBUS)
endif ()

if (GRPC_BLOB)
    target_compile_definitions (smbiosmdrv2app PRIVATE GRPC_BLOB)
endif ()

if (NOT DEFINED SYSTEMD_SYSTEM_UNIT_DIR)
    pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd systemdsystemunitdir)
endif()

install (TARGETS smbiosmdrv2app DESTINATION bin)

install (FILES  ${PROJECT_SOURCE_DIR}/service_files/smbios-mdrv2.service
    DESTINATION "${SYSTEMD_SYSTEM_UNIT_DIR}")

option (DIMM_ONLY_LOCATOR "Only use the DIMM number, and not the bank" OFF)

if (DIMM_ONLY_LOCATOR)
    target_compile_definitions (smbiosmdrv2app PRIVATE DIMM_ONLY_LOCATOR)
endif ()

option (CPU_INFO "Add Cpuinfo Service" ON)

option (YOCTO "Enable Building in Yocto" OFF)

if (CPU_INFO)
    add_executable (cpuinfoapp src/cpuinfo_main.cpp src/speed_select.cpp
        src/sst_mailbox.cpp
        src/cpuinfo_utils.cpp)
    target_link_libraries (cpuinfoapp ${SYSTEMD_LIBRARIES})
    target_link_libraries (cpuinfoapp ${DBUSINTERFACE_LIBRARIES})
    target_link_libraries (cpuinfoapp ${SDBUSPLUSPLUS_LIBRARIES})
    target_link_libraries (cpuinfoapp phosphor_logging)
    target_link_libraries (cpuinfoapp i2c)
    target_link_libraries (cpuinfoapp peci)

    if (NOT YOCTO)
        add_dependencies (cpuinfoapp libpeci)
        externalproject_add (libpeci PREFIX ${CMAKE_BINARY_DIR}/libpeci
            GIT_REPOSITORY https://github.com/openbmc/libpeci.git
            GIT_TAG 50959a23b48e4f5af9c7110fc78214ea27c6f078
            INSTALL_COMMAND "")

        externalproject_get_property (libpeci SOURCE_DIR)
        include_directories (${SOURCE_DIR})

        externalproject_get_property (libpeci BINARY_DIR)
        add_library (peci SHARED IMPORTED)
        set_target_properties (peci
            PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/libpeci.so)
    endif ()

    install (TARGETS cpuinfoapp
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static)

    install (FILES
        ${PROJECT_SOURCE_DIR}/service_files/xyz.openbmc_project.cpuinfo.service
        DESTINATION "${SYSTEMD_SYSTEM_UNIT_DIR}")
endif()

option (IPMI_BLOB "Add IPMI Blobs" ON)

if (IPMI_BLOB)
    include_directories (${CMAKE_CURRENT_SOURCE_DIR}/src/smbios-ipmi-blobs)
    add_library (smbiosstore SHARED src/smbios-ipmi-blobs/handler.cpp
                 src/smbios-ipmi-blobs/main.cpp)
    set_target_properties (smbiosstore PROPERTIES VERSION "0.0.0")
    set_target_properties (smbiosstore PROPERTIES SOVERSION "0")
    target_link_libraries (smbiosstore sdbusplus)
    target_link_libraries (smbiosstore phosphor_logging)
    install (TARGETS smbiosstore DESTINATION /usr/lib/ipmid-providers)

    if (NOT YOCTO)
        add_dependencies (smbiosstore phosphor-host-ipmid phosphor-ipmi-blobs)

        file (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/prefix)
        externalproject_add (
            phosphor-host-ipmid
            PREFIX ${CMAKE_BINARY_DIR}/phosphor-host-ipmid
            GIT_REPOSITORY https://github.com/openbmc/phosphor-host-ipmid
            SOURCE_DIR ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src
            CONFIGURE_COMMAND cd ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src
                && meson --prefix=${CMAKE_BINARY_DIR}/prefix build-ext
            BUILD_COMMAND cd ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src
                && ninja -C build-ext
            INSTALL_COMMAND cd ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src
                && ninja -C build-ext install
            LOG_DOWNLOAD ON)
        include_directories (${CMAKE_BINARY_DIR}/prefix/include)
        link_directories (${CMAKE_BINARY_DIR}/prefix/lib)

        externalproject_add (
            phosphor-ipmi-blobs PREFIX ${CMAKE_BINARY_DIR}/phosphor-ipmi-blobs
            GIT_REPOSITORY https://github.com/openbmc/phosphor-ipmi-blobs
            SOURCE_DIR ${CMAKE_BINARY_DIR}/phosphor-ipmi-blobs-src
            CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND "")
        include_directories (${CMAKE_BINARY_DIR}/phosphor-ipmi-blobs-src)

        # unit tests
        set (BLOB_TEST_SRC src/smbios-ipmi-blobs/test)

        find_package (GTest REQUIRED)

        enable_testing ()

        add_executable (runBlobBasic ${BLOB_TEST_SRC}/handler_unittest.cpp)
        add_test (NAME test_blobbasic COMMAND runBlobBasic)
        target_link_libraries (runBlobBasic smbiosstore ${GTEST_BOTH_LIBRARIES})

        add_executable (runBlobOpen ${BLOB_TEST_SRC}/handler_open_unittest.cpp)
        add_test (NAME test_blobopen COMMAND runBlobOpen)
        target_link_libraries (runBlobOpen smbiosstore ${GTEST_BOTH_LIBRARIES})

        add_executable (runBlobReadWrite
                        ${BLOB_TEST_SRC}/handler_readwrite_unittest.cpp)
        add_test (NAME test_blobreadwrite COMMAND runBlobReadWrite)
        target_link_libraries (runBlobReadWrite
            smbiosstore -lgmock -lgmock_main ${GTEST_BOTH_LIBRARIES})

        add_executable (runBlobStatClose
                        ${BLOB_TEST_SRC}/handler_statclose_unittest.cpp)
        add_test (NAME test_blobStatClose COMMAND runBlobStatClose)
        target_link_libraries (runBlobStatClose smbiosstore
                               ${GTEST_BOTH_LIBRARIES})
    endif ()
endif ()

if (GRPC_BLOB)
    # https://github.com/protocolbuffers/protobuf/blob/main/docs/cmake_protobuf_generate.md#basic-example
    # https://github.com/faaxm/exmpl-cmake-grpc/blob/master/proto/CMakeLists.txt
    # https://chromium.googlesource.com/external/github.com/grpc/grpc/+/chromium-deps/2016-08-17/examples/cpp/helloworld/CMakeLists.txt
    # https://medium.com/3yourmind/generate-grpc-source-files-using-cmake-ec3acd246b45
    # https://www.f-ax.de/dev/2020/11/08/grpc-plugin-cmake-support.html

    find_package(protobuf CONFIG REQUIRED)
    message(STATUS "Found package protobuf ${protobuf_VERSION}")

    find_package(gRPC CONFIG REQUIRED)
    message(STATUS "Found package gRPC ${gRPC_VERSION}")

    set(GRPC_BLOBS_DIR "${CMAKE_CURRENT_LIST_DIR}/src/smbios-grpc-blobs")
    set(PROTO_IMPORT_DIR "${GRPC_BLOBS_DIR}/proto")
    message(STATUS "Directory import ${PROTO_IMPORT_DIR}")

    set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    message(STATUS "Directory output ${PROTO_BINARY_DIR}")

    add_library(libSmbiosTransfer OBJECT "${PROTO_IMPORT_DIR}/SmbiosTransfer.proto")

    target_link_libraries(libSmbiosTransfer PUBLIC protobuf::libprotobuf)
    target_link_libraries(libSmbiosTransfer PUBLIC gRPC::grpc)
    target_link_libraries(libSmbiosTransfer PUBLIC gRPC::grpc++)
    target_link_libraries(libSmbiosTransfer PUBLIC gRPC::grpc++_reflection)

    target_include_directories(libSmbiosTransfer PUBLIC "${PROTO_BINARY_DIR}")

    find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
    message(STATUS "Found program ${GRPC_CPP_PLUGIN_EXECUTABLE}")

    protobuf_generate(
        TARGET libSmbiosTransfer
        LANGUAGE cpp
        IMPORT_DIRS ${PROTO_IMPORT_DIR}
        PROTOC_OUT_DIR "${PROTO_BINARY_DIR}")

    protobuf_generate(
        TARGET libSmbiosTransfer
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}"
        IMPORT_DIRS ${PROTO_IMPORT_DIR}
        PROTOC_OUT_DIR "${PROTO_BINARY_DIR}")

    target_link_libraries (smbiosmdrv2app libSmbiosTransfer)

    target_include_directories(smbiosmdrv2app PUBLIC "${GRPC_BLOBS_DIR}")
    target_include_directories(smbiosmdrv2app PUBLIC "${PROTO_BINARY_DIR}/src/smbios-grpc-blobs/proto")

    target_sources(smbiosmdrv2app PUBLIC "${GRPC_BLOBS_DIR}/smbios_grpc.cpp")
endif ()
