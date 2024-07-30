/*===================================================================*/
/*                                                                   */
/*  K6502.cpp : 6502 Emulator                                        */
/*                                                                   */
/*  1999/10/19  Racoon  New preparation                              */
/*  2001/12/22  ReGex   FrNES 0.60 Final Release - Not much changed  */
/*                      Now uses a jumptable                         */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "K6502.h"
#include "macros.h"

#include "pNesX.h"

/*-------------------------------------------------------------------*/
/*  Operation Macros                                                 */
/*-------------------------------------------------------------------*/

// Clock Op.
#define CLK(a)   g_wPassedClocks += (a); totalClocks += (a);

// Addressing Op.
// Address
// (Indirect,X)
//#define AA_IX    K6502_ReadZpW( K6502_Read( PC++ ) + X )
//#define AA_IX    K6502_ReadZpW( *(pPC++) + X )
#define AA_IX    ( RAM[ (unsigned char)(*pPC + X) ] | RAM[ (unsigned char)(*pPC + X + 1) ] << 8 )
//#define AA_IX    *(uint16 *)&RAM[ (unsigned char)(*pPC + X) ]
// (Indirect),Y
//#define AA_IY    K6502_ReadZpW( K6502_Read( PC++ ) ) + Y
//#define AA_IY    K6502_ReadZpW( *(pPC++) ) + Y
#define AA_IY    ( RAM[ *pPC ] | RAM[ (unsigned char)(*pPC + 1) ] << 8 ) + Y
//#define AA_IY    *(uint16 *)&RAM[ *pPC ] + Y
// Zero Page
//#define AA_ZP    K6502_Read( PC++ )
#define AA_ZP    (*(pPC++))
// Zero Page,X
//#define AA_ZPX   (unsigned char)( K6502_Read( PC++ ) + X )
#define AA_ZPX   (unsigned char)( *(pPC++) + X )
// Zero Page,Y
//#define AA_ZPY   (unsigned char)( K6502_Read( PC++ ) + Y )
#define AA_ZPY   (unsigned char)( *(pPC++) + Y )
// Absolute
#define AA_ABS   ( *pPC | *(pPC + 1) << 8 )
// Absolute2 ( PC-- )
//#define AA_ABS2  ( K6502_Read( PC++ ) | (uint16)K6502_Read( PC ) << 8 )
#define AA_ABS2   ( *pPC | *(pPC + 1) << 8 )
//#define AA_ABS2  *(uint16 *)pPC
// Absolute,X
//#define AA_ABSX  AA_ABS + X
#define AA_ABSX  AA_ABS + X
// Absolute,Y
//#define AA_ABSY  AA_ABS + Y
#define AA_ABSY  AA_ABS + Y

// Data
// (Indirect,X)
#define A_IX    K6502_Read( AA_IX )
// (Indirect),Y
#define A_IY    K6502_ReadIY()
// Zero Page
#define A_ZP    RAM[ AA_ZP ]
// Zero Page,X
#define A_ZPX   RAM[ AA_ZPX ]
// Zero Page,Y
#define A_ZPY   RAM[ AA_ZPY ]
// Absolute
#define A_ABS   K6502_Read( AA_ABS )
// Absolute,X
#define A_ABSX  K6502_ReadAbsX()
// Absolute,Y
#define A_ABSY  K6502_ReadAbsY()
// Immediate
#define A_IMM   (*(pPC++))

// Flag Op.
#define SETF(a)  F |= (a)
#define RSTF(a)  F &= ~(a)
#define TEST(a)  RSTF( FLAG_N | FLAG_Z ); SETF( g_byTestTable[ a ] )

// Load & Store Op.
#define STA(a)    K6502_Write( (a), A );
#define STX(a)    K6502_Write( (a), X );
#define STY(a)    K6502_Write( (a), Y );
#define LDA(a)    A = (a); TEST( A );
#define LDX(a)    X = (a); TEST( X );
#define LDY(a)    Y = (a); TEST( Y );

// Stack Op.
//#define PUSH(a)   K6502_Write( BASE_STACK + SP--, (a) )
#define PUSH(a)   RAM[ BASE_STACK + SP-- ] = (a)
#define PUSHW(a)  PUSH( (a) >> 8 ); PUSH( a )
//#define PUSHW(a)  *(uint16 *)&RAM[ BASE_STACK + SP - 1 ] = (a); SP -= 2;
//#define POP(a)    a = K6502_Read( BASE_STACK + ++SP )
#define POP(a)    a = RAM[ BASE_STACK + ++SP ]
//#define POPW(a)   POP(a); a |= ( K6502_Read( BASE_STACK + ++SP ) << 8 )
#define POPW(a)   POP(a); a |= ( RAM[ BASE_STACK + ++SP ] << 8 )
//#define POPW(a)   SP += 2; a = *(uint16 *)&RAM[ BASE_STACK + SP - 1 ]

// Logical Op.
#define ORA(a)  A |= (a); TEST( A )
#define AND(a)  A &= (a); TEST( A )
#define EOR(a)  A ^= (a); TEST( A )
#define BIT(a)  byD0 = (a); RSTF( FLAG_N | FLAG_V | FLAG_Z ); SETF( ( byD0 & ( FLAG_N | FLAG_V ) ) | ( ( byD0 & A ) ? 0 : FLAG_Z ) );
#define CMP(a)  wD0 = (uint16)A - (a); RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_byTestTable[ wD0 & 0xff ] | ( wD0 < 0x100 ? FLAG_C : 0 ) );
#define CPX(a)  wD0 = (uint16)X - (a); RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_byTestTable[ wD0 & 0xff ] | ( wD0 < 0x100 ? FLAG_C : 0 ) );
#define CPY(a)  wD0 = (uint16)Y - (a); RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_byTestTable[ wD0 & 0xff ] | ( wD0 < 0x100 ? FLAG_C : 0 ) );
  
// Math Op. (A D flag isn't being supported.)
#define ADC(a)  byD0 = (a); wD0 = A + byD0 + ( F & FLAG_C ); byD1 = (unsigned char)wD0; RSTF( FLAG_N | FLAG_V | FLAG_Z | FLAG_C ); SETF( g_byTestTable[ byD1 ] | ( ( ~( A ^ byD0 ) & ( A ^ byD1 ) & 0x80 ) ? FLAG_V : 0 ) | ( wD0 > 0xff ) ); A = byD1;

#define SBC(a)  byD0 = (a); wD0 = A - byD0 - ( ~F & FLAG_C ); byD1 = (unsigned char)wD0; RSTF( FLAG_N | FLAG_V | FLAG_Z | FLAG_C ); SETF( g_byTestTable[ byD1 ] | ( ( ( A ^ byD0 ) & ( A ^ byD1 ) & 0x80 ) ? FLAG_V : 0 ) | ( wD0 < 0x100 ) ); A = byD1;

#define DEC(a)  wA0 = a; byD0 = K6502_Read( wA0 ); --byD0; K6502_Write( wA0, byD0 ); TEST( byD0 )
#define INC(a)  wA0 = a; byD0 = K6502_Read( wA0 ); ++byD0; K6502_Write( wA0, byD0 ); TEST( byD0 )

// Shift Op.
#define ASLA    RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_ASLTable[ A ].byFlag ); A = g_ASLTable[ A ].byValue 
#define ASL(a)  RSTF( FLAG_N | FLAG_Z | FLAG_C ); wA0 = a; byD0 = K6502_Read( wA0 ); SETF( g_ASLTable[ byD0 ].byFlag ); K6502_Write( wA0, g_ASLTable[ byD0 ].byValue )
#define LSRA    RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_LSRTable[ A ].byFlag ); A = g_LSRTable[ A ].byValue 
#define LSR(a)  RSTF( FLAG_N | FLAG_Z | FLAG_C ); wA0 = a; byD0 = K6502_Read( wA0 ); SETF( g_LSRTable[ byD0 ].byFlag ); K6502_Write( wA0, g_LSRTable[ byD0 ].byValue ) 
#define ROLA    byD0 = F & FLAG_C; RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_ROLTable[ byD0 ][ A ].byFlag ); A = g_ROLTable[ byD0 ][ A ].byValue 
#define ROL(a)  byD1 = F & FLAG_C; RSTF( FLAG_N | FLAG_Z | FLAG_C ); wA0 = a; byD0 = K6502_Read( wA0 ); SETF( g_ROLTable[ byD1 ][ byD0 ].byFlag ); K6502_Write( wA0, g_ROLTable[ byD1 ][ byD0 ].byValue )
#define RORA    byD0 = F & FLAG_C; RSTF( FLAG_N | FLAG_Z | FLAG_C ); SETF( g_RORTable[ byD0 ][ A ].byFlag ); A = g_RORTable[ byD0 ][ A ].byValue 
#define ROR(a)  byD1 = F & FLAG_C; RSTF( FLAG_N | FLAG_Z | FLAG_C ); wA0 = a; byD0 = K6502_Read( wA0 ); SETF( g_RORTable[ byD1 ][ byD0 ].byFlag ); K6502_Write( wA0, g_RORTable[ byD1 ][ byD0 ].byValue )

// Jump Op.
#define JSR     wA0 = AA_ABS2; ++pPC; VIRPC; PUSHW( PC ); PC = wA0; REALPC;
#define BRA(a)  if ( a ) { VIRPC; wA0 = PC; PC += (char)(*pPC); CLK( 3 + ( ( wA0 & 0x0100 ) != ( PC & 0x0100 ) ) ); ++PC; REALPC; } else { ++pPC; CLK( 2 ); }
#define JMP(a)  PC = a; REALPC;

#if 0
#define REALPC  if ( PC != PredPC ) { PredPC = PC; switch ( PC >> 13 ) { case 0x0: pPC_Offset = RAM - ( PC & 0xf800 ); break; case 0x3: pPC_Offset = SRAM - ( PC & 0xe000 ); break; case 0x4: pPC_Offset = ROMBANK0 - ( PC & 0xe000 ); break; case 0x5: pPC_Offset = ROMBANK1 - ( PC & 0xe000 ); break; case 0x6: pPC_Offset = ROMBANK2 - ( PC & 0xe000 ); break; case 0x7: pPC_Offset = ROMBANK3 - ( PC & 0xe000 ); break; } pPC = pPC_Offset + PC; } else pPC = pPC_Offset + PC;
#else
#define REALPC  pPC_Offset = BankTable[ PC >> 13 ] - ( PC & BankMask[ PC >> 13 ] ); pPC = pPC_Offset + PC;
#endif

#define VIRPC   PC = pPC - pPC_Offset;

/*-------------------------------------------------------------------*/
/*  Global valiables                                                 */
/*-------------------------------------------------------------------*/

unsigned char HALT;

// 6502 Register
uint16 PC;
unsigned char SP;
unsigned char F;
unsigned char A;
unsigned char X;
unsigned char Y;

unsigned char *pPC;
unsigned char *pPC_Offset;
uint16 PredPC;
unsigned char *pPredPC;

unsigned char *BankTable[ 8 ];
uint16 BankMask[ 8 ] = { 0xf800, 0xf800, 0xf800, 0xe000, 0xe000, 0xe000, 0xe000, 0xe000 };

// The state of the IRQ pin
unsigned char IRQ_State;

// Wiring of the IRQ pin
unsigned char IRQ_Wiring;

// The state of the NMI pin
unsigned char NMI_State;

// Wiring of the NMI pin
unsigned char NMI_Wiring;

// The number of the clocks that it passed
uint16 g_wPassedClocks;
uint32 totalClocks;

// A table for the test
unsigned char g_byTestTable[ 256 ];

// Value and Flag Data
struct value_table_tag
{
  unsigned char byValue;
  unsigned char byFlag;
};

// A table for ASL
struct value_table_tag g_ASLTable[ 256 ];

// A table for LSR
struct value_table_tag g_LSRTable[ 256 ];

// A table for ROL
struct value_table_tag g_ROLTable[ 2 ][ 256 ];

// A table for ROR
struct value_table_tag g_RORTable[ 2 ][ 256 ];

