#!/usr/bin/env ruby
# Extract / repack RPG Maker XP Scripts.rxdata <-> editable .rb files.
# Scripts.rxdata = Marshal array of [magic_int, name_string, zlib_deflated_code].
#
#   ruby scripts_tool.rb extract <Scripts.rxdata> <outdir>
#   ruby scripts_tool.rb pack    <outdir> <Scripts.rxdata>
#
# Edit the .rb files in <outdir>; index.json preserves order + section names.
require 'zlib'
require 'json'
require 'fileutils'

cmd, a, b = ARGV

case cmd
when 'extract'
  scripts = Marshal.load(File.binread(a))
  FileUtils.mkdir_p(b)
  index = []
  scripts.each_with_index do |(magic, name, deflated), i|
    code = Zlib::Inflate.inflate(deflated)
    safe = name.to_s.gsub(/[^\w\-]+/, '_')
    safe = 'untitled' if safe.empty?
    fname = format('%03d_%s.rb', i, safe)
    File.binwrite(File.join(b, fname), code)
    index << { 'i' => i, 'magic' => magic, 'name' => name, 'file' => fname }
  end
  File.write(File.join(b, 'index.json'), JSON.pretty_generate(index))
  puts "Extracted #{scripts.size} sections to #{b}/"

when 'pack'
  index = JSON.parse(File.read(File.join(a, 'index.json')))
  arr = index.map do |h|
    code = File.binread(File.join(a, h['file']))
    [h['magic'], h['name'], Zlib::Deflate.deflate(code)]
  end
  File.binwrite(b, Marshal.dump(arr))
  puts "Packed #{arr.size} sections into #{b}"

else
  abort "usage: ruby scripts_tool.rb extract <Scripts.rxdata> <outdir>\n" \
        "       ruby scripts_tool.rb pack <outdir> <Scripts.rxdata>"
end
