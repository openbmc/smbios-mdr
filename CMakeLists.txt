cmake_minimum_required (VERSION 3.2 FATAL_ERROR)
include (ExternalProject)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set (DBUS_OBJECT_NAME "xyz/openbmc_project/Smbios/MDR_V2")
set (DBUS_INTF_NAME "xyz.openbmc_project.Smbios.MDR_V2")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

include_directories (
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phosphor-dbus-interfaces
)
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories (${DBUSINTERFACE_LIBRARIES})

find_package (PkgConfig REQUIRED)
pkg_check_modules (SYSTEMD libsystemd REQUIRED)
include_directories (${SYSTEMD_INCLUDE_DIRS})
link_directories (${SYSTEMD_LIBRARY_DIRS})

# import sdbusplus
find_package (PkgConfig REQUIRED)
pkg_check_modules (SDBUSPLUSPLUS sdbusplus REQUIRED)
include_directories (${SDBUSPLUSPLUS_INCLUDE_DIRS})
link_directories (${SDBUSPLUSPLUS_LIBRARY_DIRS})

# phosphor-dbus-interfaces
find_package (PkgConfig REQUIRED)
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
     src/system.cpp)

include_directories (${CMAKE_CURRENT_BINARY_DIR})

find_package (PkgConfig REQUIRED)
pkg_check_modules (SDBUSPLUSPLUS sdbusplus REQUIRED)
include_directories (${SDBUSPLUSPLUS_INCLUDE_DIRS})
link_directories (${SDBUSPLUSPLUS_LIBRARY_DIRS})
find_program (SDBUSPLUSPLUS sdbus++)

add_executable (smbiosmdrv2app ${SRC_FILES})
target_link_libraries (smbiosmdrv2app ${SYSTEMD_LIBRARIES})
target_link_libraries (smbiosmdrv2app ${DBUSINTERFACE_LIBRARIES})
target_link_libraries (smbiosmdrv2app ${SDBUSPLUSPLUS_LIBRARIES})
target_link_libraries (smbiosmdrv2app phosphor_logging)

add_executable (cpuinfoapp src/cpuinfo_main.cpp)
target_link_libraries (cpuinfoapp ${SYSTEMD_LIBRARIES})
target_link_libraries (cpuinfoapp ${DBUSINTERFACE_LIBRARIES})
target_link_libraries (cpuinfoapp ${SDBUSPLUSPLUS_LIBRARIES})
target_link_libraries (cpuinfoapp phosphor_logging)
target_link_libraries (cpuinfoapp i2c)
target_link_libraries (cpuinfoapp peci)

option (DIMM_DBUS "Expose DIMM D-Bus Interface" ON)

if (DIMM_DBUS)
    target_compile_definitions (smbiosmdrv2app PRIVATE DIMM_DBUS)
endif ()

install (TARGETS smbiosmdrv2app DESTINATION sbin)

install (TARGETS cpuinfoapp
         RUNTIME DESTINATION bin
         LIBRARY DESTINATION lib
         ARCHIVE DESTINATION lib/static)

set (SERVICE_FILES
     ${PROJECT_SOURCE_DIR}/service_files/xyz.openbmc_project.cpuinfo.service)
install (FILES ${SERVICE_FILES} DESTINATION /lib/systemd/system/)