struct OpcodeTable_tag OpcodeTable[256] = {
	{Op_00}, {Op_01}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_05}, {Op_06}, {Op_XX}, {Op_08}, {Op_09}, {Op_0A}, {Op_XX}, {Op_XX}, {Op_0D}, {Op_0E}, {Op_XX},
	{Op_10}, {Op_11}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_15}, {Op_16}, {Op_XX}, {Op_18}, {Op_19}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_1D}, {Op_1E}, {Op_XX},
	{Op_20}, {Op_21}, {Op_XX}, {Op_XX}, {Op_24}, {Op_25}, {Op_26}, {Op_XX}, {Op_28}, {Op_29}, {Op_2A}, {Op_XX}, {Op_2C}, {Op_2D}, {Op_2E}, {Op_XX},
	{Op_30}, {Op_31}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_35}, {Op_36}, {Op_XX}, {Op_38}, {Op_39}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_3D}, {Op_3E}, {Op_XX},
	{Op_40}, {Op_41}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_45}, {Op_46}, {Op_XX}, {Op_48}, {Op_49}, {Op_4A}, {Op_XX}, {Op_4C}, {Op_4D}, {Op_4E}, {Op_XX},
	{Op_50}, {Op_51}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_55}, {Op_56}, {Op_XX}, {Op_58}, {Op_59}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_5D}, {Op_5E}, {Op_XX},
	{Op_60}, {Op_61}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_65}, {Op_66}, {Op_XX}, {Op_68}, {Op_69}, {Op_6A}, {Op_XX}, {Op_6C}, {Op_6D}, {Op_6E}, {Op_XX},
	{Op_70}, {Op_71}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_75}, {Op_76}, {Op_XX}, {Op_78}, {Op_79}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_7D}, {Op_7E}, {Op_XX},
	{Op_XX}, {Op_81}, {Op_XX}, {Op_XX}, {Op_84}, {Op_85}, {Op_86}, {Op_XX}, {Op_88}, {Op_XX}, {Op_8A}, {Op_XX}, {Op_8C}, {Op_8D}, {Op_8E}, {Op_XX},
	{Op_90}, {Op_91}, {Op_XX}, {Op_XX}, {Op_94}, {Op_95}, {Op_96}, {Op_XX}, {Op_98}, {Op_99}, {Op_9A}, {Op_XX}, {Op_XX}, {Op_9D}, {Op_XX}, {Op_XX},
	{Op_A0}, {Op_A1}, {Op_A2}, {Op_XX}, {Op_A4}, {Op_A5}, {Op_A6}, {Op_XX}, {Op_A8}, {Op_A9}, {Op_AA}, {Op_XX}, {Op_AC}, {Op_AD}, {Op_AE}, {Op_XX},
	{Op_B0}, {Op_B1}, {Op_XX}, {Op_XX}, {Op_B4}, {Op_B5}, {Op_B6}, {Op_XX}, {Op_B8}, {Op_B9}, {Op_BA}, {Op_XX}, {Op_BC}, {Op_BD}, {Op_BE}, {Op_XX},
	{Op_C0}, {Op_C1}, {Op_XX}, {Op_XX}, {Op_C4}, {Op_C5}, {Op_C6}, {Op_XX}, {Op_C8}, {Op_C9}, {Op_CA}, {Op_XX}, {Op_CC}, {Op_CD}, {Op_CE}, {Op_XX},
	{Op_D0}, {Op_D1}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_D5}, {Op_D6}, {Op_XX}, {Op_D8}, {Op_D9}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_DD}, {Op_DE}, {Op_XX},
	{Op_E0}, {Op_E1}, {Op_XX}, {Op_XX}, {Op_E4}, {Op_E5}, {Op_E6}, {Op_XX}, {Op_E8}, {Op_E9}, {Op_EA}, {Op_XX}, {Op_EC}, {Op_ED}, {Op_EE}, {Op_XX},
	{Op_F0}, {Op_F1}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_F5}, {Op_F6}, {Op_XX}, {Op_F8}, {Op_F9}, {Op_XX}, {Op_XX}, {Op_XX}, {Op_FD}, {Op_FE}, {Op_XX}
};

uint16 wA0;
unsigned char byD0;
unsigned char byD1;
uint16 wD0;

#ifdef DEBUG
#define MAX_DISASM_STEPS 1024
#define MAX_DISASM_STRING 32
char DisassemblyBuffer[MAX_DISASM_STEPS][MAX_DISASM_STRING];

uint16 DisassemblyBufferIndex = 0;

void DisassembleInstruction(char* Opcode, char* fmt, uint16 value) {
	VIRPC;
	char p[16];
	if (fmt) {
		snprintf(p, 16, fmt, value);
	} else {
		p[0] = '\0';
	}
	snprintf(DisassemblyBuffer[DisassemblyBufferIndex], MAX_DISASM_STRING, "$%04x: %s %s", PC - 1, Opcode, p); 
	DisassemblyBufferIndex++;
	DisassemblyBufferIndex %= MAX_DISASM_STEPS;
	REALPC;
}

void UploadDisassembly() {
	printf("Uploading Disassembly to PC Host\n");
	char PCPath[256];
	// TODO: use a log time or something like that in the filename here
	snprintf(PCPath, 256, "/pc/Users/maslevin/Documents/Projects/numechanix/frnes/disasm.txt");	
	file_t PCFile = fs_open(PCPath, O_WRONLY);
	if (PCFile != -1) {
		int bufferIndex = DisassemblyBufferIndex;
		while (bufferIndex != DisassemblyBufferIndex - 1) {
			if (strcmp(DisassemblyBuffer[bufferIndex], "") != 0) {
				fs_write(PCFile, DisassemblyBuffer[bufferIndex], strlen(DisassemblyBuffer[bufferIndex]));
				fs_write(PCFile, "\n", 1);
			}
			bufferIndex++;
			bufferIndex %= MAX_DISASM_STEPS;
		}
		fs_close(PCFile);
	} else {
		printf("Error: Unable to Open File on PC Host\n");
	}
}

#else
#define DisassembleInstruction(...) (0)
#endif

void Op_00() {
	DisassembleInstruction("BRK", NULL, 0);
	VIRPC; PC++; PUSHW( PC ); SETF( FLAG_B ); PUSH( F ); SETF( FLAG_I ); PC = K6502_ReadW( VECTOR_IRQ ); REALPC; CLK( 7 );
}

// ORA (Zpg,X)
void Op_01() {
	DisassembleInstruction("ORA", "($%04x,x)", A_IX);
	ORA( A_IX ); ++pPC; CLK( 6 );
}

// ORA Zpg
void Op_05() {
	DisassembleInstruction("ORA", "$%02x", RAM[ (*(pPC + 1)) ]);
	ORA( A_ZP ); CLK( 3 );
}

// ASL Zpg
void Op_06() {
	DisassembleInstruction("ASL", "$%02x", K6502_Read( PC + 1 ));
	ASL( AA_ZP ); CLK( 5 );
}

// PHP
void Op_08() {
	DisassembleInstruction("PHP", NULL, 0);
	PUSH( F | FLAG_B | FLAG_R ); CLK( 3 );
}

// ORA #Oper
void Op_09() {
	DisassembleInstruction("ORA", "#$%02x", (*(pPC + 1)));
	ORA( A_IMM ); CLK( 2 );
}

// ASL A
void Op_0A() {
	DisassembleInstruction("ASL", NULL, 0);
	ASLA; CLK( 2 );
}

// ORA Abs
void Op_0D() {
	DisassembleInstruction("ORA", "$%04x", AA_ABS);
	ORA( A_ABS ); pPC += 2; CLK( 4 );
}

// ASL Abs
void Op_0E() {
	DisassembleInstruction("ASL", "$%04x", AA_ABS);
	ASL( AA_ABS ); pPC += 2; CLK( 6 );
}

// BPL Oper
void Op_10 () {
	DisassembleInstruction("BPL", "$%02x", K6502_Read( PC + 1 ));
	BRA( !( F & FLAG_N ) );
}

// ORA (Zpg),Y
void Op_11 () {
	DisassembleInstruction("ORA", "($%04x),y", AA_ABS);
	ORA( A_IY ); CLK( 5 );
}

// ORA Zpg,X
void Op_15 () {
	DisassembleInstruction("ORA", "$%02x,x", K6502_Read( PC + 1 ));	
	ORA( A_ZPX ); CLK( 4 );
}

// ASL Zpg,X
void Op_16() {
	DisassembleInstruction("ASL", "$%02x,x", K6502_Read( PC + 1 ));	
	ASL( AA_ZPX ); CLK( 6 );
}

// CLC
void Op_18() {
	DisassembleInstruction("CLC", NULL, 0);
	RSTF( FLAG_C ); CLK( 2 );
}

// ORA Abs,Y
void Op_19 () {
	DisassembleInstruction("ORA", "$%04x,y", AA_ABS);	
	ORA( A_ABSY ); CLK( 4 );
}

