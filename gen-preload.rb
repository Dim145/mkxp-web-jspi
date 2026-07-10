#!/usr/bin/env ruby
# Generate build/preload/Data/MapNNN.rxdata.json prefetch lists (the optional
# per-map asset prefetch system of mkxp-web; upstream generated these with
# extra/dump.sh, one slow shell `find` per string — this does all maps in one
# pass). For each Data/Map*.rxdata: Marshal-load it (with RGSS class stubs),
# collect every String in the object graph, and keep those that resolve to an
# existing game file ("<value>.*" matched by basename, like upstream, plus
# path-relative matches). Output: JSON array of relative asset paths.
#
# Usage: ruby gen-preload.rb <gameasync_dir> <out_preload_dir>
require 'json'
require 'set'

GA  = File.expand_path(ARGV[0] || 'mkxp-web/build/gameasync')
OUT = File.expand_path(ARGV[1] || 'mkxp-web/build/preload')

# ---- RGSS class stubs so Marshal.load succeeds (mirrors extra/dump_rgss.rb) ----
class Table
  def self._load(s); Table.new(s); end
  def initialize(s); @s = s; end
end
class Color
  def self._load(s); Color.new(s); end
  def initialize(s); @s = s; end
end
class Tone
  def self._load(s); Tone.new(s); end
  def initialize(s); @s = s; end
end
module RPG
  %w[Map MapInfo Event Event::Page Event::Page::Condition Event::Page::Graphic
     EventCommand MoveRoute MoveCommand AudioFile].each do |name|
    parts = name.split('::')
    mod = self
    parts.each do |p|
      mod = mod.const_defined?(p, false) ? mod.const_get(p) : mod.const_set(p, Class.new)
    end
  end
end

# ---- index all game files by lowercase basename-without-extension ----
by_base = Hash.new { |h, k| h[k] = [] }
by_relpath = {}
Dir.chdir(GA) do
  Dir.glob('**/*').each do |f|
    next unless File.file?(f)
    base = File.basename(f, File.extname(f)).downcase
    by_base[base] << f
    by_relpath[f.sub(/\.[^.\/]+$/, '').downcase] = f
  end
end

def walk(obj, strings, seen)
  return if seen.include?(obj.object_id)
  case obj
  when String
    strings << obj
  when Array
    seen << obj.object_id
    obj.each { |v| walk(v, strings, seen) }
  when Hash
    seen << obj.object_id
    obj.each { |k, v| walk(k, strings, seen); walk(v, strings, seen) }
  else
    if obj.respond_to?(:instance_variables) && !obj.instance_variables.empty?
      seen << obj.object_id
      obj.instance_variables.each { |iv| walk(obj.instance_variable_get(iv), strings, seen) }
    end
  end
end

count = 0
Dir.glob(File.join(GA, 'Data', 'Map*.rxdata')).sort.each do |path|
  name = File.basename(path)                       # Map033.rxdata
  begin
    data = Marshal.load(File.binread(path))
  rescue => e
    warn "skip #{name}: #{e.class}: #{e.message}"
    next
  end
  strings = Set.new
  walk(data, strings, Set.new)

  assets = Set.new
  strings.each do |s|
    s = s.b   # binary: script strings can carry invalid UTF-8 bytes
    next if s.empty? || s.bytesize > 120 || s.include?("\n".b) || s.include?("\0".b)
    key = s.downcase.tr('\\', '/')
    # exact path-with-known-prefix match (e.g. "Graphics/Pictures/foo")
    if (f = by_relpath[key.sub(/\.[^.\/]+$/, '')])
      assets << f
      next
    end
    # bare-name match (charset/tileset/audio names): basename anywhere
    base = File.basename(key, File.extname(key))
    next if base.empty?
    by_base[base].each { |f| assets << f } if by_base.key?(base)
  end
  # keep only real asset types; drop rxdata self-references and scripts
  list = assets.select { |f| f =~ /\.(png|jpg|jpeg|gif|ogg|wav|mp3|mid|txt)$/i }.sort

  outdir = File.join(OUT, 'Data')
  require 'fileutils'; FileUtils.mkdir_p(outdir)
  File.write(File.join(outdir, "#{name}.json"), JSON.generate(list))
  count += 1
end
puts "generated #{count} preload lists in #{OUT}/Data/"
