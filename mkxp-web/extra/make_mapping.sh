#!/bin/bash

echo "var mappingArray = [" > mapping.js
echo "var bitmapSizeMapping = {" > bitmap-map.js

shopt -s globstar
for file in **/*; do

filename="${file%.*}"
fl="$(echo "$filename" | tr '[:upper:]' '[:lower:]')"

if [ -f "${file}" ]
then
    md5=`md5sum "${file}" | awk '{ print $1 }'`

    sz=`identify -format "%w,%h" "${file}" 2>/dev/null`
    if [ $? -eq 0 ]; then
        convert "$file" -resize 64x64\> "conv.png"
        mimetype=$(file -bN --mime-type "conv.png")
        content=$(base64 -w0 < "conv.png")
        duri="data:$mimetype;base64,$content"
        rm "conv.png"

        echo "\"$fl\": [${sz},\"${duri}\"]," >> bitmap-map.js
    fi
else
    md5=''
fi

echo "[\"$fl\", \"${file}?h=${md5}\"]," >> mapping.js

done

echo "];" >> mapping.js
echo "};" >> bitmap-map.js

echo "
var mapping = {};
for (var i = 0; i < mappingArray.length; i++) {
    mapping[mappingArray[i][0]] = mappingArray[i][1];
}
" >> mapping.js

