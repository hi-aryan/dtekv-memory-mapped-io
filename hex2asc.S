  # hexmain.S
  # Written 2015-09-04 by F Lundevall
  # Copyright abandonded - this file is in the public domain.

	.text
	.globl hex2asc

hex2asc:
	li	a0, 13		# test number from 0 to 15. (it loops back if value is greater than 15)
	
	addi    sp,sp,-4
	sw      ra,0(sp)
	
	jal	hexasc		# call hexasc and return ascii symbols
	
	li	a7, 11	# write a0 to stdout
	ecall

	lw      ra,0(sp)
	addi    sp,sp,4
	jr      ra	

hexasc: 
	# PUSHs (?)

	# only check 4 least significant bits of the argument register a0
	# 0-9 to ascii, starting from 0x30
	# 10-15 to ascii, starting from 0x41
	
	# COMMENT! if a0 is greater than 15 ( ...1111 in binary) it starts over from 0-15 cause of the bitwise and
	andi a0, a0, 0xF # can we also do '15'?
	
	# this like was necessary to not get error... why?!
	addi t0, t0, 10
	# this below causes the jump: if a0 is >= t0 then jump to character
	bge a0, t0, character # replace 10 with 0xA?
	addi a0, a0, 48 # 0x3
	
	# POPs
	
	j jump

character: 
	addi a0, a0, 55 # 0x41
jump: 
	andi a0, a0, 0x7F
	
	jr ra
