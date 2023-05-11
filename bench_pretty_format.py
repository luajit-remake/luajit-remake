def get_avg(time_list):
  # Although very rarely, I do have seen outliers that are much slower than normal data points
  # Maybe it's some noise due to desktop environment background process,
  # but for now, just deal with it by stripping one fastest and one slowest before taking average
  #
  assert(len(time_list) > 0)
  time_list.sort()
  if len(time_list) >= 5:
    time_list = time_list[1:len(time_list)-1]
  total_time = 0
  for x in time_list:
    total_time = total_time + x
  return total_time/len(time_list)

result = {}
with open('benchmark.log') as f:
  first = True
  bench_name = ""
  time_list = []
  for line in f:
    line = line.strip()
    if len(line) == 0:
      continue
    if line.startswith('### '):
      if bench_name != "":
        assert(not bench_name in result)
        result[bench_name] = get_avg(time_list)
      assert(line.endswith('.lua'))
      bench_name = line[4:-4]
      assert(bench_name != "")
      time_list = []
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
      time_list.append(t)
  if bench_name != "":
    assert(not bench_name in result)
    result[bench_name] = get_avg(time_list)

for k in result:
	print(k, '\t', result[k])


