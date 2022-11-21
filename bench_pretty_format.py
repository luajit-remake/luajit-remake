result = {}
with open('benchmark.log') as f:
  first = True
  bench_name = ""
  num_runs = 0
  total_time = 0
  for line in f:
    line = line.strip()
    if len(line) == 0:
      continue
    if line.startswith('### '):
      if bench_name != "":
        assert(not bench_name in result)
        assert(num_runs > 0)
        result[bench_name] = total_time/num_runs
      assert(line.endswith('.lua'))
      bench_name = line[4:-4]
      assert(bench_name != "")
      num_runs = 0
      total_time = 0
    else:
      hr=0
      pos = line.find('h')
      if pos != -1:
        hr = float(line[0:pos])
        line = line[pos+1:]
      mi=0
      pos = line.find('m')
      if pos != -1:
        mi = float(line[0:pos])
        line = line[pos+1:]
      se=0
      pos = line.find('s')
      assert(pos != -1)
      assert(pos == len(line) - 1)
      se = float(line[0:pos])
      t = hr * 3600 + mi * 60 + se
      num_runs = num_runs + 1
      total_time = total_time + t
  if bench_name != "":
    assert(not bench_name in result)
    assert(num_runs > 0)
    result[bench_name] = total_time/num_runs

for k in result:
	print(k, '\t', result[k])


