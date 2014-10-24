#!/bin/sh

LIST="$@"
[ -z "$LIST" ] && LIST=`ls a* | sed 's#a\.##'`

for i in $LIST; do
  src=a.$i
  [ -e $src ] || continue
  echo '>>' $src ': ' ljudge -u $src -i 1.in -o 1.out -i 2.in -o 2.out --debug
  ljudge --keep-stdout --keep-stderr --user-code $src --testcase --input 1.in --output 1.out --testcase --input 2.in --output 2.out | cat
  echo
  echo
done 2>/dev/null
