#include "uxn.h"

/*
Copyright (u) 2022-2024 Devine Lu Linvega, Andrew Alderwick, Andrew Richards

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE.
*/

#define OPC(opc, name, init, body) {\
	case 0x00|opc: __metajit_comment(name); {const int _2=0,_r=0;init body;} break;\
	case 0x20|opc: __metajit_comment(name "2"); {const int _2=1,_r=0;init body;} break;\
	case 0x40|opc: __metajit_comment(name "r"); {const int _2=0,_r=1;init body;} break;\
	case 0x60|opc: __metajit_comment(name "2r"); {const int _2=1,_r=1;init body;} break;\
	case 0x80|opc: __metajit_comment(name "k"); {const int _2=0,_r=0; Uint8* k=uxn->wst.ptr;init uxn->wst.ptr=k;body;} break;\
	case 0xa0|opc: __metajit_comment(name "2k"); {const int _2=1,_r=0; Uint8* k=uxn->wst.ptr;init uxn->wst.ptr=k;body;} break;\
	case 0xc0|opc: __metajit_comment(name "kr"); {const int _2=0,_r=1; Uint8* k=uxn->rst.ptr;init uxn->rst.ptr=k;body;} break;\
	case 0xe0|opc: __metajit_comment(name "2kr"); {const int _2=1,_r=1; Uint8* k=uxn->rst.ptr;init uxn->rst.ptr=k;body;} break;\
}

/* Microcode */

__attribute__((always_inline))
Uint8 emu_dei(Uxn* uxn, Uint8 addr) { return 0; }

__attribute__((always_inline))
void emu_deo(Uxn* uxn, Uint8 addr, Uint8 value) {}

#define JMI a = uxn->ram[pc] << 8 | uxn->ram[pc + 1], pc += a + 2;
#define REM if(_r) uxn->rst.ptr -= 1 + _2; else uxn->wst.ptr -= 1 + _2;
#define INC(s) *(uxn->s.ptr++)
#define DEC(s) *(--uxn->s.ptr)
#define JMP(x) { if(_2) pc = x; else pc += (Sint8)x; }
#define PO1(o) { o = _r ? DEC(rst) : DEC(wst);}
#define PO2(o) { if(_r) o = DEC(rst), o |= DEC(rst) << 8; else o = DEC(wst), o |= DEC(wst) << 8; }
#define POx(o) { if(_2) PO2(o) else PO1(o) }
#define PU1(i) { if(_r) INC(rst) = i; else INC(wst) = i; }
#define RP1(i) { if(_r) INC(wst) = i; else INC(rst) = i; }
#define PUx(i) { if(_2) { c = (i); PU1(c >> 8) PU1(c) } else PU1(i) }
#define GET(o) { if(_2) PO1(o[1]) PO1(o[0]) }
#define PUT(i) { PU1(i[0]) if(_2) PU1(i[1]) }
#define DEI(i,o) o[0] = emu_dei(uxn, i); if(_2) o[1] = emu_dei(uxn, i + 1); PUT(o)
#define DEO(i,j) emu_deo(uxn, i, j[0]); if(_2) emu_deo(uxn, i + 1, j[1]);
#define PEK(i,o,m) o[0] = uxn->ram[i]; if(_2) o[1] = uxn->ram[(i + 1) & m]; PUT(o)
#define POK(i,j,m) uxn->ram[i] = j[0]; if(_2) uxn->ram[(i + 1) & m] = j[1];

Uint8 __metajit_freeze_Uint8(Uint8);
Uint16 __metajit_freeze_Uint16(Uint16);
Uint8 __metajit_load_pure_Uint8(Uint8*);
Uint8* __metajit_load_pure_Uint8_ptr(Uint8**);
void* __metajit_assume_const_ptr(void*);
void __metajit_comment(const char*);

void
uxn_eval(Uxn* uxn)
{
	uxn = (Uxn*) __metajit_assume_const_ptr(uxn);
	Uint16 pc = __metajit_freeze_Uint16(uxn->pc);
	unsigned int a, b, c, x[2], y[2], z[2], step;
	if(!pc || __metajit_freeze_Uint8(uxn->dev[0x0f])) return;
	switch(__metajit_load_pure_Uint8(__metajit_load_pure_Uint8_ptr(&uxn->ram) + (pc++))) {
	/* BRK */ case 0x00: __metajit_comment("BRK"); pc = 0; break;
	/* JCI */ case 0x20: __metajit_comment("JCI"); if(DEC(wst)) { JMI break; } pc += 2; break;
	/* JMI */ case 0x40: __metajit_comment("JMI"); JMI break;
	/* JSI */ case 0x60: __metajit_comment("JSI"); c = pc + 2; INC(rst) = c >> 8; INC(rst) = c; JMI break;
	/* LI2 */ case 0xa0: __metajit_comment("LIT2"); INC(wst) = uxn->ram[pc++]; /* fall-through */
	/* LIT */ case 0x80: __metajit_comment("LIT"); INC(wst) = uxn->ram[pc++]; break;
	/* L2r */ case 0xe0: __metajit_comment("LIT2r"); INC(rst) = uxn->ram[pc++]; /* fall-through */
	/* LIr */ case 0xc0: __metajit_comment("LITr"); INC(rst) = uxn->ram[pc++]; break;
	/* INC */ OPC(0x01, "INC",POx(a),PUx(a + 1))
	/* POP */ OPC(0x02, "POP",REM   ,{})
	/* NIP */ OPC(0x03, "NIP",GET(x) REM   ,PUT(x))
	/* SWP */ OPC(0x04, "SWP",GET(x) GET(y),PUT(x) PUT(y))
	/* ROT */ OPC(0x05, "ROT",GET(x) GET(y) GET(z),PUT(y) PUT(x) PUT(z))
	/* DUP */ OPC(0x06, "DUP",GET(x),PUT(x) PUT(x))
	/* OVR */ OPC(0x07, "OVR",GET(x) GET(y),PUT(y) PUT(x) PUT(y))
	/* EQU */ OPC(0x08, "EQU",POx(a) POx(b),PU1(b == a))
	/* NEQ */ OPC(0x09, "NEQ",POx(a) POx(b),PU1(b != a))
	/* GTH */ OPC(0x0a, "GTH",POx(a) POx(b),PU1(b > a))
	/* LTH */ OPC(0x0b, "LTH",POx(a) POx(b),PU1(b < a))
	/* JMP */ OPC(0x0c, "JMP",POx(a),JMP(a))
	/* JCN */ OPC(0x0d, "JCN",POx(a) PO1(b),if(b) JMP(a))
	/* JSR */ OPC(0x0e, "JSR",POx(a),RP1(pc >> 8) RP1(pc) JMP(a))
	/* STH */ OPC(0x0f, "STH",GET(x),RP1(x[0]) if(_2) RP1(x[1]))
	/* LDZ */ OPC(0x10, "LDZ",PO1(a),PEK(a, x, 0xff))
	/* STZ */ OPC(0x11, "STZ",PO1(a) GET(y),POK(a, y, 0xff))
	/* LDR */ OPC(0x12, "LDR",PO1(a),PEK(pc + (Sint8)a, x, 0xffff))
	/* STR */ OPC(0x13, "STR",PO1(a) GET(y),POK(pc + (Sint8)a, y, 0xffff))
	/* LDA */ OPC(0x14, "LDA",PO2(a),PEK(a, x, 0xffff))
	/* STA */ OPC(0x15, "STA",PO2(a) GET(y),POK(a, y, 0xffff))
	/* DEI */ OPC(0x16, "DEI",PO1(a),DEI(a, x))
	/* DEO */ OPC(0x17, "DEO",PO1(a) GET(y),DEO(a, y))
	/* ADD */ OPC(0x18, "ADD",POx(a) POx(b),PUx(b + a))
	/* SUB */ OPC(0x19, "SUB",POx(a) POx(b),PUx(b - a))
	/* MUL */ OPC(0x1a, "MUL",POx(a) POx(b),PUx(b * a))
	/* DIV */ OPC(0x1b, "DIV",POx(a) POx(b),PUx(a ? b / a : 0))
	/* AND */ OPC(0x1c, "AND",POx(a) POx(b),PUx(b & a))
	/* ORA */ OPC(0x1d, "ORA",POx(a) POx(b),PUx(b | a))
	/* EOR */ OPC(0x1e, "EOR",POx(a) POx(b),PUx(b ^ a))
	/* SFT */ OPC(0x1f, "SFT",PO1(a) POx(b),PUx(b >> (a & 0xf) << (a >> 4)))
	}
	uxn->pc = pc;
}