// ORA Abs,X
void Op_1D() {
	DisassembleInstruction("ORA", "$%04x,x", AA_ABS);
	ORA( A_ABSX ); CLK( 4 );
}

// ASL Abs,X
void Op_1E() {
	DisassembleInstruction("ASL", "$%04x,x", AA_ABS);
	ASL( AA_ABSX ); pPC += 2; CLK( 7 );
}

// JSR Abs
void Op_20() {
	DisassembleInstruction("JSR", "$%04x", AA_ABS);
	JSR; CLK( 6 );
}

// AND (Zpg,X)
void Op_21() {
	DisassembleInstruction("AND", "($%04x,x)", A_IX);
	AND( A_IX ); ++pPC; CLK( 6 );
}

// BIT Zpg
void Op_24() {
	DisassembleInstruction("BIT", "$%02x", K6502_Read( PC + 1 ));
	BIT( A_ZP ); CLK( 3 );
}

// AND Zpg
void Op_25() {
	DisassembleInstruction("AND", "$%02x", RAM[ (*(pPC + 1)) ]);
	AND( A_ZP ); CLK( 3 );
}

// ROL Zpg
void Op_26() {
	DisassembleInstruction("ROL", "$%02x", K6502_Read( PC + 1 ));	
	ROL( AA_ZP ); CLK( 5 );
}

// PLP
void Op_28() {
	DisassembleInstruction("PLP", NULL, 0);
	POP( F ); SETF( FLAG_R ); CLK( 4 );
}

// AND #Oper
void Op_29() {
	DisassembleInstruction("AND", "#$%02x", (*(pPC + 1)));	
	AND( A_IMM ); CLK( 2 );
}

// ROL A
void Op_2A() {
	DisassembleInstruction("ROL", "A", 0);	
	ROLA; CLK( 2 );
}

// BIT Abs
void Op_2C()  {
	DisassembleInstruction("BIT", "$%04x", AA_ABS);	
	BIT( A_ABS ); pPC += 2; CLK( 4 );
}

// AND Abs 
void Op_2D() {
	DisassembleInstruction("AND", "$%04x", AA_ABS);
	AND( A_ABS ); pPC += 2; CLK( 4 );
}

// ROL Abs
void Op_2E() {
	DisassembleInstruction("ROL", "$%04x", AA_ABS);
	ROL( AA_ABS ); pPC += 2; CLK( 6 );
}

// BMI Oper 
void Op_30() {
	DisassembleInstruction("BMI", "$%02x", K6502_Read( PC + 1 ));		
	BRA( F & FLAG_N );
}

// AND (Zpg),Y
void Op_31() {
	DisassembleInstruction("AND", "($%04x),y", AA_ABS);	
	AND( A_IY ); CLK( 5 );
}

// AND Zpg,X
void Op_35() {
	DisassembleInstruction("AND", "$%02x,x", K6502_Read( PC + 1 ));		
	AND( A_ZPX ); CLK( 4 );
}

// ROL Zpg,X
void Op_36() {
	DisassembleInstruction("ROL", "$%02x,x", K6502_Read( PC + 1 ));		
	ROL( AA_ZPX ); CLK( 6 );
}

// SEC
void Op_38() {
	DisassembleInstruction("SEC", NULL, 0);
	SETF( FLAG_C ); CLK( 2 );
}

// AND Abs,Y
void Op_39() {
	DisassembleInstruction("AND", "$%04x,y", AA_ABS);		
	AND( A_ABSY ); CLK( 4 );
}

// AND Abs,X
void Op_3D() {
	DisassembleInstruction("AND", "$%04x,x", AA_ABS);	
	AND( A_ABSX ); CLK( 4 );
}

// ROL Abs,X
void Op_3E() {
	DisassembleInstruction("ROL", "$%04x,x", AA_ABS);	
	ROL( AA_ABSX ); pPC += 2; CLK( 7 );
}

// RTI
void Op_40() {
	DisassembleInstruction("RTI", NULL, 0);
	POP( F ); SETF( FLAG_R ); POPW( PC ); REALPC; CLK( 6 );
}

// EOR (Zpg,X)
void Op_41() {
	DisassembleInstruction("EOR", "($%04x,x)", A_IX);	
	EOR( A_IX ); ++pPC; CLK( 6 );
}

// EOR Zpg
void Op_45() {
	DisassembleInstruction("EOR", "$%02x", RAM[ (*(pPC + 1)) ]);	
	EOR( A_ZP ); CLK( 3 );
}

// LSR Zpg
void Op_46() {
	DisassembleInstruction("LSR", "$%02x", K6502_Read( PC + 1 ));	
	LSR( AA_ZP ); CLK( 5 );
}

// PHA
void Op_48() {
	DisassembleInstruction("PHA", NULL, 0);	
	PUSH( A ); CLK( 3 );
}

// EOR #Oper
void Op_49() {
	DisassembleInstruction("EOR", "#$%02x", (*(pPC + 1)));		
	EOR( A_IMM ); CLK( 2 );
}

// LSR A
void Op_4A() {
	DisassembleInstruction("LSR", "A", 0);	
	LSRA; CLK( 2 );
}

// JMP Abs
void Op_4C() {
	DisassembleInstruction("JMP", "$%04x", AA_ABS);	
	JMP( AA_ABS ); CLK( 3 );
}

// EOR Abs
void Op_4D() {
	DisassembleInstruction("EOR", "$%04x", AA_ABS);	
	EOR( A_ABS ); pPC += 2; CLK( 4 );
}

// LSR Abs
void Op_4E() {
	DisassembleInstruction("LSR", "$%04x", AA_ABS);	
	LSR( AA_ABS ); pPC += 2; CLK( 6 );
}

// BVC
void Op_50() {
	DisassembleInstruction("BVC", "$%02x", K6502_Read( PC + 1 ));		
	BRA( !( F & FLAG_V ) );
}

// EOR (Zpg),Y
void Op_51() {
	DisassembleInstruction("EOR", "($%04x),y", AA_ABS);	
	EOR( A_IY ); CLK( 5 );
}

// EOR Zpg,X
void Op_55() {
	DisassembleInstruction("EOR", "$%02x,x", K6502_Read( PC + 1 ));		
	EOR( A_ZPX ); CLK( 4 );
}

// LSR Zpg,X
void Op_56() {
	DisassembleInstruction("LSR", "$%02x,x", K6502_Read( PC + 1 ));		
	LSR( AA_ZPX ); CLK( 6 );
}

// CLI
void Op_58() {
	DisassembleInstruction("CLI", NULL, 0);	
	byD0 = F;
    RSTF( FLAG_I ); CLK( 2 );
    if ( ( byD0 & FLAG_I ) && IRQ_State == 0 )
    {
      // IRQ Interrupt
      // Execute IRQ if an I flag isn't being set
      if ( !( F & FLAG_I ) )
      {
        CLK( 7 );

        VIRPC;
        PUSHW( PC );
        PUSH( F & ~FLAG_B );

        RSTF( FLAG_D );
        SETF( FLAG_I );

        PC = K6502_ReadW( VECTOR_IRQ );
        REALPC;
      }
    }
}

