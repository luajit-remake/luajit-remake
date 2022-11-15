print('-- coroutine.running --')
print(coroutine.running())

coro = coroutine.create(function() print(coroutine.running() == coro, coroutine.status(coroutine.running())) return end)
print(coroutine.resume(coro))
print(coroutine.status(coro))

print('-- return value passing (coroutine.create) --')
test_ret_passing_1 = function (c)
	r1 = coroutine.resume(c)
	print(r1)
end

test_ret_passing_2 = function(c)
	r1, r2 = coroutine.resume(c)
	print(r1, r2)
end

test_ret_passing_3 = function (c)
	r1, r2, r3 = coroutine.resume(c)
	print(r1, r2, r3)
end

test_ret_passing_4 = function (c)
	r1, r2, r3, r4 = coroutine.resume(c)
	print(r1, r2, r3, r4)
end

coro0 = function() return coroutine.create(function() return end) end
coro1 = function() return coroutine.create(function() return 11 end) end
coro2 = function() return coroutine.create(function() return 12,13 end) end
coro3 = function() return coroutine.create(function() return 14,15,16 end) end 
coro4 = function() return coroutine.create(function() return 17,18,19,20 end) end

test_ret_passing_1(coro0())
test_ret_passing_1(coro1())
test_ret_passing_1(coro2())
test_ret_passing_1(coro3())
test_ret_passing_1(coro4())

test_ret_passing_2(coro0())
test_ret_passing_2(coro1())
test_ret_passing_2(coro2())
test_ret_passing_2(coro3())
test_ret_passing_2(coro4())

test_ret_passing_3(coro0())
test_ret_passing_3(coro1())
test_ret_passing_3(coro2())
test_ret_passing_3(coro3())
test_ret_passing_3(coro4())

test_ret_passing_4(coro0())
test_ret_passing_4(coro1())
test_ret_passing_4(coro2())
test_ret_passing_4(coro3())
test_ret_passing_4(coro4())

print('-- return value passing (coroutine.wrap) --')
test_ret_passing_1 = function (c)
	r1 = c()
	print(r1)
end

test_ret_passing_2 = function (c)
	r1, r2 = c()
	print(r1, r2)
end

test_ret_passing_3 = function (c)
	r1, r2, r3 = c()
	print(r1, r2, r3)
end

test_ret_passing_4 = function (c)
	r1, r2, r3, r4 = c()
	print(r1, r2, r3, r4)
end

coro0 = function() return coroutine.wrap(function() return end) end
coro1 = function() return coroutine.wrap(function() return 1 end) end
coro2 = function() return coroutine.wrap(function() return 2,3 end) end 
coro3 = function() return coroutine.wrap(function() return 4,5,6 end) end
coro4 = function() return coroutine.wrap(function() return 7,8,9,10 end) end

test_ret_passing_1(coro0())
test_ret_passing_1(coro1())
test_ret_passing_1(coro2())
test_ret_passing_1(coro3())
test_ret_passing_1(coro4())

test_ret_passing_2(coro0())
test_ret_passing_2(coro1())
test_ret_passing_2(coro2())
test_ret_passing_2(coro3())
test_ret_passing_2(coro4())

test_ret_passing_3(coro0())
test_ret_passing_3(coro1())
test_ret_passing_3(coro2())
test_ret_passing_3(coro3())
test_ret_passing_3(coro4())

test_ret_passing_4(coro0())
test_ret_passing_4(coro1())
test_ret_passing_4(coro2())
test_ret_passing_4(coro3())
test_ret_passing_4(coro4())

print('-- argument passing (coroutine.create) --')
coro1 = function() return coroutine.create(function(a) print('arg:',a) end) end
coro2 = function() return coroutine.create(function(a,b) print('arg:',a,b) end) end
coro3 = function() return coroutine.create(function(a,b,c) print('arg:',a,b,c) end) end 
coro4 = function() return coroutine.create(function(a,b,c,d) print('arg:',a,b,c,d) end) end

test_arg_passing_0 = function (c)
	print(coroutine.resume(c))
end
test_arg_passing_1 = function (c)
	print(coroutine.resume(c, 10000))
end
test_arg_passing_2 = function (c)
	print(coroutine.resume(c, 20000, 20001))
end
test_arg_passing_3 = function (c)
	print(coroutine.resume(c, 30000, 30001, 30002))
end
test_arg_passing_4 = function (c)
	print(coroutine.resume(c, 40000, 40001, 40002, 40003))
end

test_arg_passing_0(coro1())
test_arg_passing_0(coro2())
test_arg_passing_0(coro3())
test_arg_passing_0(coro4())

test_arg_passing_1(coro1())
test_arg_passing_1(coro2())
test_arg_passing_1(coro3())
test_arg_passing_1(coro4())

