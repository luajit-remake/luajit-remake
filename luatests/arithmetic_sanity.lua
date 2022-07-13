function test_add(x, y)
	print(x+y)
end

function test_sub(x, y)
	print(x-y)
end

function test_mul(x, y)
	print(x*y)
end

function test_div(x, y)
	print(x/y)
end

function test_mod(x, y)
	print(x%y)
end

function test_pow(x,y)
	print(x^y)
end

test_add(2, 3)
test_add(-2, -3)

test_sub(2, 3)
test_sub(-2, -3)

test_mul(2, 3)
test_mul(-2, -3)

test_div(2, 3)
test_div(-2, -3)

test_mod(3, 4)
test_mod(-3, -4)
test_mod(3, -4)
test_mod(-3, 4)

test_pow(2, 3)
test_pow(2, 0.5)
test_pow(2, -0.5)
test_pow(-2, -3)
test_pow(-2, -0.5)

