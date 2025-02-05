/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#pragma once
#include "smbios_mdrv2.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Connector/Slot/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/AssetTag/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Revision/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cpu/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

namespace phosphor
{

namespace smbios
{

using rev =
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::Revision;
using asset =
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::Asset;
using location =
    sdbusplus::server::xyz::openbmc_project::inventory::decorator::LocationCode;
using connector =
    sdbusplus::server::xyz::openbmc_project::inventory::connector::Slot;
using processor = sdbusplus::server::xyz::openbmc_project::inventory::item::Cpu;
using Item = sdbusplus::server::xyz::openbmc_project::inventory::Item;
using association =
    sdbusplus::server::xyz::openbmc_project::association::Definitions;
using operationalStatus = sdbusplus::xyz::openbmc_project::State::Decorator::
    server::OperationalStatus;
using assetTagType =
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::AssetTag;

// This table is up to date as of SMBIOS spec DSP0134 3.7.0
static const std::map<uint8_t, const char*> familyTable = {
    {0x01, "Other"},
    {0x02, "Unknown"},
    {0x03, "8086"},
    {0x04, "80286"},
    {0x05, "Intel 386 processor"},
    {0x06, "Intel 486 processor"},
    {0x07, "8087"},
    {0x08, "80287"},
    {0x09, "80387"},
    {0x0a, "80487"},
    {0x0b, "Intel Pentium processor"},
    {0x0c, "Pentium Pro processor"},
    {0x0d, "Pentium II processor"},
    {0x0e, "Pentium processor with MMX technology"},
    {0x0f, "Intel Celeron processor"},
    {0x10, "Pentium II Xeon processor"},
    {0x11, "Pentium III processor"},
    {0x12, "M1 Family"},
    {0x13, "M2 Family"},
    {0x14, "Intel Celeron M processor"},
    {0x15, "Intel Pentium 4 HT processor"},
    {0x16, "Intel Processor"},
    {0x18, "AMD Duron Processor Family"},
    {0x19, "K5 Family"},
    {0x1a, "K6 Family"},
    {0x1b, "K6-2"},
    {0x1c, "K6-3"},
    {0x1d, "AMD Athlon Processor Family"},
    {0x1e, "AMD29000 Family"},
    {0x1f, "K6-2+"},
    {0x20, "Power PC Family"},
    {0x21, "Power PC 601"},
    {0x22, "Power PC 603"},
    {0x23, "Power PC 603+"},
    {0x24, "Power PC 604"},
    {0x25, "Power PC 620"},
    {0x26, "Power PC x704"},
    {0x27, "Power PC 750"},
    {0x28, "Intel Core Duo processor"},
    {0x29, "Intel Core Duo mobile processor"},
    {0x2a, "Intel Core Solo mobile processor"},
    {0x2b, "Intel Atom processor"},
    {0x2c, "Intel Core M processor"},
    {0x2d, "Intel Core m3 processor"},
    {0x2e, "Intel Core m5 processor"},
    {0x2f, "Intel Core m7 processor"},
    {0x30, "Alpha Family"},
    {0x31, "Alpha 21064"},
    {0x32, "Alpha 21066"},
    {0x33, "Alpha 21164"},
    {0x34, "Alpha 21164PC"},
    {0x35, "Alpha 21164a"},
    {0x36, "Alpha 21264"},
    {0x37, "Alpha 21364"},
    {0x38, "AMD Turion II Ultra Dual-Core Mobile M Processor Family"},
    {0x39, "AMD Turion II Dual-Core Mobile M Processor Family"},
    {0x3a, "AMD Athlon II Dual-Core M Processor Family"},
    {0x3b, "AMD Opteron 6100 Series Processor"},
    {0x3c, "AMD Opteron 4100 Series Processor"},
    {0x3d, "AMD Opteron 6200 Series Processor"},
    {0x3e, "AMD Opteron 4200 Series Processor"},
    {0x3f, "AMD FX Series Processor"},
    {0x40, "MIPS Family"},
    {0x41, "MIPS R4000"},
    {0x42, "MIPS R4200"},
    {0x43, "MIPS R4400"},
    {0x44, "MIPS R4600"},
    {0x45, "MIPS R10000"},
    {0x46, "AMD C-Series Processor"},
    {0x47, "AMD E-Series Processor"},
    {0x48, "AMD A-Series Processor"},
    {0x49, "AMD G-Series Processor"},
    {0x4a, "AMD Z-Series Processor"},
    {0x4b, "AMD R-Series Processor"},
    {0x4c, "AMD Opteron 4300 Series Processor"},
    {0x4d, "AMD Opteron 6300 Series Processor"},
    {0x4e, "AMD Opteron 3300 Series Processor"},
    {0x4f, "AMD FirePro Series Processor"},
    {0x50, "SPARC Family"},
    {0x51, "SuperSPARC"},
    {0x52, "microSPARC II"},
    {0x53, "microSPARC IIep"},
    {0x54, "UltraSPARC"},
    {0x55, "UltraSPARC II"},
    {0x56, "UltraSPARC Iii"},
    {0x57, "UltraSPARC III"},
    {0x58, "UltraSPARC IIIi"},
    {0x60, "68040 Family"},
    {0x61, "68xxx"},
    {0x62, "68000"},
    {0x63, "68010"},
    {0x64, "68020"},
    {0x65, "68030"},
    {0x66, "AMD Athlon X4 Quad-Core Processor Family"},
    {0x67, "AMD Opteron X1000 Series Processor"},
    {0x68, "AMD Opteron X2000 Series APU"},
    {0x69, "AMD Opteron A-Series Processor"},
    {0x6a, "AMD Opteron X3000 Series APU"},
    {0x6b, "AMD Zen Processor Family"},
    {0x70, "Hobbit Family"},
    {0x78, "Crusoe TM5000 Family"},
    {0x79, "Crusoe TM3000 Family"},
    {0x7a, "Efficeon TM8000 Family"},
    {0x80, "Weitek"},
    {0x82, "Itanium processor"},
    {0x83, "AMD Athlon 64 Processor Family"},
    {0x84, "AMD Opteron Processor Family"},
    {0x85, "AMD Sempron Processor Family"},
    {0x86, "AMD Turion 64 Mobile Technology"},
    {0x87, "Dual-Core AMD Opteron Processor Family"},
    {0x88, "AMD Athlon 64 X2 Dual-Core Processor Family"},
    {0x89, "AMD Turion 64 X2 Mobile Technology"},
    {0x8a, "Quad-Core AMD Opteron Processor Family"},
    {0x8b, "Third-Generation AMD Opteron Processor Family"},
    {0x8c, "AMD Phenom FX Quad-Core Processor Family"},
    {0x8d, "AMD Phenom X4 Quad-Core Processor Family"},
    {0x8e, "AMD Phenom X2 Dual-Core Processor Family"},
    {0x8f, "AMD Athlon X2 Dual-Core Processor Family"},
    {0x90, "PA-RISC Family"},
    {0x91, "PA-RISC 8500"},
    {0x92, "PA-RISC 8000"},
    {0x93, "PA-RISC 7300LC"},
    {0x94, "PA-RISC 7200"},
    {0x95, "PA-RISC 7100LC"},
    {0x96, "PA-RISC 7100"},
    {0xa0, "V30 Family"},
    {0xa1, "Quad-Core Intel Xeon processor 3200 Series"},
    {0xa2, "Dual-Core Intel Xeon processor 3000 Series"},
    {0xa3, "Quad-Core Intel Xeon processor 5300 Series"},
    {0xa4, "Dual-Core Intel Xeon processor 5100 Series"},
    {0xa5, "Dual-Core Intel Xeon processor 5000 Series"},
    {0xa6, "Dual-Core Intel Xeon processor LV"},
    {0xa7, "Dual-Core Intel Xeon processor ULV"},
    {0xa8, "Dual-Core Intel Xeon processor 7100 Series"},
    {0xa9, "Quad-Core Intel Xeon processor 5400 Series"},
    {0xaa, "Quad-Core Intel Xeon processor"},
    {0xab, "Dual-Core Intel Xeon processor 5200 Series"},
    {0xac, "Dual-Core Intel Xeon processor 7200 Series"},
    {0xad, "Quad-Core Intel Xeon processor 7300 Series"},
    {0xae, "Quad-Core Intel Xeon processor 7400 Series"},
    {0xaf, "Multi-Core Intel Xeon processor 7400 Series"},
    {0xb0, "Pentium III Xeon processor"},
    {0xb1, "Pentium III Processor with Intel SpeedStep Technology"},
    {0xb2, "Pentium 4 Processor"},
    {0xb3, "Intel Xeon processor"},
    {0xb4, "AS400 Family"},
    {0xb5, "Intel Xeon processor MP"},
    {0xb6, "AMD Athlon XP Processor Family"},
    {0xb7, "AMD Athlon MP Processor Family"},
    {0xb8, "Intel Itanium 2 processor"},
    {0xb9, "Intel Pentium M processor"},
    {0xba, "Intel Celeron D processor"},
    {0xbb, "Intel Pentium D processor"},
    {0xbc, "Intel Pentium Processor Extreme Edition"},
    {0xbd, "Intel Core Solo Processor"},
    {0xbf, "Intel Core 2 Duo Processor"},
    {0xc0, "Intel Core 2 Solo processor"},
    {0xc1, "Intel Core 2 Extreme processor"},
    {0xc2, "Intel Core 2 Quad processor"},
    {0xc3, "Intel Core 2 Extreme mobile processor"},
    {0xc4, "Intel Core 2 Duo mobile processor"},
    {0xc5, "Intel Core 2 Solo mobile processor"},
    {0xc6, "Intel Core i7 processor"},
    {0xc7, "Dual-Core Intel Celeron processor"},
    {0xc8, "IBM390 Family"},
    {0xc9, "G4"},
    {0xca, "G5"},
    {0xcb, "ESA/390 G6"},
    {0xcc, "z/Architecture base"},
    {0xcd, "Intel Core i5 processor"},
    {0xce, "Intel Core i3 processor"},
    {0xcf, "Intel Core i9 processor"},
    {0xd2, "VIA C7-M Processor Family"},
    {0xd3, "VIA C7-D Processor Family"},
    {0xd4, "VIA C7 Processor Family"},
    {0xd5, "VIA Eden Processor Family"},
    {0xd6, "Multi-Core Intel Xeon processor"},
    {0xd7, "Dual-Core Intel Xeon processor 3xxx Series"},
    {0xd8, "Quad-Core Intel Xeon processor 3xxx Series"},
    {0xd9, "VIA Nano Processor Family"},
    {0xda, "Dual-Core Intel Xeon processor 5xxx Series"},
    {0xdb, "Quad-Core Intel Xeon processor 5xxx Series"},
    {0xdd, "Dual-Core Intel Xeon processor 7xxx Series"},
    {0xde, "Quad-Core Intel Xeon processor 7xxx Series"},
    {0xdf, "Multi-Core Intel Xeon processor 7xxx Series"},
    {0xe0, "Multi-Core Intel Xeon processor 3400 Series"},
    {0xe4, "AMD Opteron 3000 Series Processor"},
    {0xe5, "AMD Sempron II Processor"},
    {0xe6, "Embedded AMD Opteron Quad-Core Processor Family"},
    {0xe7, "AMD Phenom Triple-Core Processor Family"},
    {0xe8, "AMD Turion Ultra Dual-Core Mobile Processor Family"},
    {0xe9, "AMD Turion Dual-Core Mobile Processor Family"},
    {0xea, "AMD Athlon Dual-Core Processor Family"},
    {0xeb, "AMD Sempron SI Processor Family"},
    {0xec, "AMD Phenom II Processor Family"},
    {0xed, "AMD Athlon II Processor Family"},
    {0xee, "Six-core AMD Opteron Processor Family"},
    {0xef, "AMD Sempron M Processor Family"},
    {0xfa, "i860"},
    {0xfb, "i960"},
    {0xfe, "Processor Family 2 Indicator"}};

// This table is up to date as of SMBIOS spec DSP0134 3.7.0
static const std::map<uint16_t, const char*> family2Table = {
    {0x100, "ARMv7"},
    {0x101, "ARMv8"},
    {0x102, "ARMv9"},
    {0x104, "SH-3"},
    {0x105, "SH-4"},
    {0x118, "ARM"},
    {0x119, "StrongARM"},
    {0x12c, "6x86"},
    {0x12d, "MediaGX"},
    {0x12e, "MII"},
    {0x140, "WinChip"},
    {0x15e, "DSP"},
    {0x1f4, "Video Processor"},
    {0x200, "RISC-V RV32"},
    {0x201, "RISC-V RV64"},
    {0x202, "RISC-V RV128"},
    {0x258, "LoongArch"},
    {0x259, "Loongson 1 Processor Family"},
    {0x25a, "Loongson 2 Processor Family"},
    {0x25b, "Loongson 3 Processor Family"},
    {0x25c, "Loongson 2K Processor Family"},
    {0x25d, "Loongson 3A Processor Family"},
    {0x25e, "Loongson 3B Processor Family"},
    {0x25f, "Loongson 3C Processor Family"},
    {0x260, "Loongson 3D Processor Family"},
    {0x261, "Loongson 3E Processor Family"},
    {0x262, "Dual-Core Loongson 2K Processor 2xxx Series"},
    {0x26c, "Quad-Core Loongson 3A Processor 5xxx Series"},
    {0x26d, "Multi-Core Loongson 3A Processor 5xxx Series"},
    {0x26e, "Quad-Core Loongson 3B Processor 5xxx Series"},
    {0x26f, "Multi-Core Loongson 3B Processor 5xxx Series"},
    {0x270, "Multi-Core Loongson 3C Processor 5xxx Series"},
    {0x271, "Multi-Core Loongson 3D Processor 5xxx Series"}};

// Definition follow smbios spec DSP0134 3.0.0
static const std::array<std::optional<processor::Capability>, 16>
    characteristicsTable{
        std::nullopt,
        std::nullopt,
        processor::Capability::Capable64bit,
        processor::Capability::MultiCore,
        processor::Capability::HardwareThread,
        processor::Capability::ExecuteProtection,
        processor::Capability::EnhancedVirtualization,
        processor::Capability::PowerPerformanceControl,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt};

class Cpu :
    sdbusplus::server::object_t<processor, asset, location, connector, rev,
                                Item, association, operationalStatus,
                                assetTagType>
{
  public:
    Cpu() = delete;
    Cpu(const Cpu&) = delete;
    Cpu& operator=(const Cpu&) = delete;
    Cpu(Cpu&&) = delete;
    Cpu& operator=(Cpu&&) = delete;
    ~Cpu() = default;

    Cpu(sdbusplus::bus_t& bus, const std::string& objPath, const uint8_t& cpuId,
        uint8_t* smbiosTableStorage, const std::string& motherboard) :
        sdbusplus::server::object_t<processor, asset, location, connector, rev,
                                    Item, association, operationalStatus,
                                    assetTagType>(bus, objPath.c_str()),
        cpuNum(cpuId), storage(smbiosTableStorage), motherboardPath(motherboard)
    {
        infoUpdate(smbiosTableStorage, motherboard);
    }

    void infoUpdate(uint8_t* smbiosTableStorage,
                    const std::string& motherboard);

  private:
    uint8_t cpuNum;

    uint8_t* storage;

    std::string motherboardPath;

    struct ProcessorInfo
    {
        uint8_t type;
        uint8_t length;
        uint16_t handle;
        uint8_t socketDesignation;
        uint8_t processorType;
        uint8_t family;
        uint8_t manufacturer;
        uint64_t id;
        uint8_t version;
        uint8_t voltage;
        uint16_t exClock;
        uint16_t maxSpeed;
        uint16_t currSpeed;
        uint8_t status;
        uint8_t upgrade;
        uint16_t l1Handle;
        uint16_t l2Handle;
        uint16_t l3Handle;
        uint8_t serialNum;
        uint8_t assetTag;
        uint8_t partNum;
        uint8_t coreCount;
        uint8_t coreEnable;
        uint8_t threadCount;
        uint16_t characteristics;
        uint16_t family2;
        uint16_t coreCount2;
        uint16_t coreEnable2;
        uint16_t threadCount2;
    } __attribute__((packed));

    void socket(const uint8_t positionNum, const uint8_t structLen,
                uint8_t* dataIn);
    void family(const uint8_t family, const uint16_t family2);
    void manufacturer(const uint8_t positionNum, const uint8_t structLen,
                      uint8_t* dataIn);
    void serialNumber(const uint8_t positionNum, const uint8_t structLen,
                      uint8_t* dataIn);
    void partNumber(const uint8_t positionNum, const uint8_t structLen,
                    uint8_t* dataIn);
    void version(const uint8_t positionNum, const uint8_t structLen,
                 uint8_t* dataIn);
    void characteristics(const uint16_t value);
    void assetTagString(const uint8_t positionNum, const uint8_t structLen,
                        uint8_t* dataIn);
};

} // namespace smbios

} // namespace phosphor
