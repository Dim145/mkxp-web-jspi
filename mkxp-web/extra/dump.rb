require 'yaml'

eval(IO.read('dump_rgss.rb'))

file = File.open(ARGV[0], "r")
data = Marshal.load(file)
IO.write('dump.yml', data.to_yaml)

