-- The Computer Language Benchmarks Game
-- http://benchmarksgame.alioth.debian.org/
-- contributed by Mike Pall
--
-- modified by Haoran
--
-- This version uses named property to address left child and 
-- right child (instead of using numbers), which better matches 
-- general guildlines of good programming practice
--
local function BottomUpTree(item, depth)
  local left, right = nil, nil
  if depth > 0 then
    local i = item + item
    depth = depth - 1
    left = BottomUpTree(i-1, depth)
    right = BottomUpTree(i, depth)
  end
  return { item = item, left = left, right = right }
end

local function ItemCheck(tree)
  if tree.left then
    return tree.item + ItemCheck(tree.left) - ItemCheck(tree.right)
  else
    return tree.item
  end
end

local N = tonumber(arg and arg[1]) or 0
local mindepth = 4
local maxdepth = mindepth + 2
if maxdepth < N then maxdepth = N end

do
  local stretchdepth = maxdepth + 1
  local stretchtree = BottomUpTree(0, stretchdepth)
  io.write(string.format("stretch tree of depth %d\t check: %d\n",
    stretchdepth, ItemCheck(stretchtree)))
end

local longlivedtree = BottomUpTree(0, maxdepth)

for depth=mindepth,maxdepth,2 do
  local iterations = 2 ^ (maxdepth - depth + mindepth)
  local check = 0
  for i=1,iterations do
    check = check + ItemCheck(BottomUpTree(1, depth)) +
            ItemCheck(BottomUpTree(-1, depth))
  end
  io.write(string.format("%d\t trees of depth %d\t check: %d\n",
    iterations*2, depth, check))
end

io.write(string.format("long lived tree of depth %d\t check: %d\n",
  maxdepth, ItemCheck(longlivedtree)))
  