// EOR Abs,Y
void Op_59() {
	DisassembleInstruction("EOR", "$%04x,y", AA_ABS);		
	EOR( A_ABSY ); CLK( 4 );
}

// EOR Abs,X
void Op_5D() {
	DisassembleInstruction("EOR", "$%04x,x", AA_ABS);	
	EOR( A_ABSX ); CLK( 4 );
}

// LSR Abs,X
void Op_5E() {
	DisassembleInstruction("LSR", "$%04x,x", AA_ABS);	
	LSR( AA_ABSX ); pPC += 2; CLK( 7 );
}

// RTS
void Op_60() {
	DisassembleInstruction("RTS", NULL, 0);	
	POPW( PC ); ++PC; REALPC; CLK( 6 );
}

// ADC (Zpg,X)
void Op_61() {
	DisassembleInstruction("ADC", "($%04x,x)", A_IX);	
	ADC( A_IX ); ++pPC; CLK( 6 );
}

// ADC Zpg
void Op_65() {
	DisassembleInstruction("ADC", "$%02x", RAM[ (*(pPC + 1)) ]);	
	ADC( A_ZP ); CLK( 3 );
}

// ROR Zpg
void Op_66() {
	DisassembleInstruction("ROR", "$%02x", K6502_Read( PC + 1 ));	
	ROR( AA_ZP ); CLK( 5 );
}

// PLA
void Op_68() {
	DisassembleInstruction("PLA", NULL, 0);	
	POP( A ); TEST( A ); CLK( 4 );
}

// ADC #Oper
void Op_69() {
	DisassembleInstruction("ADC", "#$%02x", (*(pPC + 1)));		
	ADC( A_IMM ); CLK( 2 );
}

// ROR A
void Op_6A() {
	DisassembleInstruction("ROR", "A", 0);	
	RORA; CLK( 2 );
}

// JMP (Abs)
void Op_6C() {
	DisassembleInstruction("JMP", "($%04x)", AA_ABS);
	JMP( K6502_ReadW( AA_ABS ) ); CLK( 5 );
}

// ADC Abs
void Op_6D() {
	DisassembleInstruction("ADC", "$%04x", AA_ABS);	
	ADC( A_ABS ); pPC += 2; CLK( 4 );
}

// ROR Abs
void Op_6E() {
	DisassembleInstruction("ROR", "$%04x", AA_ABS);	
	ROR( AA_ABS ); pPC += 2; CLK( 6 );
}

// BVS
void Op_70() {
	DisassembleInstruction("BVS", "$%02x", K6502_Read( PC + 1 ));
	BRA( F & FLAG_V );
}

// ADC (Zpg),Y
void Op_71() {
	DisassembleInstruction("ADC", "($%04x),y", AA_ABS);
	ADC( A_IY ); CLK( 5 );
}

// ADC Zpg,X
void Op_75() {
	DisassembleInstruction("ADC", "$%02x,x", K6502_Read( PC + 1 ));		
	ADC( A_ZPX ); CLK( 4 );
}

// ROR Zpg,X
void Op_76() {
	DisassembleInstruction("ROR", "$%02x,x", K6502_Read( PC + 1 ));		
	ROR( AA_ZPX ); CLK( 6 );
}

// SEI
void Op_78() {
	DisassembleInstruction("SEI", NULL, 0);	
	SETF( FLAG_I ); CLK( 2 );
}

// ADC Abs,Y
void Op_79() {
	DisassembleInstruction("ADC", "$%04x,y", AA_ABS);		
	ADC( A_ABSY ); CLK( 4 );
}

// ADC Abs,X
void Op_7D() {
	DisassembleInstruction("ADC", "$%04x,x", AA_ABS);	
    ADC( A_ABSX ); CLK( 4 );
}

// ROR Abs,X
void Op_7E() {
	DisassembleInstruction("ROR", "$%04x,x", AA_ABS);	
	ROR( AA_ABSX ); pPC += 2; CLK( 7 );
}

// STA (Zpg,X)
void Op_81() {
	DisassembleInstruction("STA", "($%04x,x)", A_IX);	
	STA( AA_IX ); ++pPC; CLK( 6 );
}
      
 // STY Zpg
void Op_84() {
	DisassembleInstruction("STY", "$%02x", K6502_Read( PC + 1 ));	
	STY( AA_ZP ); CLK( 3 );
}

// STA Zpg
void Op_85() {
	DisassembleInstruction("STA", "$%02x", *pPC);
	STA( AA_ZP ); CLK( 3 );
}

// STX Zpg
void Op_86() {
	DisassembleInstruction("STX", "$%02x", K6502_Read( PC + 1 ));	
	STX( AA_ZP ); CLK( 3 );
}

// DEY
void Op_88() {
	DisassembleInstruction("DEY", NULL, 0);
	--Y; TEST( Y ); CLK( 2 );
}

// 89?

// TXA
void Op_8A() {
	DisassembleInstruction("TXA", NULL, 0);
	A = X; TEST( A ); CLK( 2 );
}

// STY Abs
void Op_8C() {
	DisassembleInstruction("STY", "$%04x", AA_ABS);	
	STY( AA_ABS ); pPC += 2; CLK( 4 );
}

// STA Abs
void Op_8D() {
	DisassembleInstruction("STA", "$%04x", AA_ABS);	
	STA( AA_ABS ); pPC += 2; CLK( 4 );
}

// STX Abs
void Op_8E() {
	DisassembleInstruction("STX", "$%04x", AA_ABS);	
	STX( AA_ABS ); pPC += 2; CLK( 4 );
}

// BCC
void Op_90() {
	DisassembleInstruction("BCC", "$%02x", K6502_Read( PC + 1 ));		
	BRA( !( F & FLAG_C ) );
}

// STA (Zpg),Y
void Op_91() {
	DisassembleInstruction("STA", "($%04x),y", AA_ABS);		
	STA( AA_IY ); ++pPC; CLK( 6 );
}

// STY Zpg,X
void Op_94() {
	DisassembleInstruction("STY", "$%02x,x", K6502_Read( PC + 1 ));	
	STY( AA_ZPX ); CLK( 4 );
}

// STA Zpg,X
void Op_95() {
	DisassembleInstruction("STA", "$%02x,x", K6502_Read( PC + 1 ));		
	STA( AA_ZPX ); CLK( 4 );
}

// STX Zpg,Y
void Op_96() {
	DisassembleInstruction("STX", "$%02x,x", K6502_Read( PC + 1 ));		
	STX( AA_ZPY ); CLK( 4 );
}

// TYA
void Op_98() {
	DisassembleInstruction("TYA", NULL, 0);	
	A = Y; TEST( A ); CLK( 2 );
}

// STA Abs,Y
void Op_99() {
	DisassembleInstruction("STA", "$%04x,y", AA_ABS);		
	STA( AA_ABSY ); pPC += 2; CLK( 5 );
}

// TXS
void Op_9A() {
	DisassembleInstruction("TXS", NULL, 0);	
	SP = X; CLK( 2 );
}

