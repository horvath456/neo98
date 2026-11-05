// SPDX-License-Identifier: GPL-2.0-or-later
// ROP3 is an eight-bit Boolean truth table indexed by pattern, source, destination.
#pragma once
namespace DibEng {
using RasterOp = unsigned (*)(unsigned, unsigned, unsigned);
static unsigned Rop00(unsigned s, unsigned d, unsigned p) { return 0u; }
static unsigned Rop01(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d); }
static unsigned Rop02(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d); }
static unsigned Rop03(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d); }
static unsigned Rop04(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d); }
static unsigned Rop05(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d); }
static unsigned Rop06(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d); }
static unsigned Rop07(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d); }
static unsigned Rop08(unsigned s, unsigned d, unsigned p) { return (~p & s & d); }
static unsigned Rop09(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d); }
static unsigned Rop0a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d); }
static unsigned Rop0b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d); }
static unsigned Rop0c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d); }
static unsigned Rop0d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d); }
static unsigned Rop0e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d); }
static unsigned Rop0f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d); }
static unsigned Rop10(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d); }
static unsigned Rop11(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d); }
static unsigned Rop12(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d); }
static unsigned Rop13(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d); }
static unsigned Rop14(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d); }
static unsigned Rop15(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d); }
static unsigned Rop16(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d); }
static unsigned Rop17(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d); }
static unsigned Rop18(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop19(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop1a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop1b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop1c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop1d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop1e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop1f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d); }
static unsigned Rop20(unsigned s, unsigned d, unsigned p) { return (p & ~s & d); }
static unsigned Rop21(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop22(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & d); }
static unsigned Rop23(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & d); }
static unsigned Rop24(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & d); }
static unsigned Rop25(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & d); }
static unsigned Rop26(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d); }
static unsigned Rop27(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d); }
static unsigned Rop28(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & d); }
static unsigned Rop29(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop2a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop2b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop2c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop2d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop2e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop2f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d); }
static unsigned Rop30(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop31(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop32(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop33(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop34(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop35(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop36(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop37(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop38(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop39(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop3a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop3b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop3c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop3d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop3e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop3f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d); }
static unsigned Rop40(unsigned s, unsigned d, unsigned p) { return (p & s & ~d); }
static unsigned Rop41(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop42(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & s & ~d); }
static unsigned Rop43(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & s & ~d); }
static unsigned Rop44(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & s & ~d); }
static unsigned Rop45(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & s & ~d); }
static unsigned Rop46(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & s & ~d); }
static unsigned Rop47(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & s & ~d); }
static unsigned Rop48(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & s & ~d); }
static unsigned Rop49(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop4a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop4b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop4c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop4d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop4e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop4f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & s & ~d); }
static unsigned Rop50(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop51(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop52(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop53(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop54(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop55(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop56(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop57(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop58(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop59(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop5a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop5b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop5c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop5d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop5e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop5f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d); }
static unsigned Rop60(unsigned s, unsigned d, unsigned p) { return (p & ~s & d) | (p & s & ~d); }
static unsigned Rop61(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop62(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop63(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop64(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop65(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop66(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop67(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop68(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop69(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop6a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop6b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop6c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop6d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop6e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop6f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop70(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop71(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop72(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop73(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop74(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop75(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop76(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop77(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop78(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop79(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop7a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop7b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop7c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop7d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop7e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop7f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d); }
static unsigned Rop80(unsigned s, unsigned d, unsigned p) { return (p & s & d); }
static unsigned Rop81(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & s & d); }
static unsigned Rop82(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & s & d); }
static unsigned Rop83(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & s & d); }
static unsigned Rop84(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & s & d); }
static unsigned Rop85(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & s & d); }
static unsigned Rop86(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & s & d); }
static unsigned Rop87(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & s & d); }
static unsigned Rop88(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & s & d); }
static unsigned Rop89(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & s & d); }
static unsigned Rop8a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & s & d); }
static unsigned Rop8b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & s & d); }
static unsigned Rop8c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & s & d); }
static unsigned Rop8d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & s & d); }
static unsigned Rop8e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & s & d); }
static unsigned Rop8f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & s & d); }
static unsigned Rop90(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & s & d); }
static unsigned Rop91(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop92(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop93(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop94(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop95(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop96(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop97(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop98(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop99(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop9a(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop9b(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop9c(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop9d(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop9e(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Rop9f(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & d); }
static unsigned Ropa0(unsigned s, unsigned d, unsigned p) { return (p & ~s & d) | (p & s & d); }
static unsigned Ropa1(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa2(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa3(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa4(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa5(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa6(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa7(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa8(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropa9(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropaa(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropab(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropac(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropad(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropae(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropaf(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb0(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb1(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb2(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb3(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb4(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb5(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb6(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb7(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb8(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropb9(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropba(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropbb(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropbc(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropbd(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropbe(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropbf(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & d); }
static unsigned Ropc0(unsigned s, unsigned d, unsigned p) { return (p & s & ~d) | (p & s & d); }
static unsigned Ropc1(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc2(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc3(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc4(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc5(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc6(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc7(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc8(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropc9(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropca(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropcb(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropcc(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropcd(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropce(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropcf(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd0(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd1(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd2(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd3(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd4(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd5(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd6(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd7(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd8(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropd9(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropda(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropdb(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropdc(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropdd(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropde(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropdf(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope0(unsigned s, unsigned d, unsigned p) { return (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope1(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope2(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope3(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope4(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope5(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope6(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope7(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope8(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Rope9(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropea(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropeb(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropec(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Roped(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropee(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropef(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf0(unsigned s, unsigned d, unsigned p) { return (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf1(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf2(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf3(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf4(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf5(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf6(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf7(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf8(unsigned s, unsigned d, unsigned p) { return (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropf9(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropfa(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropfb(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropfc(unsigned s, unsigned d, unsigned p) { return (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropfd(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropfe(unsigned s, unsigned d, unsigned p) { return (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static unsigned Ropff(unsigned s, unsigned d, unsigned p) { return (~p & ~s & ~d) | (~p & ~s & d) | (~p & s & ~d) | (~p & s & d) | (p & ~s & ~d) | (p & ~s & d) | (p & s & ~d) | (p & s & d); }
static const RasterOp RasterOps[] = {Rop00, Rop01, Rop02, Rop03, Rop04, Rop05, Rop06, Rop07, Rop08, Rop09, Rop0a, Rop0b, Rop0c, Rop0d, Rop0e, Rop0f, Rop10, Rop11, Rop12, Rop13, Rop14, Rop15, Rop16, Rop17, Rop18, Rop19, Rop1a, Rop1b, Rop1c, Rop1d, Rop1e, Rop1f, Rop20, Rop21, Rop22, Rop23, Rop24, Rop25, Rop26, Rop27, Rop28, Rop29, Rop2a, Rop2b, Rop2c, Rop2d, Rop2e, Rop2f, Rop30, Rop31, Rop32, Rop33, Rop34, Rop35, Rop36, Rop37, Rop38, Rop39, Rop3a, Rop3b, Rop3c, Rop3d, Rop3e, Rop3f, Rop40, Rop41, Rop42, Rop43, Rop44, Rop45, Rop46, Rop47, Rop48, Rop49, Rop4a, Rop4b, Rop4c, Rop4d, Rop4e, Rop4f, Rop50, Rop51, Rop52, Rop53, Rop54, Rop55, Rop56, Rop57, Rop58, Rop59, Rop5a, Rop5b, Rop5c, Rop5d, Rop5e, Rop5f, Rop60, Rop61, Rop62, Rop63, Rop64, Rop65, Rop66, Rop67, Rop68, Rop69, Rop6a, Rop6b, Rop6c, Rop6d, Rop6e, Rop6f, Rop70, Rop71, Rop72, Rop73, Rop74, Rop75, Rop76, Rop77, Rop78, Rop79, Rop7a, Rop7b, Rop7c, Rop7d, Rop7e, Rop7f, Rop80, Rop81, Rop82, Rop83, Rop84, Rop85, Rop86, Rop87, Rop88, Rop89, Rop8a, Rop8b, Rop8c, Rop8d, Rop8e, Rop8f, Rop90, Rop91, Rop92, Rop93, Rop94, Rop95, Rop96, Rop97, Rop98, Rop99, Rop9a, Rop9b, Rop9c, Rop9d, Rop9e, Rop9f, Ropa0, Ropa1, Ropa2, Ropa3, Ropa4, Ropa5, Ropa6, Ropa7, Ropa8, Ropa9, Ropaa, Ropab, Ropac, Ropad, Ropae, Ropaf, Ropb0, Ropb1, Ropb2, Ropb3, Ropb4, Ropb5, Ropb6, Ropb7, Ropb8, Ropb9, Ropba, Ropbb, Ropbc, Ropbd, Ropbe, Ropbf, Ropc0, Ropc1, Ropc2, Ropc3, Ropc4, Ropc5, Ropc6, Ropc7, Ropc8, Ropc9, Ropca, Ropcb, Ropcc, Ropcd, Ropce, Ropcf, Ropd0, Ropd1, Ropd2, Ropd3, Ropd4, Ropd5, Ropd6, Ropd7, Ropd8, Ropd9, Ropda, Ropdb, Ropdc, Ropdd, Ropde, Ropdf, Rope0, Rope1, Rope2, Rope3, Rope4, Rope5, Rope6, Rope7, Rope8, Rope9, Ropea, Ropeb, Ropec, Roped, Ropee, Ropef, Ropf0, Ropf1, Ropf2, Ropf3, Ropf4, Ropf5, Ropf6, Ropf7, Ropf8, Ropf9, Ropfa, Ropfb, Ropfc, Ropfd, Ropfe, Ropff};
static const unsigned char RasterInputs[] = {0, 7, 7, 6, 7, 5, 7, 7, 7, 7, 5, 7, 6, 7, 7, 4, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 7, 7, 2, 7, 7, 7, 7, 7, 7, 7, 7, 6, 7, 7, 6, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 7, 7, 7, 7, 1, 7, 7, 7, 7, 5, 7, 7, 7, 7, 5, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 5, 7, 7, 7, 7, 5, 7, 7, 7, 7, 1, 7, 7, 7, 7, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 6, 7, 7, 6, 7, 7, 7, 7, 7, 7, 7, 7, 2, 7, 7, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 4, 7, 7, 6, 7, 5, 7, 7, 7, 7, 5, 7, 6, 7, 7, 0};
}
