	.text
	.global batradio_handler
	.global batradio_handler_end

batradio_handler:
	stmdb sp!, {r6-r7}
	mov 	r6, #0
	str	r6, [r9, #0x40C]	// TIMCINT = 0 // clear interrupt
	
	mov r7, #0x2000			// 1 << CNVST
	add r10, r10, #1
	ands r6, r10, #0x0001		// set toggle speed
	bne off
	str r7, [r8, #40]		// led on
	ldmia sp!, {r6-r7}	
	subs	pc, lr, #4
off:	str r7, [r8, #28]		// led off
	ldmia sp!, {r6-r7}	
	subs	pc, lr, #4
batradio_handler_end:
