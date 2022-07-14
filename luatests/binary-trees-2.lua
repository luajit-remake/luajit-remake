-- The Computer Language Benchmarks Game
-- http://benchmarksgame.alioth.debian.org/
-- contributed by Mike Pall

local function BottomUpTree(item, depth)
  if depth > 0 then
    local i = item + item
    depth = depth - 1
    local left, right = BottomUpTree(i-1, depth), BottomUpTree(i, depth)
    return { item = item, left = left, right = right }
  else
    return { item = item }
  end
end

local function ItemCheck(tree)
  if tree.left then
    return tree.item + ItemCheck(tree.left) - ItemCheck(tree.right)
  else
    return tree.item
  end
end

local N = 0
local mindepth = 4
local maxdepth = mindepth + 2
if maxdepth < N then maxdepth = N end

do
  local stretchdepth = maxdepth + 1
  local stretchtree = BottomUpTree(0, stretchdepth)
  io.write("stretch tree of depth ", stretchdepth, "\t check: ", ItemCheck(stretchtree), "\n")
end

local longlivedtree = BottomUpTree(0, maxdepth)

for depth=mindepth,maxdepth,2 do
  local iterations = 2 ^ (maxdepth - depth + mindepth)
  local check = 0
  for i=1,iterations do
    check = check + ItemCheck(BottomUpTree(1, depth)) +
            ItemCheck(BottomUpTree(-1, depth))
  end
  io.write(iterations*2, "\t trees of depth ", depth, "\t check: ", check, "\n")
end

io.write("long lived tree of depth ", maxdepth, "\t check: ", ItemCheck(longlivedtree), "\n")