test_arg_passing_2(coro1())
test_arg_passing_2(coro2())
test_arg_passing_2(coro3())
test_arg_passing_2(coro4())

test_arg_passing_3(coro1())
test_arg_passing_3(coro2())
test_arg_passing_3(coro3())
test_arg_passing_3(coro4())

test_arg_passing_4(coro1())
test_arg_passing_4(coro2())
test_arg_passing_4(coro3())
test_arg_passing_4(coro4())

print('-- argument passing (coroutine.wrap) --')
coro1 = function() return coroutine.wrap(function(a) print('arg:',a) end) end
coro2 = function() return coroutine.wrap(function(a,b) print('arg:',a,b) end) end
coro3 = function() return coroutine.wrap(function(a,b,c) print('arg:',a,b,c) end) end 
coro4 = function() return coroutine.wrap(function(a,b,c,d) print('arg:',a,b,c,d) end) end

test_arg_passing_0 = function (c)
	print(c())
end
test_arg_passing_1 = function (c)
	print(c(10))
end
test_arg_passing_2 = function (c)
	print(c(20, 21))
end
test_arg_passing_3 = function (c)
	print(c(30, 31, 32))
end
test_arg_passing_4 = function (c)
	print(c(40, 41, 42, 43))
end

test_arg_passing_0(coro1())
test_arg_passing_0(coro2())
test_arg_passing_0(coro3())
test_arg_passing_0(coro4())

test_arg_passing_1(coro1())
test_arg_passing_1(coro2())
test_arg_passing_1(coro3())
test_arg_passing_1(coro4())

test_arg_passing_2(coro1())
test_arg_passing_2(coro2())
test_arg_passing_2(coro3())
test_arg_passing_2(coro4())

test_arg_passing_3(coro1())
test_arg_passing_3(coro2())
test_arg_passing_3(coro3())
test_arg_passing_3(coro4())

test_arg_passing_4(coro1())
test_arg_passing_4(coro2())
test_arg_passing_4(coro3())
test_arg_passing_4(coro4())

print('-- return value passing (coroutine.create + coroutine.yield) --')
coro0 = function() return coroutine.create(function() coroutine.yield(); return end) end
coro1 = function() return coroutine.create(function() coroutine.yield(1000); return 2000 end) end
coro2 = function() return coroutine.create(function() coroutine.yield(3000,3001); return 4000,4001 end) end
coro3 = function() return coroutine.create(function() coroutine.yield(5000,5001,5002); return 6000,6001,6002 end) end
coro4 = function() return coroutine.create(function() coroutine.yield(7000,7001,7002,7003); return 8000,8001,8002,8003 end) end

test_ret_passing_1 = function (c)
	r1 = coroutine.resume(c)
	print(coroutine.status(c), r1)
	r1 = coroutine.resume(c)
	print(coroutine.status(c), r1)
end

test_ret_passing_2 = function (c)
	r1,r2 = coroutine.resume(c)
	print(coroutine.status(c), r1,r2)
	r1,r2 = coroutine.resume(c)
	print(coroutine.status(c), r1,r2)
end

test_ret_passing_3 = function (c)
	r1,r2,r3 = coroutine.resume(c)
	print(coroutine.status(c), r1,r2,r3)
	r1,r2,r3 = coroutine.resume(c)
	print(coroutine.status(c), r1,r2,r3)
end

test_ret_passing_4 = function (c)
	r1,r2,r3,r4 = coroutine.resume(c)
	print(coroutine.status(c), r1,r2,r3,r4)
	r1,r2,r3,r4 = coroutine.resume(c)
	print(coroutine.status(c), r1,r2,r3,r4)
end

test_ret_passing_1(coro0())
test_ret_passing_1(coro1())
test_ret_passing_1(coro2())
test_ret_passing_1(coro3())
test_ret_passing_1(coro4())

test_ret_passing_2(coro0())
test_ret_passing_2(coro1())
test_ret_passing_2(coro2())
test_ret_passing_2(coro3())
test_ret_passing_2(coro4())

test_ret_passing_3(coro0())
test_ret_passing_3(coro1())
test_ret_passing_3(coro2())
test_ret_passing_3(coro3())
test_ret_passing_3(coro4())

test_ret_passing_4(coro0())
test_ret_passing_4(coro1())
test_ret_passing_4(coro2())
test_ret_passing_4(coro3())
test_ret_passing_4(coro4())

print('-- return value passing (coroutine.wrap + coroutine.yield) --')
coro0 = function() return coroutine.wrap(function() coroutine.yield(); return end) end
coro1 = function() return coroutine.wrap(function() coroutine.yield(100); return 200 end) end
coro2 = function() return coroutine.wrap(function() coroutine.yield(300,301); return 400,401 end) end
coro3 = function() return coroutine.wrap(function() coroutine.yield(500,501,502); return 600,601,602 end) end
coro4 = function() return coroutine.wrap(function() coroutine.yield(700,701,702,703); return 800,801,802,803 end) end

test_ret_passing_1 = function (c)
	r1 = c()
	print(r1)
	r1 = c()
	print(r1)
end

test_ret_passing_2 = function (c)
	r1,r2 = c()
	print(r1,r2)
	r1,r2 = c()
	print(r1,r2)
end

test_ret_passing_3 = function (c)
	r1,r2,r3 = c()
	print(r1,r2,r3)
	r1,r2,r3 = c()
	print(r1,r2,r3)
end

test_ret_passing_4 = function (c)
	r1,r2,r3,r4 = c()
	print(r1,r2,r3,r4)
	r1,r2,r3,r4 = c()
	print(r1,r2,r3,r4)
end

test_ret_passing_1(coro0())
test_ret_passing_1(coro1())
test_ret_passing_1(coro2())
test_ret_passing_1(coro3())
test_ret_passing_1(coro4())

test_ret_passing_2(coro0())
test_ret_passing_2(coro1())
test_ret_passing_2(coro2())
test_ret_passing_2(coro3())
test_ret_passing_2(coro4())

test_ret_passing_3(coro0())
test_ret_passing_3(coro1())
test_ret_passing_3(coro2())
test_ret_passing_3(coro3())
test_ret_passing_3(coro4())

test_ret_passing_4(coro0())
test_ret_passing_4(coro1())
test_ret_passing_4(coro2())
test_ret_passing_4(coro3())
test_ret_passing_4(coro4())

print('-- argument passing (coroutine.create + coroutine.yield) --')
coro1 = function() return coroutine.create(function(a) print('arg:',a) a=a==a; a = coroutine.yield() print('yield:',a) end) end
coro2 = function() return coroutine.create(function(a,b) print('arg:',a,b) a=a==a; b=b==b; a,b = coroutine.yield() print('yield:',a,b) end) end
coro3 = function() return coroutine.create(function(a,b,c) print('arg:',a,b,c) a=a==a; b=b==b; c=c==c; a,b,c = coroutine.yield() print('yield:',a,b,c) end) end
coro4 = function() return coroutine.create(function(a,b,c,d) print('arg:',a,b,c,d) a=a==a; b=b==b; c=c==c; d=d==d; a,b,c,d = coroutine.yield() print('yield:',a,b,c,d) end) end

test_arg_passing_0 = function (c)
	print(coroutine.resume(c))
	print(coroutine.resume(c))
end
test_arg_passing_1 = function (c)
	print(coroutine.resume(c, 50))
	print(coroutine.resume(c, 60))
end
test_arg_passing_2 = function (c)
	print(coroutine.resume(c, 40,41))
	print(coroutine.resume(c, 30,31))
end
test_arg_passing_3 = function (c)
	print(coroutine.resume(c, 20,21,22))
	print(coroutine.resume(c, 10,11,12))
end
test_arg_passing_4 = function (c)
	print(coroutine.resume(c, 0,1,2,3))
	print(coroutine.resume(c, 100,101,102,103))
end

test_arg_passing_0(coro1())
test_arg_passing_0(coro2())
test_arg_passing_0(coro3())
test_arg_passing_0(coro4())

test_arg_passing_1(coro1())
test_arg_passing_1(coro2())
test_arg_passing_1(coro3())
test_arg_passing_1(coro4())

test_arg_passing_2(coro1())
test_arg_passing_2(coro2())
test_arg_passing_2(coro3())
test_arg_passing_2(coro4())

test_arg_passing_3(coro1())
test_arg_passing_3(coro2())
test_arg_passing_3(coro3())
test_arg_passing_3(coro4())

test_arg_passing_4(coro1())
test_arg_passing_4(coro2())
test_arg_passing_4(coro3())
test_arg_passing_4(coro4())

print('-- argument passing (coroutine.wrap + coroutine.yield) --')
coro1 = function() return coroutine.wrap(function(a) print('arg:',a) a=a==a; a = coroutine.yield() print('yield:',a) end) end
coro2 = function() return coroutine.wrap(function(a,b) print('arg:',a,b) a=a==a; b=b==b; a,b = coroutine.yield() print('yield:',a,b) end) end
coro3 = function() return coroutine.wrap(function(a,b,c) print('arg:',a,b,c) a=a==a; b=b==b; c=c==c; a,b,c = coroutine.yield() print('yield:',a,b,c) end) end
coro4 = function() return coroutine.wrap(function(a,b,c,d) print('arg:',a,b,c,d) a=a==a; b=b==b; c=c==c; d=d==d; a,b,c,d = coroutine.yield() print('yield:',a,b,c,d) end) end

test_arg_passing_0 = function (c)
	print(c())
	print(c())
end
test_arg_passing_1 = function (c)
	print(c(50))
	print(c(60))
end
test_arg_passing_2 = function (c)
	print(c(70,71))
	print(c(80,81))
end
test_arg_passing_3 = function (c)
	print(c(90,91,92))
	print(c(100,101,102))
end
test_arg_passing_4 = function (c)
	print(c(110,111,112,113))
	print(c(120,121,122,123))
end

test_arg_passing_0(coro1())
test_arg_passing_0(coro2())
test_arg_passing_0(coro3())
test_arg_passing_0(coro4())

test_arg_passing_1(coro1())
test_arg_passing_1(coro2())
test_arg_passing_1(coro3())
test_arg_passing_1(coro4())

test_arg_passing_2(coro1())
test_arg_passing_2(coro2())
test_arg_passing_2(coro3())
test_arg_passing_2(coro4())

test_arg_passing_3(coro1())
test_arg_passing_3(coro2())
test_arg_passing_3(coro3())
test_arg_passing_3(coro4())

test_arg_passing_4(coro1())
test_arg_passing_4(coro2())
test_arg_passing_4(coro3())
test_arg_passing_4(coro4())

print('-- yield sanity (coroutine.create) --')
coro0 = coroutine.create(function(x) 
	print('coro0 enter', x)
	while (x < 10) do
		x = coroutine.yield(x + 1); 
		print('coro0 yield', x)
	end
	return x
end)
coro1 = coroutine.create(function(x) 
	print('coro1 enter', x)
	while (x < 10) do
		x = coroutine.yield(x + 1); 
		print('coro1 yield', x)
	end
	return x
end)

print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, 0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, 5)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

ok, x0 = coroutine.resume(coro0, x0)
print(ok, x0)
ok, x1 = coroutine.resume(coro1, x1)
print(ok, x1)
print(coroutine.status(coro0), coroutine.status(coro1))

print('-- yield sanity (coroutine.wrap) --')
coro0 = coroutine.wrap(function(x) 
	print('coro0 enter', x)
	while (x < 10) do
		x = coroutine.yield(x + 1); 
		print('coro0 yield', x)
	end
	return x
end)
coro1 = coroutine.wrap(function(x) 
	print('coro1 enter', x)
	while (x < 10) do
		x = coroutine.yield(x + 1); 
		print('coro1 yield', x)
	end
	return x
end)

x0 = coro0(0)
print(x0)
x1 = coro1(5)
print(x1)

x0 = coro0(x0)
print(x0)
x1 = coro1(x1)
print(x1)

x0 = coro0(x0)
print(x0)
x1 = coro1(x1)
print(x1)

x0 = coro0(x0)
print(x0)
x1 = coro1(x1)
print(x1)

x0 = coro0(x0)
print(x0)
x1 = coro1(x1)
print(x1)

x0 = coro0(x0)
print(x0)
x1 = coro1(x1)
print(x1)

x0 = coro0(x0)
print(x0)

x0 = coro0(x0)
print(x0)

x0 = coro0(x0)
print(x0)

x0 = coro0(x0)
print(x0)

x0 = coro0(x0)
print(x0)

print((pcall(function() coro0() end)))
print((pcall(function() coro1() end)))

print('-- yield sanity (coroutine.create + call) --')
function f(x)
	print('In f', x)
	x = coroutine.yield(x+1)
	print('In f 2', x)
	x = coroutine.yield(x+1)
	print('Returning from f')
	return x
end

coro = coroutine.create(function(x) 
	print('In coro', x)
	x = f(x+1)
	print('In coro 2', x)
	x = f(x+1)
	print('Returning from coro')
	return x
end)
ok, x = coroutine.resume(coro, 1)
print(ok, x)
ok, x = coroutine.resume(coro, x+1)
print(ok, x)
ok, x = coroutine.resume(coro, x+1)
print(ok, x)
ok, x = coroutine.resume(coro, x+1)
print(ok, x)
ok, x = coroutine.resume(coro, x+1)
print(ok, x)
print(coroutine.status(coro))
ok, x = coroutine.resume(coro, x+1)
print(ok, x)
print(coroutine.status(coro))
ok, x = coroutine.resume(coro, x)
print(ok, x)
print(coroutine.status(coro))

print('-- yield sanity (coroutine.wrap + call) --')
function f(x)
	print('In f', x)
	x = coroutine.yield(x+1)
	print('In f 2', x)
	x = coroutine.yield(x+1)
	print('Returning from f')
	return x
end

coro = coroutine.wrap(function(x) 
	print('In coro', x)
	x = f(x+1)
	print('In coro 2', x)
	x = f(x+1)
	print('Returning from coro')
	return x
end)
x = coro(1)
print(x)
x = coro(x+1)
print(x)
x = coro(x+1)
print(x)
x = coro(x+1)
print(x)
x = coro(x+1)
print(x)
print((pcall(function() coro(x) end)))


