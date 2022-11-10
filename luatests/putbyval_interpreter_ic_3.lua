function create_rng(seed)
  local Rm, Rj = {}, 1
  for i=1,17 do Rm[i] = 0 end
  for i=17,1,-1 do
    seed = (seed*9069) % (2^31)
    Rm[i] = seed
  end
  return function()
      local j, m = Rj, Rm
      local h = j - 5
      if h < 1 then h = h + 17 end
      local k = m[h] - m[j]
      if k < 0 then k = k + 2147483647 end
      m[j] = k
      if j < 17 then Rj = j + 1 else Rj = 1 end
      return k
  end
end 

rand = create_rng(12345)

metamethod_invoked = false
function newindex_metamethod(base, idx, newvalue)
	metamethod_invoked = true
	rawset(base, idx, newvalue)
end

newidx_metatable = { __newindex = newindex_metamethod } 

function test_put_by_imm_impl(o, gold, o_fn, key, newValue)
	metamethod_invoked = false
	o_fn(o, newValue)
	
	if (rawget(gold, key) == nil) then
		local expectMt = (rawget(gold, "testinternal_has_mt") ~= nil)
		if (metamethod_invoked ~= expectMt) then
			if (expectMt) then
				print("PutByImm expect metamethod called, actually not!")
			else
				print("PutByImm expect metamethod not called, actually called!")
			end
			return false
		end
	end
	
	rawset(gold, key, newValue)
	
	if (rawget(o, key) ~= newValue) then
		print("PutByImm did not put the key successfully!")
		return false
	end
	
	return true
end

function put_by_val_fn(o, key, newValue)
	o[key] = newValue
end

function test_put_by_val_impl(o, gold, key, newValue)
	metamethod_invoked = false
	put_by_val_fn(o, key, newValue)
	
	if (rawget(gold, key) == nil) then
		local expectMt = (rawget(gold, "testinternal_has_mt") ~= nil)
		if (metamethod_invoked ~= expectMt) then
			if (expectMt) then
				print("PutByVal expect metamethod called, actually not!")
			else
				print("PutByVal expect metamethod not called, actually called!")
			end
			return false
		end
	end
	
	rawset(gold, key, newValue)
	
	if (rawget(o, key) ~= newValue) then
		print("PutByVal did not put the key successfully!")
		return false
	end
	
	return true
end

function check_table_identical(o, gold)
	for k, v in pairs(o) do
		other = rawget(gold, k)
		if (other ~= v) then
			print("Table is not identical! key = ", k, "o = ", v, "other = ", other)
			return false
		end
	end
	for k, v in pairs(gold) do
		if (k ~= "testinternal_has_mt") then
			other = rawget(o, k)
			if (other ~= v) then
				print("Table is not identical! key = ", k, "o = ", other, "other = ", v)
				return false
			end
		end
	end
	return true
end

function putbyimm0(t, v) t[0] = v end
function putbyimm1(t, v) t[1] = v end
function putbyimm2(t, v) t[2] = v end
function putbyimm3(t, v) t[3] = v end
function putbyimm4(t, v) t[4] = v end
function putbyimm5(t, v) t[5] = v end
function putbyimm6(t, v) t[6] = v end
function putbyimm7(t, v) t[7] = v end
function putbyimm8(t, v) t[8] = v end
function putbyimm9(t, v) t[9] = v end
function putbyimm10(t, v) t[10] = v end
function putbyimm11(t, v) t[11] = v end
function putbyimm12(t, v) t[12] = v end
function putbyimm13(t, v) t[13] = v end
function putbyimm14(t, v) t[14] = v end
function putbyimm15(t, v) t[15] = v end
function putbyimm16(t, v) t[16] = v end
function putbyimm17(t, v) t[17] = v end
function putbyimm18(t, v) t[18] = v end
function putbyimm19(t, v) t[19] = v end
function putbyimm20(t, v) t[20] = v end
function putbyimm21(t, v) t[21] = v end
function putbyimm22(t, v) t[22] = v end
function putbyimm23(t, v) t[23] = v end
function putbyimm24(t, v) t[24] = v end
function putbyimm25(t, v) t[25] = v end
function putbyimm26(t, v) t[26] = v end
function putbyimm27(t, v) t[27] = v end
function putbyimm28(t, v) t[28] = v end
function putbyimm29(t, v) t[29] = v end
function putbyimm30(t, v) t[30] = v end
function putbyimm31(t, v) t[31] = v end
function putbyimm32(t, v) t[32] = v end
function putbyimm33(t, v) t[33] = v end
function putbyimm34(t, v) t[34] = v end
function putbyimm35(t, v) t[35] = v end
function putbyimm36(t, v) t[36] = v end
function putbyimm37(t, v) t[37] = v end
function putbyimm38(t, v) t[38] = v end
function putbyimm39(t, v) t[39] = v end
function putbyimm40(t, v) t[40] = v end
function putbyimm41(t, v) t[41] = v end
function putbyimm42(t, v) t[42] = v end
function putbyimm43(t, v) t[43] = v end
function putbyimm44(t, v) t[44] = v end
function putbyimm45(t, v) t[45] = v end
function putbyimm46(t, v) t[46] = v end
function putbyimm47(t, v) t[47] = v end
function putbyimm48(t, v) t[48] = v end
function putbyimm49(t, v) t[49] = v end
function putbyimm50(t, v) t[50] = v end

function get_put_by_imm_impl_fn(ord)
	local fnName = "putbyimm" .. ord
	local fn = _G[fnName]
	return fn
end

function get_random_put_by_imm_fn(maxOrd)
	if (maxOrd > 50) then
		maxOrd = 50
	end
	local idx = rand() % (maxOrd + 1)
	return idx, get_put_by_imm_impl_fn(idx)
end

function test_put_by_imm(o, gold, key, newValue)
	local putByImmFn = get_put_by_imm_impl_fn(key)
	return test_put_by_imm_impl(o, gold, putByImmFn, key, newValue)
end

function test_rand_put_by_imm(o, gold, key, newValue, maxOrd)
	local putByImmFn = get_random_put_by_imm_fn(maxOrd)
	return test_put_by_imm_impl(o, gold, putByImmFn, key, newValue)
end

function set_metatable(o, gold)
	if (rawget(gold, "testinternal_has_mt") ~= nil) then
		return
	end
	rawset(gold, "testinternal_has_mt", true)
	setmetatable(o, newidx_metatable)
end

function remove_metatable(o, gold)
	if (rawget(gold, "testinternal_has_mt") == nil) then
		return
	end
	rawset(gold, "testinternal_has_mt", nil)
	setmetatable(o, nil)
end

possible_init_object_list = {}
possible_init_object_list[1] = function() return {} end
possible_init_object_list[2] = function() return { aa = 123, s4 = 234 } end
possible_init_object_list[3] = function() return { bb = 23, cc = 34, dd = 45, ee = 56, ff = 67, gg = 78, hh = 89, ii = 90, jj = 91, kk = 92 } end
possible_init_object_list[4] = function() return { cc = 23, hh = 34, [1] = "a", [2] = 3 } end
possible_init_object_list[5] = function() return { oo = 13, kk = 24, [1] = 3 } end
possible_init_object_list[6] = function() return { jj = 25, ll = 64, [2] = 4 } end
possible_init_object_list[7] = function() return { tt = 25, [-1] = 5 } end
possible_init_object_list[8] = function() return { [1] = 1, [2] = 2, [3] = 3 } end
possible_init_object_list[9] = function() return { [2] = 1, [3] = 2, [4] = 3 } end
possible_init_object_list[10] = function() return { [3] = "aa", [4] = true, [5] = "cc" } end
possible_init_object_list[11] = function() return { [-1] = "aa" } end
possible_init_object_list[12] = function() return { [100000] = 234 } end
possible_init_object_list[13] = function() return { [0] = 1, [1] = 3, [2] = 5, [3] = 7, [4] = 9, [5] = 11, [6] = 13, [7] = 15, [8] = 17 } end
possible_init_object_list[14] = function() return { [0] = "a1", [1] = "a3", [2] = 5, [3] = 7, [4] = 9, [5] = 11, [6] = 13, [7] = 15, [8] = 17 } end

function get_random_init_object_creator_fn()
	local idx = rand() % 14 + 1
	return possible_init_object_list[idx]
end

--- test-specific config

function get_rand_put_by_val_key_property()
	local dice = rand() % 30
	if (dice == 0) then
		return false
	end
	if (dice == 1) then
		return true
	end
	if (dice < 10) then
		return "s" .. (rand() % 8)
	end
	return "s" .. (rand() % 1000)
end

function get_rand_put_by_val_key_integer()
	local dice = rand() % 100
	if (dice == 0) then
		return rand() % 100000 + 1
	end
	if (dice < 80) then	
		return rand() % 50 + 1
	end
	return rand() % 1000 + 1
end

function get_rand_put_by_val_key_double()
	local dice = rand() % 5
	if (dice == 0) then
		return 0
	end
	if (dice == 1) then
		local tmp1 = -1.0
		local tmp2 = 0.0
		return tmp1 * tmp2
	end
	if (dice ~= 2) then
		return rand() % 1000 * (-1)
	end
	return rand() / 1000000.0 - 1000
end

only_insert_number_values = false

function get_value_to_put()
	local dice = rand() % 6
	if (dice == 0) then
		return nil
	end
	if (dice < 3 and (not only_insert_number_values)) then
		return "v" .. (rand())
	end
	return rand()
end

do_not_insert_non_vector_index = false
num_repeats = 3

function execute_one_putbyval_test_op(o, gold)
	local dice = rand() % 30
	local key
	if (dice == 0 and (not do_not_insert_non_vector_index)) then
		key = get_rand_put_by_val_key_double()
	elseif (dice < 15) then
		key = get_rand_put_by_val_key_property()
	else
		key = get_rand_put_by_val_key_integer()
	end
	for iter = 1, num_repeats do
		local newValue = get_value_to_put()
		if (not test_put_by_val_impl(o, gold, key, newValue)) then
			return false
		end
	end
	return true
end

function exeucte_one_test_op(o, gold)
	local dice = rand() % 13
	if (dice < 10) then
		return execute_one_putbyval_test_op(o, gold)
	end
	
	if (dice == 10) then
		local idx = rand() % (#gold) + 1
		if (idx <= 50) then
			for iter = 1, num_repeats do
				local newValue = get_value_to_put()
				if (not test_put_by_imm(o, gold, idx, newValue)) then
					return false
				end
			end
			return true
		end
	end
	
	if (dice == 11) then
		local idx = #gold + 1
		if (idx <= 50) then
			for iter = 1, num_repeats do
				local newValue = get_value_to_put()
				if (not test_put_by_imm(o, gold, idx, newValue)) then
					return false
				end
			end
			return true
		end
	end
	
	for iter = 1, num_repeats do
		local newValue = get_value_to_put()
		if (not test_put_by_imm(o, gold, rand() % 51, newValue)) then
			return false
		end
	end
	return true
end

local totalTestOpExecuted = 0

for numTests = 1, 20000 do
	local init_object_creator_fn = get_random_init_object_creator_fn()
	local o = init_object_creator_fn()
	local gold = init_object_creator_fn()
	
	if (rand() % 2 == 0) then
		set_metatable(o, gold)
	end
	
	if (rand() % 2 == 0) then
		only_insert_number_values = false
	else
		only_insert_number_values = true
	end
	
	if (rand() % 2 == 0) then
		do_not_insert_non_vector_index = false
	else
		do_not_insert_non_vector_index = true
	end
	
	local success = true
	for testOp = 1, 5 do
		if (not exeucte_one_test_op(o, gold)) then
			print('exeucte_one_test_op failed!')
			success = false
			break
		end
		if (not check_table_identical(o, gold)) then
			print('check_table_identical failed!')
			success = false
			break
		end
		totalTestOpExecuted = totalTestOpExecuted + 1
	end
	
	if (not success) then
		break
	end
end

print('Total test ops executed = ', totalTestOpExecuted)

