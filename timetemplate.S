# timetemplate.S
# Written 2015 by F Lundevall
# Ported 2024/06 by W Szczerek (from MIPS to RISC-V)
# Copyright abandonded - this file is in the public domain.

# DTEK VERION!!

#############################################################
# Choose the macro syntax for RARS or DTEK-V board.         #
# The syntax for RARS is probably due to its MIPS heritage. #
#############################################################
###################
# Macros for RARS #
###################
#.macro	PUSH (%reg)
#	addi	sp,sp,-4
#	sw	%reg,0(sp) 
#.end_macro

#.macro	POP (%reg)
#	lw	%reg,0(sp)
#	addi	sp,sp,4
#.end_macro
###################
# Macros for DTEK #
###################
.macro	PUSH reg
	addi sp,sp,-4
	sw	\reg,0(sp) 
.endm

.macro	POP reg
	lw	\reg,0(sp)
	addi	sp,sp,4
.endm
#############################################################

.data
.align 2
mytime:	.word 	0x5957 # saving the hexadecimal into memory, and "mytime" points to that memory address
timstr:	.asciz 	"text more text lots of text\0"

.text
.globl timetemplate, tick, time2string, display_string, delay

# Function for displaying a string with a newline at the end	
display_string:	
	li a7,4
	ecall
	li a0, 10
	li a7,11
	ecall
	jr ra
	
timetemplate:
	la	a0, timstr
	jal     display_string
	
	# wait a little
	li	a0, 2		# ms
	jal	delay
	
	# call tick
	la 	a0, mytime
	jal	tick
	
	# call your function time2string
	la	a0, timstr
	la	t0, mytime
	lw	a1, 0(t0)
	jal	time2string
	
	# go back and do it all again
	j	timetemplate

	
# tick: update time pointed to by $a0
tick:	lw	t0, 0(a0)	# get time
	addi	t0, t0, 1	# increase
	andi	t1, t0, 0xf	# check lowest digit
	sltiu	t2, t1, 0xa	# COMMENT! if digit < a, okay
	bnez	t2, tiend
	addi	t0, t0, 0x6	# adjust lowest digit
	
	andi	t1, t0, 0xf0	# check next digit
	sltiu	t2, t1, 0x60	# COMMENT! if digit < 6, okay
	bnez	t2, tiend
	addi	t0, t0, 0xa0	# adjust digit
	
	li	t3, 0xF
	slli	t3, t3, 0x8
	and	t1, t0, t3	# check minute digit
	addi	t3, x0, 0xA
	slli	t3, t3, 0x8
	slt	t2, t1, t3	# COMMENT! if digit < a, okay
	bnez	t2, tiend
	addi	t0, t0, 0x600	# adjust digit - this one's okay, it's lower than 0x7FF 
	
	li	t3, 0xF
	slli	t3, t3, 0xC
	and	t1, t0, t3	# check last digit
	addi	t3, x0, 0x6
	slli	t3, t3, 0xC
	slt	t2, t1, t3	# COMMENT! if digit < 6, okay
	bnez	t2, tiend
	
	li	t3, 0xA
	slli	t3, t3, 0xC
	add	t0, t0, t3	# adjust last digit
tiend:	sw	t0,0(a0)	# save updated result
	jr	ra		# return	

#########################################################
# Place for your functions: time2string, hex2asc, delay.#
#########################################################

time2string: 
	# PUSH before calling hex2asc (also s-registers if used!)
	PUSH ra
	PUSH s0
	PUSH s1
	PUSH s2
	PUSH s3

	addi s0, a0, 0

	# andi a1, a1, 0xFFFF # a1 holds the *value* of the word at 'mytime' address (initially 5957 in hexa, so 16 bits)
	li   t0, 0xFFFF
	and  a1, a1, t0
	
	
	# position of first number in a1 (5 at start)	
	addi s1, a1, 0 # store the original value of a1 (5957) in s0
	
	#andi s1, s1, 0xF000 # isolate the top 4 bits
	li   t0, 0xF000
	and  s1, s1, t0
	
	srli a0, s1, 12 # move from position 4 to position 1 (12 bits to the right) to hexa2asc can understand which digit it is
	# store value in a0 cause it's an argument for hexa2asc
	jal hexasc
	sb a0, 0(s0) # a0 will be the returned value from hexa2asc function
	
	
	# position of 2nd number
	addi s1, a1, 0
	
	#andi s1, s1, 0x0F00
	li   t0, 0x0F00
	and  s1, s1, t0
	
	srli a0, s1, 8
	jal hexasc
	sb a0, 1(s0) # 1 for 1 byte??
	
	
	# colon
	# addi t0, zero, 0x3A
	li t0, 0x3A
	sb t0, 2(s0)
	#addi a0, zero, 0xA
	#jal hexasc
	#sb a0, 2(s0)
	
	
	addi s1, a1, 0
	andi s1, s1, 0x00F0
	srli a0, s1, 4
	
	jal hexasc
	sb a0, 3(s0)
	
	
	addi s1, a1, 0
	andi a0, s1, 0x000F
	jal hexasc
	sb a0, 4(s0)
	
	
	# null
	#addi t0, zero, 0x00
	li t0, 0x00
	sb t0, 5(s0) # store null at 6th position
			
	# POP before returning
	POP s3
	POP s2
	POP s1
	POP s0
	POP ra
	jr ra
	
	
hexasc: 
	# only check 4 least significant bits of the argument register a0
	

	# COMMENT! if a0 is greater than 10 ( ...1010 in binary) it starts over from 0-10 cause of the bitwise and
	# isn't this already checked for before calling the function?!
#	andi a0, a0, 0xA # 0xA = 10
	
	addi a0, a0, 48 # 0x3
	andi a0, a0, 0x7F
	
	jr ra
	
delay: 
	# PUSH convention!
	# a0 is 2 (ms)
	li t0, 0
	li t1, 5000000
	
while:
	ble a0, t0, whiledone
	# bla bla
	li t2, 0 # variable i
	addi a0, a0, -1
	for: 
		bge t2, t1, fordone
		addi t2, t2, 1
		j for
		
	fordone: 	

	j while
	
whiledone: 
	# POP convention!
	jr ra
