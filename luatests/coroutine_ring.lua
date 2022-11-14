-- The Computer Language Benchmarks Game
-- http://shootout.alioth.debian.org/
-- contributed by Sam Roberts
-- reviewed by Bruno Massa

local n         = 200

-- fixed size pool
local poolsize  = 47
local threads   = {}

-- cache these to avoid global environment lookups
local create    = coroutine.create
local resume    = coroutine.resume
local yield     = coroutine.yield

local id        = 1
local token     = 0
local ok

local body = function(token)
  while true do
    print('body', token)
    token = yield(token + 1)
  end
end

-- create all threads
for id = 1, poolsize do
  threads[id] = create(body)
end

-- send the token
repeat
  if id == poolsize then
    id = 1
  else
    id = id + 1
  end
  ok, token = resume(threads[id], token)
  print('main', ok, token)
until token == n

io.write(id, "\n")