// STA Abs,X
void Op_9D() {
	DisassembleInstruction("STA", "$%04x,x", AA_ABS);	
	STA( AA_ABSX ); pPC += 2; CLK( 5 );
}

// LDY #Oper
void Op_A0() {
	DisassembleInstruction("LDY", "#$%02x", (*(pPC + 1)));	
	LDY( A_IMM ); CLK( 2 );
}

// LDA (Zpg,X)
void Op_A1() {
	DisassembleInstruction("LDA", "($%04x,x)", A_IX);	
	LDA( A_IX ); ++pPC; CLK( 6 );
}

// LDX #Oper
void Op_A2() {
	DisassembleInstruction("LDX", "#$%02x", (*(pPC + 1)));	
	LDX( A_IMM ); CLK( 2 );
}

// LDY Zpg
void Op_A4() {
	DisassembleInstruction("LDY", "$%02x", K6502_Read( PC + 1 ));	
	LDY( A_ZP ); CLK( 3 );
}

// LDA Zpg
void Op_A5() {
	DisassembleInstruction("LDA", "$%02x", RAM[ (*(pPC + 1)) ]);	
	LDA( A_ZP ); CLK( 3 );
}

// LDX Zpg
void Op_A6() {
	DisassembleInstruction("LDX", "$%02x", K6502_Read( PC + 1 ));	
	LDX( A_ZP ); CLK( 3 );
}

// TAY
void Op_A8() {
	DisassembleInstruction("TAY", NULL, 0);	
	Y = A; TEST( A ); CLK( 2 );
}

// LDA #Oper
void Op_A9() {
	DisassembleInstruction("LDA", "#$%02x", (*(pPC + 1)));		
	LDA( A_IMM ); CLK( 2 );
}

// TAX
void Op_AA() {
	DisassembleInstruction("TAX", NULL, 0);	
	X = A; TEST( A ); CLK( 2 );
}

// LDY Abs
void Op_AC() {
	DisassembleInstruction("LDY", "$%04x", AA_ABS);	
	LDY( A_ABS ); pPC += 2; CLK( 4 );
}

// LDA Abs
void Op_AD() {
	DisassembleInstruction("LDA", "$%04x", AA_ABS);	
	LDA( A_ABS ); pPC += 2; CLK( 4 );
}

// LDX Abs
void Op_AE() {
	DisassembleInstruction("LDX", "$%04x", AA_ABS);	
	LDX( A_ABS ); pPC += 2; CLK( 4 );
}

// BCS
void Op_B0() {
	DisassembleInstruction("BCS", "$%02x", K6502_Read( PC + 1 ));		
	BRA( F & FLAG_C );
}

// LDA (Zpg),Y
void Op_B1() {
	DisassembleInstruction("LDA", "($%04x),y", AA_ABS);		
	LDA( A_IY ); CLK( 5 );
}

// LDY Zpg,X
void Op_B4() {
	DisassembleInstruction("LDY", "$%02x,x", K6502_Read( PC + 1 ));
	LDY( A_ZPX ); CLK( 4 );
}

// LDA Zpg,X
void Op_B5() {
	DisassembleInstruction("LDA", "$%02x,x", K6502_Read( PC + 1 ));
	LDA( A_ZPX ); CLK( 4 );
}

// LDX Zpg,Y
void Op_B6() {
	DisassembleInstruction("LDX", "$%02x,x", K6502_Read( PC + 1 ));		
	LDX( A_ZPY ); CLK( 4 );
}

// CLV
void Op_B8() {
	DisassembleInstruction("CLV", NULL, 0);	
	RSTF( FLAG_V ); CLK( 2 );
}

// LDA Abs,Y
void Op_B9() {
	DisassembleInstruction("LDA", "$%04x,y", AA_ABS);		
	LDA( A_ABSY ); CLK( 4 );
}

// TSX
void Op_BA() {
	DisassembleInstruction("TSX", NULL, 0);	
	X = SP; TEST( X ); CLK( 2 );
}

// LDY Abs,X
void Op_BC() {
	DisassembleInstruction("LDY", "$%04x,x", AA_ABS);
	LDY( A_ABSX ); CLK( 4 );
}

// LDA Abs,X
void Op_BD() {
	DisassembleInstruction("LDA", "$%04x,x", AA_ABS);
	LDA( A_ABSX ); CLK( 4 );
}

// LDX Abs,Y
void Op_BE() {
	DisassembleInstruction("LDX", "$%04x,x", AA_ABS);	
	LDX( A_ABSY ); CLK( 4 );
}

// CPY #Oper
void Op_C0() {
	DisassembleInstruction("CPY", "#$%02x", (*(pPC + 1)));	
	CPY( A_IMM ); CLK( 2 );
}

// CMP (Zpg,X)
void Op_C1() {
	DisassembleInstruction("CMP", "($%04x,x)", A_IX);	
	CMP( A_IX ); ++pPC; CLK( 6 );
}

// CPY Zpg
void Op_C4() {
	DisassembleInstruction("CPY", "$%02x", K6502_Read( PC + 1 ));	
	CPY( A_ZP ); CLK( 3 );
}

// CMP Zpg
void Op_C5() {
	DisassembleInstruction("CMP", "$%02x", RAM[ (*(pPC + 1)) ]);	
	CMP( A_ZP ); CLK( 3 );
}

// DEC Zpg
void Op_C6() {
	DisassembleInstruction("DEC", "$%02x", K6502_Read( PC + 1 ));	
	DEC( AA_ZP ); CLK( 5 );
}

// INY
void Op_C8() {
	DisassembleInstruction("INY", NULL, 0);	
	++Y; TEST( Y ); CLK( 2 );	
}

// CMP #Oper
void Op_C9() {
	DisassembleInstruction("CMP", "#$%02x", (*(pPC + 1)));
	CMP( A_IMM ); CLK( 2 );
}

// DEX
void Op_CA() {
	DisassembleInstruction("DEX", NULL, 0);	
	--X; TEST( X ); CLK( 2 );
}

// CPY Abs
void Op_CC() {
	DisassembleInstruction("CPY", "$%04x", AA_ABS);	
	CPY( A_ABS ); pPC += 2; CLK( 4 );
}

// CMP Abs
void Op_CD() {
	DisassembleInstruction("CMP", "$%04x", AA_ABS);	
	CMP( A_ABS ); pPC += 2; CLK( 4 );
}

// DEC Abs
void Op_CE() {
	DisassembleInstruction("DEC", "$%04x", AA_ABS);	
	DEC( AA_ABS ); pPC += 2;CLK( 6 );
}

// BNE
void Op_D0() {
	DisassembleInstruction("BNE", "$%02x", K6502_Read( PC + 1 ));		
	BRA( !( F & FLAG_Z ) );
}

// CMP (Zpg),Y
void Op_D1() {
	DisassembleInstruction("CMP", "($%04x),y", AA_ABS);		
	CMP( A_IY ); CLK( 5 );
}

// CMP Zpg,X
void Op_D5 () {
	DisassembleInstruction("CMP", "$%02x,x", K6502_Read( PC + 1 ));		
	CMP( A_ZPX ); CLK( 4 );
}
        
