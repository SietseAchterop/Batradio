/*   SPDX-License-Identifier: GPL-2.0
 *
 * Kernel module to be used with the batradio-hat on rpi
 *
 *      fiq handler
 *
*/


#define TT 0x8

	.text
	.global batradio_handler
	.global batradio_handler_end

/*
 base io devices vast of in een register?
    r8 :
    r9 :
    r10:

 C? register gebruiken om index in fig_buf te zijn?

I/O periphirals
7e000000   vc cpu bus
20000000   arm physical
f2000000   arm virtual

wat te gebruiken? printk gebruiken om uit te zoeken

Enable interrupt system timer 1
           static unsigned int *Enable_IRQs1 = (unsigned int*)0x2000B210;
           *Enable_IRQs1 = 1 << 1;
	dit is bit 1 van Interrupt Enable Register 1.  Waar is GPU IRQ table?

Clear interrupt na optreden
       schrijf 1 in bit 1 van CS van de system timer:

// System timer counter  (address)
#define SYST_CLO IOREG(0x20003004)
#define SYST_CHI IOREG(0x20003008)
	
	

bare metal voorbeeld van gebruik system timer
	https://github.com/boochow/bare_matal_rpi_zero/blob/master/timer-irq2/main.c

fiq is standaard arm? dus in arm docs kijken?

linux lijkt system timer 3 te gebruiken.
	0 en 2 worden door de gpu gebruikt?
	dan is 1 vrij?


*/

batradio_handler:
	/* Add x to the C? Register for 1/x MHz rate */
	mov	r11, #1
	lsl	r11, r11, #15
	str	r11, [r8, #TT]

	/* do our stuff here */


out:
	/* return from FIQ */
	subs	pc, lr, #4
batradio_handler_end:
