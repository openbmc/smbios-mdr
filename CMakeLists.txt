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

add_executable (smbiosmdrv2app ${SRC_FILES})
target_link_libraries (smbiosmdrv2app ${SYSTEMD_LIBRARIES})
target_link_libraries (smbiosmdrv2app ${DBUSINTERFACE_LIBRARIES})
target_link_libraries (smbiosmdrv2app ${SDBUSPLUSPLUS_LIBRARIES})
target_link_libraries (smbiosmdrv2app phosphor_logging)

option (DIMM_DBUS "Expose DIMM D-Bus Interface" ON)

if (DIMM_DBUS)
    target_compile_definitions (smbiosmdrv2app PRIVATE DIMM_DBUS)
endif ()

install (TARGETS smbiosmdrv2app DESTINATION bin)

install (FILES  ${PROJECT_SOURCE_DIR}/service_files/smbios-mdrv2.service
    DESTINATION /lib/systemd/system/)

option (CPU_INFO "Add Cpuinfo Service" ON)

option (YOCTO "Enable Building in Yocto" OFF)

if (CPU_INFO)
    add_executable (cpuinfoapp src/cpuinfo_main.cpp src/speed_select.cpp
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
            GIT_TAG a2ceec2aa139277cebb62e1eda449ef60fa4c962
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
        DESTINATION /lib/systemd/system/)
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
            phosphor-host-ipmid PREFIX ${CMAKE_BINARY_DIR}/phosphor-host-ipmid
            GIT_REPOSITORY https://github.com/openbmc/phosphor-host-ipmid
            SOURCE_DIR ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src
            CONFIGURE_COMMAND cd ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src &&
            ./bootstrap.sh && ./configure --prefix=${CMAKE_BINARY_DIR}/prefix
            BUILD_COMMAND cd ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src && make
            INSTALL_COMMAND cd ${CMAKE_BINARY_DIR}/phosphor-host-ipmid-src &&
            make install LOG_DOWNLOAD ON)
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
