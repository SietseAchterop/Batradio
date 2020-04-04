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
	push {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}
	bl c_fiq_handler
	pop  {r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,lr}

	/* return from FIQ */
	subs	pc, lr, #4
batradio_handler_end:
