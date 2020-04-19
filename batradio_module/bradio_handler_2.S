	.text
	.global batradio_handler
	.global batradio_handler_end

batradio_handler:
	str	r8, [r9, #0x40C]	// TIMCINT = 0
	add	r10, r10, #1
	subs	pc, lr, #4
	.align
batradio_handler_end:
