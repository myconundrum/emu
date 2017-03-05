
#include <time.h>
#include <string.h>
#include "emu.h"
#include "cia1.h"

#define MAX_CHARS 256

typedef struct {
	byte column;
	byte row;
} KEYMAP;

KEYMAP g_ciaKeyboardTable[MAX_CHARS] = {0};

#define CIA1_LATCH 0x01
#define CIA1_REAL  0x00

typedef struct {

	byte kbd[0x08];				// keyboard column matrix.
	byte regs[2][0x10];			// CIA1 internal registers. use CIA1_REGS enum to address.
	byte isr;  					// read pulls current irq status

	unsigned long lticks;		// stores the value of sysclock_getticks() on last cia1_update()
	bool todlatched;			// if true, registers will not update on reads until
								// the tenths register is read. 
} CIA1;


CIA1 g_cia1;



byte cia1_getreal(byte reg) {
	return g_cia1.regs[CIA1_REAL][reg];
}

void cia1_setreal(byte reg,byte val) {
	g_cia1.regs[CIA1_REAL][reg] = val;
}

byte cia1_getlatched(byte reg) {
	return g_cia1.regs[CIA1_LATCH][reg];
}

void cia1_setlatched(byte reg,byte val) {
	g_cia1.regs[CIA1_LATCH][reg] = val;
}

void cia1_latchtoreal(byte reg) {
	g_cia1.regs[CIA1_REAL][reg] = g_cia1.regs[CIA1_LATCH][reg];
}

void cia1_realtolatch(byte reg) {
	g_cia1.regs[CIA1_LATCH][reg] = g_cia1.regs[CIA1_REAL][reg];
}

byte cia1_getkbdprb() {

	int i = 0;
	byte val = 0xff;

	for (i = 0; i < 8; i++) {

		if (~cia1_getreal(CIA1_PRA) & (0x01 << i)) {
			val &= g_cia1.kbd[i];
		}
	}
	return val;

}


void cia1_latchtod() {

	g_cia1.todlatched 		= true;
	cia1_realtolatch(CIA1_TODHRS);
	cia1_realtolatch(CIA1_TODMINS);
	cia1_realtolatch(CIA1_TODSECS);
	cia1_realtolatch(CIA1_TODTENTHS);
}

byte cia1_readtod(byte reg) {
   return g_cia1.todlatched ? cia1_getlatched(reg) : cia1_getreal(reg);
}



byte cia1_peek(byte address) {

	byte val;

	switch(address %0x10) {
		case CIA1_ICR: 
			val = g_cia1.isr;
		break;
		case CIA1_PRB:
			val = cia1_getkbdprb();
		break;
		case CIA1_TODHRS: 
			cia1_latchtod();
			val = cia1_readtod(CIA1_TODHRS);
		break;
		case CIA1_TODMINS: 
			val = cia1_readtod(CIA1_TODMINS);
		break;
		case CIA1_TODSECS: 
			val = cia1_readtod(CIA1_TODSECS);
		break;		
		case CIA1_TODTENTHS: 
			val = cia1_readtod(CIA1_TODTENTHS);
			g_cia1.todlatched = false;
		break;
		default: 
			val = cia1_getreal(address % 0x10);
		break;
	}
	return val;
}

void cia1_seticr(byte val) {

	byte old = cia1_getreal(CIA1_ICR);
	byte fillbit = val & CIA_FLAG_FILIRQ ? 0xFF : 0;
	byte new = 0;

	/*
		Enabling or Disabling IRQSs. Bits 0-6 specify which IRQs you want to enable/disable
		(a 1 in a bit position means you are attempting to change that IRQ)

		Bit 7 is the bit that is used for these. In other words -- if a bit is set in 
		positions 0-6, then use the value of bit 7 for that bit, otherwise, keep existing
		value.

		At least that's how I read the documentation :-)
	*/

	new |= (val & 0x01) ? (fillbit & 0x01) : (old & 0x01);  
	new |= (val & 0x02) ? (fillbit & 0x02) : (old & 0x02);
	new |= (val & 0x04) ? (fillbit & 0x04) : (old & 0x04);  
	new |= (val & 0x08) ? (fillbit & 0x08) : (old & 0x08);
	new |= (val & 0x10) ? (fillbit & 0x10) : (old & 0x10);  
	new |= (val & 0x20) ? (fillbit & 0x20) : (old & 0x20);
	new |= (val & 0x40) ? (fillbit & 0x40) : (old & 0x40);  
	
	cia1_setreal(CIA1_ICR,new);
}

