#!/bin/sh

LIST="$@"
[ -z "$LIST" ] && LIST=`ls a* | sed 's#a\.##'`

echo1() {
  # echo in 1 line
  echo "$@" | tr -d "\n"
}

DEBUG_LOG=.debug.$$.log
ERROR_LOG=error.log

for i in $LIST; do
  src=a.$i
  [ -e $src ] || continue
  echo -n $src': '

  RESULT=`ljudge --debug --keep-stdout --keep-stderr --user-code $src --testcase --input 1.in --output 1.out --testcase --input 2.in --output 2.out 2> $DEBUG_LOG | cat`
  if [ -z "$RESULT" ] || (echo1 "$RESULT" | grep -qi ERROR) || (echo1 "$RESULT" | grep -qv ACCEPT); then
    # Log error
    echo `date` 'Error running' $i 'test' >> $ERROR_LOG
    echo1 "$RESULT" >> $ERROR_LOG
    cat $DEBUG_LOG >> $ERROR_LOG
    echo >> $ERROR_LOG
    # notify user
    echo1 'ERROR' "$RESULT"
    echo
    echo 'To re-run: ljudge -u '$src' -i 1.in -o 1.out -i 2.in -o 2.out --debug'
    echo
  else
    echo OKAY
  fi
done 2>/dev/null

[ -e $DEBUG_LOG ] && unlink $DEBUG_LOG
