function f1(a) return a > 1 end
function f2(a) return a < 1 end
function f3(a) return a >= 1 end
function f4(a) return a <= 1 end
function f5(a) return 1 > a end
function f6(a) return 1 < a end
function f7(a) return 1 >= a end
function f8(a) return 1 <= a end
function f9(a) return (a+1)>(2+3) end
function f10(a) return (a+1)<(2+3) end
function f11(a) return (a+1)>=(2+3) end
function f12(a) return (a+1)<=(2+3) end
function f13(a) return (2+3)>(a+1) end
function f14(a) return (2+3)<(a+1) end
function f15(a) return (2+3)>=(a+1) end
function f16(a) return (2+3)<=(a+1) end
function f17(a) if (a > 1) then return 'x' else return 'y' end end
function f18(a) if (a < 1) then return 'x' else return 'y' end end
function f19(a) if (a >= 1) then return 'x' else return 'y' end end
function f20(a) if (a <= 1) then return 'x' else return 'y' end end
function f21(a) if (1 > a) then return 'x' else return 'y' end end
function f22(a) if (1 < a) then return 'x' else return 'y' end end
function f23(a) if (1 >= a) then return 'x' else return 'y' end end
function f24(a) if (1 <= a) then return 'x' else return 'y' end end
function f25(a) if ((a+1) > (2+3)) then return 'x' else return 'y' end end
function f26(a) if ((a+1) < (2+3)) then return 'x' else return 'y' end end
function f27(a) if ((a+1) >= (2+3)) then return 'x' else return 'y' end end
function f28(a) if ((a+1) <= (2+3)) then return 'x' else return 'y' end end
function f29(a) if ((2+3) > (a+1)) then return 'x' else return 'y' end end
function f30(a) if ((2+3) < (a+1)) then return 'x' else return 'y' end end
function f31(a) if ((2+3) >= (a+1)) then return 'x' else return 'y' end end
function f32(a) if ((2+3) <= (a+1)) then return 'x' else return 'y' end end
function f33(a) return not(a > 1) end
function f34(a) return not(a < 1) end
function f35(a) return not(a >= 1) end
function f36(a) return not(a <= 1) end
function f37(a) return not(1 > a) end
function f38(a) return not(1 < a) end
function f39(a) return not(1 >= a) end
function f40(a) return not(1 <= a) end
function f41(a) return not((a+1)>(2+3)) end
function f42(a) return not((a+1)<(2+3)) end
function f43(a) return not((a+1)>=(2+3)) end
function f44(a) return not((a+1)<=(2+3)) end
function f45(a) return not((2+3)>(a+1)) end
function f46(a) return not((2+3)<(a+1)) end
function f47(a) return not((2+3)>=(a+1)) end
function f48(a) return not((2+3)<=(a+1)) end
function f49(a) if (not(a > 1)) then return 'x' else return 'y' end end
function f50(a) if (not(a < 1)) then return 'x' else return 'y' end end
function f51(a) if (not(a >= 1)) then return 'x' else return 'y' end end
function f52(a) if (not(a <= 1)) then return 'x' else return 'y' end end
function f53(a) if (not(1 > a)) then return 'x' else return 'y' end end
function f54(a) if (not(1 < a)) then return 'x' else return 'y' end end
function f55(a) if (not(1 >= a)) then return 'x' else return 'y' end end
function f56(a) if (not(1 <= a)) then return 'x' else return 'y' end end
function f57(a) if (not((a+1) > (2+3))) then return 'x' else return 'y' end end
function f58(a) if (not((a+1) < (2+3))) then return 'x' else return 'y' end end
function f59(a) if (not((a+1) >= (2+3))) then return 'x' else return 'y' end end
function f60(a) if (not((a+1) <= (2+3))) then return 'x' else return 'y' end end
function f61(a) if (not((2+3) > (a+1))) then return 'x' else return 'y' end end
function f62(a) if (not((2+3) < (a+1))) then return 'x' else return 'y' end end
function f63(a) if (not((2+3) >= (a+1))) then return 'x' else return 'y' end end
function f64(a) if (not((2+3) <= (a+1))) then return 'x' else return 'y' end end

function test_fn(name, fn)
	print("testing:", name)
	print(fn(-1))
	print(fn(0))
	print(fn(1))
	print(fn(2))
	print(fn(3))
	print(fn(4))
	print(fn(5))
	print(fn(6))
	print('pcall:',(pcall(function() fn({}) end)))
end

test_fn("f1", f1)
test_fn("f2", f2)
test_fn("f3", f3)
test_fn("f4", f4)
test_fn("f5", f5)
test_fn("f6", f6)
test_fn("f7", f7)
test_fn("f8", f8)
test_fn("f9", f9)
test_fn("f10", f10)
test_fn("f11", f11)
test_fn("f12", f12)
test_fn("f13", f13)
test_fn("f14", f14)
test_fn("f15", f15)
test_fn("f16", f16)
test_fn("f17", f17)
test_fn("f18", f18)
test_fn("f19", f19)
test_fn("f20", f20)
test_fn("f21", f21)
test_fn("f22", f22)
test_fn("f23", f23)
test_fn("f24", f24)
test_fn("f25", f25)
test_fn("f26", f26)
test_fn("f27", f27)
test_fn("f28", f28)
test_fn("f29", f29)
test_fn("f30", f30)
test_fn("f31", f31)
test_fn("f32", f32)
test_fn("f33", f33)
test_fn("f34", f34)
test_fn("f35", f35)
test_fn("f36", f36)
test_fn("f37", f37)
test_fn("f38", f38)
test_fn("f39", f39)
test_fn("f40", f40)
test_fn("f41", f41)
test_fn("f42", f42)
test_fn("f43", f43)
test_fn("f44", f44)
test_fn("f45", f45)
test_fn("f46", f46)
test_fn("f47", f47)
test_fn("f48", f48)
test_fn("f49", f49)
test_fn("f50", f50)
test_fn("f51", f51)
test_fn("f52", f52)
test_fn("f53", f53)
test_fn("f54", f54)
test_fn("f55", f55)
test_fn("f56", f56)
test_fn("f57", f57)
test_fn("f58", f58)
test_fn("f59", f59)
test_fn("f60", f60)
test_fn("f61", f61)
test_fn("f62", f62)
test_fn("f63", f63)
test_fn("f64", f64)