void cia_setport(CIA1_REGISTERS reg, CIA1_REGISTERS ddr, byte val) {

	byte test = 0b10000000;
	byte new = 0;
	byte portval = cia1_getreal(reg);
	byte ddrval  = cia1_getreal(ddr);

	while (test) {
		new |= (test & ddrval) ? (test & val) : (test & portval);
		test >>= 1;
	}
	cia1_setreal(reg,new);
}

void cia1_poke(byte address,byte val) {

	byte reg = address % 0x10;

	switch (address % 0x10) {

		case CIA1_PRA: 			// data port a register
			cia_setport(CIA1_PRA,CIA1_DDRA,val);
		break;
		case CIA1_PRB: 			// data port b register
			cia_setport(CIA1_PRB,CIA1_DDRB,val);
		break;
		//
		// timers sets. 
		//
		case CIA1_TALO: case CIA1_TAHI: case CIA1_TBLO: case CIA1_TBHI:		 
			cia1_setreal(reg,val);
			cia1_setlatched(reg,val);			
		break;
		case CIA1_ICR:			// Interupt control and status 
			cia1_seticr(val);
		break;
		case CIA1_CRA:			// Timer A control register 
			cia1_setreal(reg,val);
			// force load startlatch into current 
			if (val & CIA_CR_FORCELATCH) { 
				cia1_latchtoreal(CIA1_TALO);
				cia1_latchtoreal(CIA1_TAHI);
			}		
		break;
		case CIA1_CRB:			// Timer B control register 
			cia1_setreal(reg,val);
			// force load startlatch into current 
			if (val & CIA_CR_FORCELATCH) { 
				cia1_latchtoreal(CIA1_TBLO);
				cia1_latchtoreal(CIA1_TBHI);
			}	
		break;
		default: cia1_setreal(reg,val);
		break;
	}	
}

void ciaInitChar(byte ch, byte col, byte row) {

	g_ciaKeyboardTable[ch].column = col;
	g_ciaKeyboardTable[ch].row = row;
}

void cia1_init() {

	int i;
	memset(&g_cia1,0,sizeof(CIA1));

	ciaInitChar(C64KEY_RUNSTOP,7,ROW_7); // STOP KEY NOT IMPL
	ciaInitChar('/',6,ROW_7);
	ciaInitChar(',',5,ROW_7);
	ciaInitChar('N',4,ROW_7);
	ciaInitChar('V',3,ROW_7);
	ciaInitChar('X',2,ROW_7);
	ciaInitChar(C64KEY_LSHIFT,1,ROW_7);
	ciaInitChar(C64KEY_CURDOWN,0,ROW_7);

	ciaInitChar('Q',7,ROW_6); 
	ciaInitChar('^',6,ROW_6);
	ciaInitChar('@',5,ROW_6);
	ciaInitChar('O',4,ROW_6);
	ciaInitChar('U',3,ROW_6);
	ciaInitChar('T',2,ROW_6);
	ciaInitChar('E',1,ROW_6);
	ciaInitChar(C64KEY_F5,0,ROW_6);

	ciaInitChar(C64KEY_C64,7,ROW_5); 
	ciaInitChar('=',6,ROW_5);
	ciaInitChar(':',5,ROW_5);
	ciaInitChar('K',4,ROW_5);
	ciaInitChar('H',3,ROW_5);
	ciaInitChar('F',2,ROW_5);
	ciaInitChar('S',1,ROW_5);
	ciaInitChar(C64KEY_F3,0,ROW_5);

	ciaInitChar(' ',7,ROW_4); 
	ciaInitChar(C64KEY_RSHIFT,6,ROW_4);
	ciaInitChar('.',5,ROW_4);
	ciaInitChar('M',4,ROW_4);
	ciaInitChar('B',3,ROW_4);
	ciaInitChar('C',2,ROW_4);
	ciaInitChar('Z',1,ROW_4);
	ciaInitChar(C64KEY_F1,0,ROW_4);

	ciaInitChar('2',7,ROW_3); 
	ciaInitChar(C64KEY_HOME,6,ROW_3);
	ciaInitChar('-',5,ROW_3);
	ciaInitChar('0',4,ROW_3);
	ciaInitChar('8',3,ROW_3);
	ciaInitChar('6',2,ROW_3);
	ciaInitChar('4',1,ROW_3);
	ciaInitChar(C64KEY_F7,0,ROW_3);

	ciaInitChar(C64KEY_CTRL,7,ROW_2); 
	ciaInitChar(';',6,ROW_2);
	ciaInitChar('L',5,ROW_2);
	ciaInitChar('J',4,ROW_2);
	ciaInitChar('G',3,ROW_2);
	ciaInitChar('D',2,ROW_2);
	ciaInitChar('A',1,ROW_2);
	ciaInitChar(C64KEY_CURRIGHT,0,ROW_2);

	ciaInitChar(C64KEY_BACK,7,ROW_1); 
	ciaInitChar('*',6,ROW_1);
	ciaInitChar('P',5,ROW_1);
	ciaInitChar('I',4,ROW_1);
	ciaInitChar('Y',3,ROW_1);
	ciaInitChar('R',2,ROW_1);
	ciaInitChar('W',1,ROW_1);
	ciaInitChar('\n',0,ROW_1);

	ciaInitChar('1',7,ROW_0); 
	ciaInitChar(C64KEY_POUND,6,ROW_1);
	ciaInitChar('+',5,ROW_0);
	ciaInitChar('9',4,ROW_0);
	ciaInitChar('7',3,ROW_0);
	ciaInitChar('5',2,ROW_0);
	ciaInitChar('3',1,ROW_0);
	ciaInitChar(C64KEY_DELETE,0,ROW_0);

	ciaInitChar(0xFF,0,0);

	for (i = 0; i < 8; i++) {
		g_cia1.kbd[i] = 0xff;
	}
	//
	// set default direction for ports. 
	//
	cia1_setreal(CIA1_DDRA,0xff);
	cia1_setreal(CIA1_DDRB,0x0);
	cia1_setreal(CIA1_PRB,0xff);
	g_cia1.lticks = 0;
}

void cia1_destroy() {
	
}

void cia1_update_timera() {

	unsigned long ticks;
	word tval = 0;
	word updateval= 0;
	byte cra = cia1_getreal(CIA1_CRA);

	//
	// is timer a enabled?
	//
	if ((cra & CIA_CR_TIMERSTART) == 0) {
		//
		// timer not running.
		//
		return;
	}
	//
	// count down ticks
	//
	if (cra & CIA_CRA_TIMERINPUT) {

		//
		// BUGBUG: Not implemented. Should count down on  CNT presses here. 
		//

	} else {
		ticks = sysclock_getticks();
		tval = ((word) cia1_getreal(CIA1_TAHI) << 8 ) | cia1_getreal(CIA1_TALO);
		updateval = tval - (ticks - g_cia1.lticks);
		cia1_setreal(CIA1_TAHI,updateval >> 8);
		cia1_setreal(CIA1_TALO,updateval & 0xFF); 
	}
	// 
	// check for underflow condition.
	//
	if (updateval > tval) { 
		//
		// set bit in ICS regiser. 
		//
		g_cia1.isr = CIA_FLAG_TAUIRQ; 
		//
		// check to see if (and how) to signal underflow on port b bit six. 
		//
		if (cra & CIA_CR_PORTBSELECT) {

			if (cra & CIA_CR_PORTBMODE) {
				//
				// BUGBUG Not Implemented
				//
			}
			else {
				//
				// BUGBUG Not Implemented
				//
			}
		}
		//
		// if runmode is one shot, turn timer off.  
		// 
		if (cra & CIA_CR_TIMERRUNMODE) {
			cia1_setreal(CIA1_CRA,cra & (~CIA_CR_TIMERSTART));
		}
		//
		// should we trigger an interrupt? 
		//
		if (cia1_getreal(CIA1_ICR) & CIA_FLAG_TAUIRQ) {
			//
			// set isr bit that we did do an interrupt and signal IRQ line on CPU. 
			//
			g_cia1.isr |=  CIA_FLAG_CIAIRQ;	
			cpu_irq();
		}
		//
		// reset to latch value. 
		//
		cia1_latchtoreal(CIA1_TAHI);
		cia1_latchtoreal(CIA1_TALO);
	}
}

