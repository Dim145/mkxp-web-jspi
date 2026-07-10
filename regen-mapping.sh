#!/bin/bash
# Regenerate mapping.js over the EXISTING gameasync, INCLUDING directory entries
# (value "path?h=" with empty md5) so drive.js createDummies() can FS.mkdir them.
# Matches upstream make_mapping.sh semantics, minus the slow bitmap thumbnails.
set -e
GA=/src/mkxp-web/build/gameasync
cd "$GA"

: > /tmp/mapping_body.txt
shopt -s globstar nullglob
count=0
for file in **/*; do
  # skip the generated files themselves
  case "$file" in mapping.js|bitmap-map.js) continue ;; esac
  filename="${file%.*}"
  fl="$(printf '%s' "$filename" | tr '[:upper:]' '[:lower:]')"
  if [ -f "$file" ]; then
    md5="$(md5sum "$file" | awk '{print $1}')"
  else
    md5=''          # directories (and non-regular files) -> folder marker
  fi
  printf '["%s", "%s?h=%s"],\n' "$fl" "$file" "$md5" >> /tmp/mapping_body.txt
  count=$((count+1))
  if [ $((count % 3000)) -eq 0 ]; then echo "   ...indexed $count entries"; fi
done
{
  echo "var mappingArray = ["
  cat /tmp/mapping_body.txt
  echo "];"
  echo "var mapping = {};"
  echo "for (var i = 0; i < mappingArray.length; i++) { mapping[mappingArray[i][0]] = mappingArray[i][1]; }"
} > mapping.js
echo "var bitmapSizeMapping = {};" > bitmap-map.js
dirs=$(grep -c 'h="],' /tmp/mapping_body.txt || true)
echo ">>> mapping.js regenerated: $count entries total (incl. directory markers)"
