#!/bin/bash

for f in Graphics/**/*
do
  echo $f
  echo "1" > $f
done

for f in Audio/**/*
do
  echo $f
  echo "1" > $f
done

