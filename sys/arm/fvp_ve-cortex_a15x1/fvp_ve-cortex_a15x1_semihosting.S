	@
	@ Function for C code to make semihosting calls:
	@
	.globl __semi_call
__semi_call:
	svc     0x123456
	mov pc, lr

