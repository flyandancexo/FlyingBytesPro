// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

namespace usbasp {

inline constexpr quint16 SharedVid = 0x16C0;
inline constexpr quint16 SharedPid = 0x05DC;
inline constexpr quint16 OldVid = 0x03EB;
inline constexpr quint16 OldPid = 0xC7B4;

inline constexpr quint8 FuncConnect = 1;
inline constexpr quint8 FuncDisconnect = 2;
inline constexpr quint8 FuncTransmit = 3;
inline constexpr quint8 FuncReadFlash = 4;
inline constexpr quint8 FuncEnableProgramming = 5;
inline constexpr quint8 FuncWriteFlash = 6;
inline constexpr quint8 FuncReadEeprom = 7;
inline constexpr quint8 FuncWriteEeprom = 8;
inline constexpr quint8 FuncSetLongAddress = 9;
inline constexpr quint8 FuncSetIspSck = 10;
inline constexpr quint8 FuncTpiConnect = 11;
inline constexpr quint8 FuncTpiDisconnect = 12;
inline constexpr quint8 FuncTpiRawRead = 13;
inline constexpr quint8 FuncTpiRawWrite = 14;
inline constexpr quint8 FuncTpiReadBlock = 15;
inline constexpr quint8 FuncTpiWriteBlock = 16;
inline constexpr quint8 FuncGetCapabilities = 127;

inline constexpr quint8 BlockFlagFirst = 1;
inline constexpr quint8 BlockFlagLast = 2;
inline constexpr int ReadBlockSize = 200;
inline constexpr int WriteBlockSize = 200;
inline constexpr int QueueSafeFlashWriteBlockSize = 128;
inline constexpr int SlowReadBlockSize = 8;
inline constexpr int SlowWriteBlockSize = 8;
inline constexpr int TpiBlockSize = 32;

inline constexpr quint8 TpiOpSstInc = 0x64;
inline constexpr quint8 TpiOpSstPr(int index) { return static_cast<quint8>(0x68 | (index & 0x03)); }
inline constexpr quint8 TpiOpSin(int address) { return static_cast<quint8>(0x10 | ((address << 1) & 0x60) | (address & 0x0F)); }
inline constexpr quint8 TpiOpSout(int address) { return static_cast<quint8>(0x90 | ((address << 1) & 0x60) | (address & 0x0F)); }
inline constexpr quint8 TpiOpSldCs(int address) { return static_cast<quint8>(0x80 | (address & 0x0F)); }
inline constexpr quint8 TpiOpSstCs(int address) { return static_cast<quint8>(0xC0 | (address & 0x0F)); }
inline constexpr quint8 TpiIr = 0x0F;
inline constexpr quint8 TpiPcr = 0x02;
inline constexpr quint8 TpiSr = 0x00;
inline constexpr quint8 TpiPcrGuard2Bit = 0x06;
inline constexpr quint8 TpiSrNvmEnable = 0x02;
inline constexpr quint8 TpiNvmCsr = 0x32;
inline constexpr quint8 TpiNvmCmd = 0x33;
inline constexpr quint8 TpiNvmBusy = 0x80;
inline constexpr quint8 TpiNvmChipErase = 0x10;
inline constexpr quint8 TpiNvmSectionErase = 0x14;
inline constexpr quint8 TpiNvmWordWrite = 0x1D;

inline constexpr quint32 CapabilityTpi = 0x00000001u;
inline constexpr quint32 Capability3MHz = 0x01000000u;

enum class IspClock : quint8 {
    Pro = 0,
    Hz500 = 1,
    KHz1 = 2,
    KHz2 = 3,
    KHz4 = 4,
    KHz8 = 5,
    KHz16 = 6,
    KHz32 = 7,
    KHz93_75 = 8,
    KHz187_5 = 9,
    KHz375 = 10,
    KHz750 = 11,
    MHz1_5 = 12,
    MHz3 = 13,
    Auto = 14
};

inline constexpr int clockFrequencyHz(IspClock clock)
{
    switch (clock) {
    case IspClock::Hz500: return 500;
    case IspClock::KHz1: return 1'000;
    case IspClock::KHz2: return 2'000;
    case IspClock::KHz4: return 4'000;
    case IspClock::KHz8: return 8'000;
    case IspClock::KHz16: return 16'000;
    case IspClock::KHz32: return 32'000;
    case IspClock::KHz93_75: return 93'750;
    case IspClock::KHz187_5: return 187'500;
    case IspClock::KHz375: return 375'000;
    case IspClock::KHz750: return 750'000;
    case IspClock::MHz1_5: return 1'500'000;
    case IspClock::MHz3: return 3'000'000;
    case IspClock::Pro: return 0;
    case IspClock::Auto: return 0;
    }
    return 0;
}

} // namespace usbasp