// DEC Zpg,X
void Op_D6() {
	DisassembleInstruction("DEC", "$%02x,x", K6502_Read( PC + 1 ));		
	DEC( AA_ZPX ); CLK( 6 );
}

// CLD
void Op_D8() {
	DisassembleInstruction("CLD", NULL, 0);	
	RSTF( FLAG_D ); CLK( 2 );
}

// CMP Abs,Y
void Op_D9() {
	DisassembleInstruction("CMP", "$%04x,y", AA_ABS);		
	CMP( A_ABSY ); CLK( 4 );
}

// CMP Abs,X
void Op_DD() {
	DisassembleInstruction("CMP", "$%04x,x", AA_ABS);	
	CMP( A_ABSX ); CLK( 4 );
}

// DEC Abs,X
void Op_DE() {
	DisassembleInstruction("DEC", "$%04x,x", AA_ABS);	
	DEC( AA_ABSX ); pPC += 2; CLK( 7 );
}

// CPX #Oper
void Op_E0() {
	DisassembleInstruction("CPX", "#$%02x", (*(pPC + 1)));	
	CPX( A_IMM ); CLK( 2 );
}

// SBC (Zpg,X)
void Op_E1() {
	DisassembleInstruction("SBC", "($%04x,x)", A_IX);	
	SBC( A_IX ); ++pPC; CLK( 6 );
}

// CPX Zpg
void Op_E4() {
	DisassembleInstruction("CPX", "$%02x", K6502_Read( PC + 1 ));	
	CPX( A_ZP ); CLK( 3 );
}

// SBC Zpg
void Op_E5() {
	DisassembleInstruction("SBC", "$%02x", RAM[ (*(pPC + 1)) ]);	
	SBC( A_ZP ); CLK( 3 );
}

// INC Zpg
void Op_E6() {
	DisassembleInstruction("INC", "$%02x", K6502_Read( PC + 1 ));	
	INC( AA_ZP ); CLK( 5 );
}

// INX 
void Op_E8() {
	DisassembleInstruction("INX", NULL, 0);
	++X; TEST( X ); CLK( 2 );
}

// SBC #Oper
void Op_E9() {
	DisassembleInstruction("SBC", "#$%02x", (*(pPC + 1)));
	SBC( A_IMM ); CLK( 2 );
}

// NOP
void Op_EA() {
	DisassembleInstruction("NOP", NULL, 0);	
	CLK( 2 );
}

// CPX Abs
void Op_EC() {
	DisassembleInstruction("CPX", "$%04x", AA_ABS);
	CPX( A_ABS ); pPC += 2; CLK( 4 );
}

// SBC Abs
void Op_ED() {
	DisassembleInstruction("SBC", "$%04x", AA_ABS);	
	SBC( A_ABS ); pPC += 2; CLK( 4 );
}

// INC Abs
void Op_EE() {
	DisassembleInstruction("INC", "$%04x", AA_ABS);	
	INC( AA_ABS ); pPC += 2; CLK( 6 );
}

// BEQ
void Op_F0() {
	DisassembleInstruction("BEQ", "$%02x", K6502_Read( PC + 1 ));		
	BRA( F & FLAG_Z );
}

// SBC (Zpg),Y
void Op_F1() {
	DisassembleInstruction("SBC", "($%04x),y", AA_ABS);		
	SBC( A_IY ); CLK( 5 );
}

// SBC Zpg,X
void Op_F5() {
	DisassembleInstruction("SBC", "$%02x,x", K6502_Read( PC + 1 ));		
	SBC( A_ZPX ); CLK( 4 );
}

// INC Zpg,X
void Op_F6() {
	DisassembleInstruction("INC", "$%02x,x", K6502_Read( PC + 1 ));		
	INC( AA_ZPX ); CLK( 6 );
}

// SED
void Op_F8() {
	DisassembleInstruction("SED", NULL, 0);	
	SETF( FLAG_D ); CLK( 2 );
}

// SBC Abs,Y
void Op_F9() {
	DisassembleInstruction("SBC", "$%04x,y", AA_ABS);		
	SBC( A_ABSY ); CLK( 4 );
}

// SBC Abs,X
void Op_FD() {
	DisassembleInstruction("SBC", "$%04x,x", AA_ABS);	
	SBC( A_ABSX ); CLK( 4 );
}

// INC Abs,X
void Op_FE() {
	DisassembleInstruction("INC", "$%04x,x", AA_ABS);	
	INC( AA_ABSX ); pPC += 2; CLK( 7 );
}

// Unknown Instruction
void Op_XX() {
	DisassembleInstruction("HACF", NULL, 0);
	printf("WARNING: RUNNING UNDOCUMENTED INSTRUCTION - HALTING\n");
	HALT = 1;
	CLK( 2 );
}

/*===================================================================*/
/*                                                                   */
/*                K6502_Init() : Initialize K6502                    */
/*                                                                   */
/*===================================================================*/
void K6502_Init() {
/*
 *  Initialize K6502
 *
 *  You must call this function only once at first.
 */
  unsigned char idx;
  unsigned char idx2;

  HALT = 0;

  // The establishment of the IRQ pin
  NMI_Wiring = NMI_State = 1;
  IRQ_Wiring = IRQ_State = 1;

  // Make a table for the test
  idx = 0;
  do
  {
    if ( idx == 0 )
      g_byTestTable[ 0 ] = FLAG_Z;
    else
    if ( idx > 127 )
      g_byTestTable[ idx ] = FLAG_N;
    else
      g_byTestTable[ idx ] = 0;

    ++idx;
  } while ( idx != 0 );

  // Make a table ASL
  idx = 0;
  do
  {
    g_ASLTable[ idx ].byValue = idx << 1;
    g_ASLTable[ idx ].byFlag = 0;

    if ( idx > 127 )
      g_ASLTable[ idx ].byFlag = FLAG_C;

    if ( g_ASLTable[ idx ].byValue == 0 )
      g_ASLTable[ idx ].byFlag |= FLAG_Z;
    else
    if ( g_ASLTable[ idx ].byValue & 0x80 )
      g_ASLTable[ idx ].byFlag |= FLAG_N;

    ++idx;
  } while ( idx != 0 );

  // Make a table LSR
  idx = 0;
  do
  {
    g_LSRTable[ idx ].byValue = idx >> 1;
    g_LSRTable[ idx ].byFlag = 0;

    if ( idx & 1 )
      g_LSRTable[ idx ].byFlag = FLAG_C;

    if ( g_LSRTable[ idx ].byValue == 0 )
      g_LSRTable[ idx ].byFlag |= FLAG_Z;

    ++idx;
  } while ( idx != 0 );

  // Make a table ROL
  for ( idx2 = 0; idx2 < 2; ++idx2 )
  {
    idx = 0;
    do
    {
      g_ROLTable[ idx2 ][ idx ].byValue = ( idx << 1 ) | idx2;
      g_ROLTable[ idx2 ][ idx ].byFlag = 0;

      if ( idx > 127 )
        g_ROLTable[ idx2 ][ idx ].byFlag = FLAG_C;

      if ( g_ROLTable[ idx2 ][ idx ].byValue == 0 )
        g_ROLTable[ idx2 ][ idx ].byFlag |= FLAG_Z;
      else
      if ( g_ROLTable[ idx2 ][ idx ].byValue & 0x80 )
        g_ROLTable[ idx2 ][ idx ].byFlag |= FLAG_N;

      ++idx;
    } while ( idx != 0 );
  }

  // Make a table ROR
  for ( idx2 = 0; idx2 < 2; ++idx2 )
  {
    idx = 0;
    do
    {
      g_RORTable[ idx2 ][ idx ].byValue = ( idx >> 1 ) | ( idx2 << 7 );
      g_RORTable[ idx2 ][ idx ].byFlag = 0;

      if ( idx & 1 )
        g_RORTable[ idx2 ][ idx ].byFlag = FLAG_C;

      if ( g_RORTable[ idx2 ][ idx ].byValue == 0 )
        g_RORTable[ idx2 ][ idx ].byFlag |= FLAG_Z;
      else
      if ( g_RORTable[ idx2 ][ idx ].byValue & 0x80 )
        g_RORTable[ idx2 ][ idx ].byFlag |= FLAG_N;

      ++idx;
    } while ( idx != 0 );
  }
}

