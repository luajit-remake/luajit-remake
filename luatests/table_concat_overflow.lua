-- reported by XmiliaH  
print((pcall(function() table.concat({[-2^63]="x", [0] = "y"}, "s", -2^63, 0) end)))

