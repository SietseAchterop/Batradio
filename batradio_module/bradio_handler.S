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
	/* Save no banked registers */
	stmdb sp!, {r0-r7}
	
	bl c_fiq_handler

	/* return from FIQ */
	ldmia sp!, {r0-r7}	
	subs	pc, lr, #4
batradio_handler_end:
