print('sanity test')

-- some sanity tests
local x = "abcde"
print(#x)
x = { a = 1, b = 2, [1] = 3, [2] = 4, [3] = 5 }
print(#x)
x[4] = 6
print(#x)
x[6] = 7
local tmp = #x
if tmp ~= 4 and tmp ~= 6 then
	print("unexpected length", tmp)
end
x[-1] = 8
tmp = #x
if tmp ~= 4 and tmp ~= 6 then
	print("unexpected length", tmp)
end
x[5] = 9
print(#x)

print('stress test')


-- test the case without sparse map
for len = 1,20 do
	for brk = 0,20 do
		local t = {}
		for i = 1,len do
			if i ~= brk then
				t[i] = i
			end
		end
		local lt = #t
		if lt > 0 then
			if t[lt] == nil then
				print("Fail 1! t[lt] == nil", len, brk, lt)
			end
			if t[lt + 1] ~= nil then
				print("Fail 1! t[lt + 1] ~= nil", len, brk, lt)
			end
		else
			if lt ~= 0 or t[1] ~= nil then
				print("Fail 1! t[1] == nil", len, brk, lt)
			end
		end
		
		t[brk] = 233
		lt = #t
		if lt > 0 then
			if t[lt] == nil then
				print("Fail 2! t[lt] == nil", len, brk, lt)
			end
			if t[lt + 1] ~= nil then
				print("Fail 2! t[lt + 1] ~= nil", len, brk, lt)
			end
		else
			if lt ~= 0 or t[1] ~= nil then
				print("Fail 2! t[1] == nil", len, brk, lt)
			end
		end
		
		t[brk] = nil
		lt = #t
		if lt > 0 then
			if t[lt] == nil then
				print("Fail 3! t[lt] == nil", len, brk, lt)
			end
			if t[lt + 1] ~= nil then
				print("Fail 3! t[lt + 1] ~= nil", len, brk, lt)
			end
		else
			if lt ~= 0 or t[1] ~= nil then
				print("Fail 3! t[1] == nil", len, brk, lt)
			end
		end
	end
end

-- test the case with sparse map
for ty = 0,3 do
	for len = 1,20 do
		for brk1 = 0,20 do
			for brk2 = brk1,20 do
				local t = {}
				for i = 1,brk1 do
					t[i] = i
				end
				local lt = #t
				if lt ~= brk1 then
					print("Fail! unexpected len", len, brk1, brk2, lt)
				end
				if ty == 0 then
					t[1000000] = 123
				elseif ty == 1 then
					t[-1] = 123
				elseif ty == 2 then
					t[123.4] = 123
				else
					t[0] = 123
				end
				for i = brk1 + 1,20 do
					if i ~= brk2 then
						t[i] = i
					end
				end
				lt = #t
				if lt > 0 then
					if t[lt] == nil then
						print("Fail 1! t[lt] == nil", len, brk1, brk2, lt)
					end
					if t[lt + 1] ~= nil then
						print("Fail 1! t[lt + 1] ~= nil", len, brk1, brk2, lt)
					end
				else
					if lt ~= 0 or t[1] ~= nil then
						print("Fail 1! t[1] == nil", len, brk1, brk2, lt)
					end
				end
				
				t[brk2] = 233
				lt = #t
				if lt > 0 then
					if t[lt] == nil then
						print("Fail 2! t[lt] == nil", len, brk1, brk2, lt)
					end
					if t[lt + 1] ~= nil then
						print("Fail 2! t[lt + 1] ~= nil", len, brk1, brk2, lt)
					end
				else
					if lt ~= 0 or t[1] ~= nil then
						print("Fail 2! t[1] == nil", len, brk1, brk2, lt)
					end
				end
				
				t[brk2] = nil
				lt = #t
				if lt > 0 then
					if t[lt] == nil then
						print("Fail 3! t[lt] == nil", len, brk1, brk2, lt)
					end
					if t[lt + 1] ~= nil then
						print("Fail 3! t[lt + 1] ~= nil", len, brk1, brk2, lt)
					end
				else
					if lt ~= 0 or t[1] ~= nil then
						print("Fail 3! t[1] == nil", len, brk1, brk2, lt)
					end
				end
			end
		end
	end
end

print('test end')