void cia1_update_timerb() {

	unsigned long ticks = 0;
	word tval = 0;
	word updateval= 0;
	byte crb = cia1_getreal(CIA1_CRB);
	
	//
	// is timer a enabled?
	//
	if ((crb & CIA_CR_TIMERSTART) == 0) {
		//
		// timer not running.
		//
		return;
	}

	//
	// get timer ticks.
	//
	switch(crb & (CIA_CRB_TIMERINPUT1 | CIA_CRB_TIMERINPUT2)) {

		case 0b00000000: // sysclock ticks
			ticks = sysclock_getticks() - g_cia1.lticks;
		break;
		case 0b00100000: // CNT pin
			// BUGBUG: Not implemented
		break;
		case 0b01000000: // TAU undeflow
			ticks = (g_cia1.isr & CIA_FLAG_TAUIRQ) ? 1 : 0;
		break;
		case 0b01100000: // TAU underflow + CNT pin
			// BUGBUG: Not implemented
		break;
		default: break;
	}

	tval = ((word) cia1_getreal(CIA1_TBHI) << 8 ) | cia1_getreal(CIA1_TBLO);
	updateval = tval - ticks;

	cia1_setreal(CIA1_TBHI,updateval >> 8);
	cia1_setreal(CIA1_TBLO,updateval & 0xFF);

	// 
	// check for underflow condition.
	//
	if (updateval > tval) { 

		//
		// set bit in ICS regiser. 
		//
		g_cia1.isr |= CIA_FLAG_TBUIRQ; 
		
		//
		// check to see if (and how) to signal underflow on port b bit six. 
		//
		if (crb & CIA_CR_PORTBSELECT) {

			if (crb & CIA_CR_PORTBMODE) {
				//
				// BUGBUG Not Implemented
				//
			}
			else {
				//
				// BUGBUG Not Implemented
				//
			}
		}
		//
		// if runmode is one shot, turn timer off.  
		// 
		if (crb & CIA_CR_TIMERRUNMODE) {
			cia1_setreal(CIA1_CRB,crb & (~CIA_CR_TIMERSTART));
		}
		//
		// should we trigger an interrupt? 
		//
		if (cia1_getreal(CIA1_ICR) & CIA_FLAG_TBUIRQ) {
			//
			// set isr bit that we did do an interrupt and signal IRQ line on CPU. 
			//
			g_cia1.isr |=  CIA_FLAG_CIAIRQ;	
			cpu_irq();
		}
		//
		// reset to latch value. 
		//
		cia1_latchtoreal(CIA1_TBHI);
		cia1_latchtoreal(CIA1_TBLO);
	}
}

void cia1_update_timeofday() {}

void cia1_update_keyboard() {}

void cia1_update() {

	//
	// order here is important. Timerb can count timera underflows so needs to come 
	// second.
	//
	cia1_update_timera();
	cia1_update_timerb();
	cia1_update_timeofday();
	cia1_update_keyboard();

	g_cia1.lticks = sysclock_getticks ();
}

void cia1_keyup(byte ch) {
	g_cia1.kbd[g_ciaKeyboardTable[ch].column] |= g_ciaKeyboardTable[ch].row;
}
void cia1_keydown(byte ch) {

	if (ch == C64KEY_RESTORE) {
		//
		// bugbug cia2 should handle this.
		//
		cpu_nmi();
	}
	g_cia1.kbd[g_ciaKeyboardTable[ch].column] &= (~g_ciaKeyboardTable[ch].row);
}