/*===================================================================*/
/*                                                                   */
/*                K6502_Reset() : Reset a CPU                        */
/*                                                                   */
/*===================================================================*/
void K6502_Reset()
{
/*
 *  Reset a CPU
 *
 */
	HALT = 0;

  // Reset Bank Table
  BankTable[ 0 ] = RAM;
  BankTable[ 1 ] = RAM;
  BankTable[ 2 ] = RAM;
  BankTable[ 3 ] = SRAM;
  BankTable[ 4 ] = ROMBANK0;
  BankTable[ 5 ] = ROMBANK1;
  BankTable[ 6 ] = ROMBANK2;
  BankTable[ 7 ] = ROMBANK3;

  // Reset Registers
  PC = K6502_ReadW( VECTOR_RESET );
  PredPC = 0;
  REALPC;
  SP = 0xFF;
  A = X = Y = 0;
  F = FLAG_Z | FLAG_R;

  // Set up the state of the Interrupt pin.
  NMI_State = NMI_Wiring;
  IRQ_State = IRQ_Wiring;

  // Reset Passed Clocks
  g_wPassedClocks = 0;
  totalClocks = 0;

  #ifdef DEBUG
  memset(DisassemblyBuffer, 0, MAX_DISASM_STEPS * MAX_DISASM_STRING);
  #endif
}

/*===================================================================*/
/*                                                                   */
/*    K6502_Set_Int_Wiring() : Set up wiring of the interrupt pin    */
/*                                                                   */
/*===================================================================*/
void K6502_Set_Int_Wiring( unsigned char byNMI_Wiring, unsigned char byIRQ_Wiring )
{
/*
 * Set up wiring of the interrupt pin
 *
 */

  NMI_Wiring = byNMI_Wiring;
  IRQ_Wiring = byIRQ_Wiring;
}

void K6502_DoNMI()
{
	if (NMI_State != NMI_Wiring)
	{
		// NMI Interrupt
		CLK( 7 );

		VIRPC;
		PUSHW( PC );
		PUSH( F & ~FLAG_B );

		RSTF( FLAG_D );

		PC = K6502_ReadW( VECTOR_NMI );
		REALPC;

		NMI_State = NMI_Wiring;
	}
}

void K6502_DoIRQ () {
  if ( IRQ_State != IRQ_Wiring ) {
    // IRQ Interrupt
    // Execute IRQ if an I flag isn't being set
    if ( !( F & FLAG_I ) ) {
      CLK( 7 );

      VIRPC;
      PUSHW( PC );
      PUSH( F & ~FLAG_B );

      RSTF( FLAG_D );
      SETF( FLAG_I );
    
      PC = K6502_ReadW( VECTOR_IRQ );
      REALPC;
    }

  	IRQ_State = IRQ_Wiring;	
  }
}

/*===================================================================*/
/*                                                                   */
/*  K6502_Step() :                                                   */
/*          Only the specified number of the clocks execute Op.      */
/*                                                                   */
/*===================================================================*/
void K6502_Step( uint16 wClocks )
{
/*
 *  Only the specified number of the clocks execute Op.
 *
 *  Parameters
 *    uint16 wClocks              (Read)
 *      The number of the clocks
 */

  wA0 = 0;
  byD0 = 0;
  byD1 = 0;
  wD0 = 0;
  
  K6502_DoIRQ();

  unsigned char opcode;

  // It has a loop until a constant clock passes
  while ( g_wPassedClocks < wClocks )
  {
    // Read an instruction
    opcode = *(pPC++);
    
    // Execute an instruction, run the instruction from the jump table.
	((OpcodeTable[opcode]).pFPtr)();

	if (HALT) {
		printf("Attempted to Run Illegal Opcode [%02X]\n", opcode);
		break;
	}
  }  /* end of while ... */

  // Correct the number of the clocks
  g_wPassedClocks -= wClocks;
}

// Addressing Op.
// Data
// Absolute,X
inline unsigned char K6502_ReadAbsX(){ uint16 wA0, wA1; wA0 = AA_ABS; pPC += 2; wA1 = wA0 + X; CLK( ( wA0 & 0x0100 ) != ( wA1 & 0x0100 ) ); return K6502_Read( wA1 ); };
// Absolute,Y
inline unsigned char K6502_ReadAbsY(){ uint16 wA0, wA1; wA0 = AA_ABS; pPC += 2; wA1 = wA0 + Y; CLK( ( wA0 & 0x0100 ) != ( wA1 & 0x0100 ) ); return K6502_Read( wA1 ); };
// (Indirect),Y
//static inline unsigned char K6502_ReadIY(){ uint16 wA0, wA1; wA0 = K6502_ReadZpW( K6502_Read( PC++ ) ); wA1 = wA0 + Y; CLK( ( wA0 & 0x0100 ) != ( wA1 & 0x0100 ) ); return K6502_Read( wA1 ); };
inline unsigned char K6502_ReadIY(){ uint16 wA0, wA1; wA0 = RAM[ *pPC ] | RAM[ (unsigned char)(*pPC + 1) ] << 8; ++pPC; wA1 = wA0 + Y; CLK( ( wA0 & 0x0100 ) != ( wA1 & 0x0100 ) ); return K6502_Read( wA1 ); };
//static inline unsigned char K6502_ReadIY(){ uint16 wA0, wA1; wA0 = *(uint16 *)&RAM[ *pPC ]; ++pPC; wA1 = wA0 + Y; CLK( ( wA0 & 0x0100 ) != ( wA1 & 0x0100 ) ); return K6502_Read( wA1 ); };

void K6502_Burn(uint16 wClocks) {
	g_wPassedClocks += wClocks;
	totalClocks += wClocks;
}

uint32 K6502_GetCycles() {
	return totalClocks;
}
