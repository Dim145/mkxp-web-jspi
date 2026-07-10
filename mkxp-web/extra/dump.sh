#!/bin/bash
ARRAY=()
ARRAYFILES=()

FILE=$1

mkdir -p preload/Data
rm -f preload/$FILE.json
touch preload/$FILE.json

ruby dump.rb $FILE

while IFS= read -r line; do
    if [[ $line == *"::"* ]]; then
        continue
    fi
    if [[ $line == *"!"* ]]; then
        continue
    fi
    if [[ $line == *"true" ]]; then
        continue
    fi
    if [[ $line == *"false" ]]; then
        continue
    fi
    if [[ $line == *"[]" ]]; then
        continue
    fi

    if [[ $line == *":"* ]]; then
        value=${line#*:}
    fi
    
    if [[ $line == *"-"* ]]; then
        value=${line#*-}
    fi

    value=${value//\*}
    value=${value//[[:blank:]]/}
    value=${value//\'}
        
    re="^[0-9.-]+$"
    if [[ $value =~ $re ]] ; then
        continue
    fi

    re='^EV[0-9]+$'
    if [[ $value =~ $re ]] ; then
        continue
    fi

    if [[ $value == 'A' ]] ; then
        continue
    fi
        
    if [ -z "$value" ] ; then
        continue
    fi

    if [[ " ${ARRAY[@]} " =~ " ${value} " ]]; then
        continue  
    fi

    echo $value
    ARRAY+=($value)

    files="$(find -name "$value.*")"
    files="${files//\.\/}"
    if [ -z "$files" ] ; then
        continue
    fi

    echo "$files"
    ARRAYFILES+=($files)

done < dump.yml
rm dump.yml

echo '[' >> preload/$FILE.json
printf '"%s",\n' "${ARRAYFILES[@]}" | sed '$s/,$//' >> preload/$FILE.json
echo ']' >> preload/$FILE.json

echo "============================================================"
cat preload/$FILE.json
echo "============================================================"

