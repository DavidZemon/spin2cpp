PUB main
  coginit(0, @entry, 0)
DAT
	org	0
entry

_strlen
	neg	_var_r, #1
L__0001
	rdbyte	_var_c, arg1 wz
	add	arg1, #1
	add	_var_r, #1
 if_ne	jmp	#L__0001
	mov	result1, _var_r
_strlen_ret
	ret

_var_c
	long	0
_var_r
	long	0
arg1
	long	0
arg2
	long	0
arg3
	long	0
arg4
	long	0
result1
	long	0
	fit	496
