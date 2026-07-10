#!/usr/bin/env ruby
# WEB PORT: generate gameasync/bitmap-map.js (bitmapSizeMapping).
#
# Why this matters: mkxp-web's drive.js has two image-load paths (loadFileAsync):
#   * WITH a sizemap entry -> it sizes a placeholder canvas to [w,h] from an
#     INSTANT data-URI, hands the correctly-sized bitmap back to C++ immediately,
#     then streams the real pixels in the background (load() + reloadBitmap).
#   * WITHOUT a sizemap entry -> it must getLazyAsset() the whole file
#     synchronously before returning control to C++, blocking the ASYNCIFY loop
#     for the full XHR (and hitting a 10s retry timer). That is the multi-second
#     stall that made the game feel "strangely slow" every time a new image
#     (intro pictures, a map's tileset+charsets) was first touched.
#
# The shipped bitmap-map.js was an empty `{}`, so EVERY first image load blocked.
# This script fills it from the real PNG/GIF/BMP dimensions (header-only reads,
# no decode) so every image load takes the fast, non-blocking path.
#
# Usage: ruby gen-bitmap-map.rb
require 'json'

BUILD   = File.expand_path("mkxp-web/build", __dir__)
GAMEASY = File.join(BUILD, "gameasync")
MAPJS   = File.join(GAMEASY, "mapping.js")
OUTJS   = File.join(GAMEASY, "bitmap-map.js")

# 1x1 fully-transparent GIF: instant to decode, no network. Real pixels replace
# it a moment later via drive.js's background load()+reloadBitmap.
PLACEHOLDER = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"

def png_size(bytes)
  return nil unless bytes[0, 8] == "\x89PNG\r\n\x1a\n".b
  # 8-byte sig, 4-byte len, "IHDR", then width(4 BE), height(4 BE)
  return nil unless bytes[12, 4] == "IHDR".b
  w = bytes[16, 4].unpack1("N")
  h = bytes[20, 4].unpack1("N")
  [w, h]
end

def gif_size(bytes)
  return nil unless bytes[0, 6] == "GIF87a".b || bytes[0, 6] == "GIF89a".b
  w = bytes[6, 2].unpack1("v")
  h = bytes[8, 2].unpack1("v")
  [w, h]
end

def bmp_size(bytes)
  return nil unless bytes[0, 2] == "BM".b
  w = bytes[18, 4].unpack1("l<")
  h = bytes[22, 4].unpack1("l<").abs
  [w, h]
end

def image_size(path)
  bytes = File.binread(path, 32) rescue nil
  return nil unless bytes && bytes.bytesize >= 24
  case File.extname(path).downcase
  when ".png"        then png_size(bytes)
  when ".gif"        then gif_size(bytes)
  when ".bmp"        then bmp_size(bytes)
  else nil
  end
end

IMG_EXT = %w[.png .gif .bmp]

# WEB PORT (memory): the sizemap fast-path in drive.js builds a full [w,h] canvas,
# toDataURL()s it and createPreloadedFiles a placeholder for EVERY listed image
# before streaming the real pixels. For big images (tilesets, panoramas,
# battlebacks -- often 256x2000+ = a multi-MB transient canvas each) that is a lot
# of peak memory + CPU, and loading a whole asset-dense outdoor area at once caused
# a ~25s stall and pushed memory hard. So we OMIT large images from the sizemap:
# they fall back to the slow (blocking) load path -- a one-time pause when a map's
# tileset first loads, instead of per-image memory churn. Small, frequently-loaded
# images (character sprites, icons) stay in the sizemap so gameplay stays smooth.
# NOTE: excluding large images (tilesets) BROKE outdoor rendering -- the native
# tilemap renders black without a sizemap entry (it needs the correctly-sized
# placeholder up front; the slow path leaves it blank). So keep ALL images in the
# sizemap. The real transfer crash was the mruby alias+super dispose bug (fixed in
# 025_Sprite_Character), NOT memory, and ALLOW_MEMORY_GROWTH + INITIAL_MEMORY=256MB
# handle the churn. Threshold set high enough to include everything.
MAX_SIZEMAP_PIXELS = 100_000_000

# Path prefixes (lowercased) to keep OUT of the sizemap, comma-separated, via the
# SIZEMAP_EXCLUDE env var (default: none). See the exclusion note in the loop below --
# use this for UIs that redraw many images very rapidly and race the sizemap fast-path.
SIZEMAP_EXCLUDE = (ENV["SIZEMAP_EXCLUDE"] || "").split(",").map { |s| s.strip.downcase }.reject(&:empty?)

src = File.read(MAPJS, encoding: "UTF-8")
# Each entry: ["key", "Relative/Path.ext?h=hash"],
entries = src.scan(/\[\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\]/)

out = []
missing = 0
skipped_nonimg = 0
large_skipped = 0
excluded = 0
entries.each do |key, val|
  rel = val.split("?").first          # strip ?h=hash
  ext = File.extname(rel).downcase
  next (skipped_nonimg += 1) unless IMG_EXT.include?(ext)
  disk = File.join(GAMEASY, rel)
  size = image_size(disk)
  if size.nil?
    missing += 1
    next
  end
  if size[0]*size[1] > MAX_SIZEMAP_PIXELS
    large_skipped += 1
    next
  end
  # Optional exclusions (SIZEMAP_EXCLUDE). The sizemap fast-path hands the engine a
  # correctly-sized placeholder, then streams the real bytes and calls createPreloadedFile
  # for the same path. A UI that loads/redraws many images very rapidly (e.g. a paperdoll
  # screen cycling dozens of files) can race that double-load into "PHYSFS ERROR" spam and
  # an FS error. Such images load fine on the slow single-createPreloadedFile path, so let
  # the game exclude their path prefixes here. (Small menu images -> no gameplay-stall cost.)
  if SIZEMAP_EXCLUDE.any? { |pfx| key.start_with?(pfx) }
    excluded += 1
    next
  end
  out << [key, size[0], size[1]]
end

File.open(OUTJS, "w") do |f|
  f.write("var bitmapSizeMapping = {\n")
  out.each_with_index do |(key, w, h), i|
    comma = (i == out.length - 1) ? "" : ","
    f.write(%Q{#{key.to_json}:[#{w},#{h},"#{PLACEHOLDER}"]#{comma}\n})
  end
  f.write("};\n")
end

puts "Wrote #{out.length} image size entries to #{OUTJS}"
puts "  (skipped #{skipped_nonimg} non-image entries; #{missing} unreadable; #{large_skipped} large + #{excluded} excluded imgs omitted -> slow path)"
puts "  bitmap-map.js is now #{File.size(OUTJS)} bytes"
