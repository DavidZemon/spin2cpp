PUB main
  coginit(0, @entry, 0)
DAT
	org	0
entry

_foo
	neg	result1, #1
_foo_ret
	ret

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
