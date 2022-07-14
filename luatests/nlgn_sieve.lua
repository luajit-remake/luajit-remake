-- $Id: sieve.lua,v 1.9 2001/05/06 04:37:45 doug Exp $
-- http://www.bagley.org/~doug/shootout/
--
-- Roberto Ierusalimschy pointed out the for loop is much
-- faster for our purposes here than using a while loop.

local count = 0

function main(num, lim)
    local flags = {}
    for num=num,1,-1 do
	count = 0
	for i=1,lim do
          flags[i] = 1
        end
	for i=2,lim do
	    if flags[i] == 1 then
	        k = 0
	        for k=i+i, lim, i do
		    flags[k] = 0
		end
	        count = count + 1	
	    end
	end
    end
end

NUM = 10
lim = 8192 
print(NUM,lim)
count = 0
main(NUM, lim)
print("Count: ", count)
