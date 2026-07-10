#!/bin/bash

# MID2OGG
for file in Audio/**/*.mid
do

filename="${file%.*}"
echo "MIDI $filename"

timidity "$file" -o "$filename.ogg" -Ov
rm "$file"

ffmpeg -i "$filename.ogg" -c:a libvorbis -qscale:a 0 "CONV/$filename.ogg"
rm "$filename.ogg"

done


# OGG REENCODE
for file in Audio/**/*.ogg
do

filename="${file%.*}"
echo "$filename"

ffmpeg -i "$file" -c:a libvorbis -qscale:a 0 "CONV/$filename.ogg"
rm "$file"

done

# WAV TO OGG
for file in Audio/**/*.wav
do

filename="${file%.*}"
echo "$filename"

ffmpeg -i "$file" -c:a libvorbis -qscale:a 0 "CONV/$filename.ogg"
rm "$file"

done

