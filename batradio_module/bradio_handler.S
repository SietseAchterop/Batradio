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

batradio_handler:
	subs	pc, lr, #4


	stmdb sp!, {r6-r7}

	// TIMCINT = 0
	mov r6, #0
	str r6, [r9, #0x40C]
	
//	bl c_fiq_handler
	mov r7, #0x2000		// 1 << CNVST
	add r10, r10, #1
	ands r6, r10, #0x0001	// voor snelheid van knipperen
	bne off
	str r7, [r8, #40]	// led on
	ldmia sp!, {r6-r7}	
	subs	pc, lr, #4
off:	str r3, [r8, #28]	// led off
	/* return from FIQ */
	ldmia sp!, {r6-r7}	
	subs	pc, lr, #4
	.align
batradio_handler_end:
