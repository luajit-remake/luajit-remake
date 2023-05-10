#!/bin/bash
TASKSET_PIN_CPU_CORE=4

if [ ! -f "luajitr" ]; then
	echo "[ERROR] Benchmark executable 'luajitr' not found! Did you run the build?"
	exit
fi

if [ -z "$(./luajitr -v 2>&1 | grep 'release build')" ]; then 
	echo "[ERROR] Benchmark executable 'luajitr' is not built in release mode!"
	echo "[ERROR] You should only run benchmark using a release build."
	exit
fi 

# If this is the first time we run the benchmark, create the input data file 'FASTA_5000000' 
#
if [ ! -f "luabench/FASTA_5000000" ]; then
	lua luabench/fasta.lua 5000000 > luabench/FASTA_5000000
fi

run_bench_once() {
	# Although only 'k-nucleotide' and 'revcomp' need the FASTA_5000000 data input, 
	# for simplicity we always redirect the input, since it doesn't affect anything if the benchmark doesn't use it
	#
	{ time taskset -c $TASKSET_PIN_CPU_CORE $@ < luabench/FASTA_5000000 > /dev/null 2>&1 ; } 2> /tmp/bench_time.out
	RET_CODE=$?
	if [ $RET_CODE -ne 0 ]; then
		echo "[ERROR] Benchmark failed with return code ${RET_CODE}!"
		echo "Benchmark command: $@"
		echo "Exiting..."
		exit
	fi
	T=`grep 'real' /tmp/bench_time.out | cut -f 2`
	echo $T >> benchmark.log
	echo $T
	# Intel PLEASE fix your overheat problem...
	# I observed 10% perf fluctuation due to overheating on my i7-12700H, 
	# even though this benchmark is only using 1 of the 20 CPU cores...
	# It turns out that the overheating issue is gone if I allow the CPU to rest 3 sec after each benchmark
	#
	sleep 3
}

run_bench() {
	echo "### $1" >> benchmark.log
	echo "Benchmark: $@"
	FILE_PATH="luabench/$1"
	shift
	run_bench_once ./luajitr $FILE_PATH $@
	run_bench_once ./luajitr $FILE_PATH $@
	run_bench_once ./luajitr $FILE_PATH $@
	run_bench_once ./luajitr $FILE_PATH $@
	run_bench_once ./luajitr $FILE_PATH $@
}

echo -n > benchmark.log

run_bench array3d.lua 300 packed
run_bench binary-trees-num.lua 16
run_bench binary-trees-name.lua 15
run_bench bounce.lua 3000
run_bench cd.lua
run_bench chameneos.lua 1e7
run_bench coroutine-ring.lua 2e7
run_bench deltablue.lua
run_bench fannkuch.lua 11
run_bench fasta.lua 5e6
run_bench fixpoint-fact.lua 1000
run_bench havlak.lua
run_bench heapsort.lua 1 3000000
run_bench json.lua
run_bench k-nucleotide.lua 5e6
run_bench life.lua 2000
run_bench linear-sieve.lua 3e7
run_bench list.lua
run_bench mandelbrot.lua 3000
run_bench mandel-metatable.lua 256
run_bench nbody.lua 5e6
run_bench nsieve.lua 12
run_bench partialsums.lua 3e7
run_bench permute.lua
run_bench pidigits-nogmp.lua 5000
run_bench qt.lua 14
run_bench quadtree-2.lua 14
run_bench queen.lua 12
run_bench ray.lua 9
run_bench ray-prop.lua 9
run_bench recursive-fib-uv.lua 40
run_bench recursive-fib-gv.lua 40
run_bench revcomp.lua 5e6
run_bench richard.lua
run_bench scimark-fft.lua 10
run_bench scimark-lu.lua 5
run_bench scimark-sor.lua 5
run_bench scimark-sparse.lua 300
run_bench series.lua 5000
run_bench spectral-norm.lua 2000
run_bench storage.lua
run_bench table-sort.lua 5e6
run_bench table-sort-cmp.lua 1e6
run_bench towers.lua

python3 bench_pretty_format.py

