def C_PROGRAM(x):
  return square(x, 3)
def square(x,y):
	y = x * x
	print y
	return y
def square2(x::int)::int:
	print x * x
def square3(x::int):
	native "C_PROGRAM"

print "TEST"
print square2(2)
print square(3, 2)
print square3(4)

