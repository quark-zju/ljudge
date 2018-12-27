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

  export SEGFAULT_USE_ALTSTACK=1
  export SEGFAULT_OUTPUT_NAME=segfault.$i.$$.log
  RESULT=`ljudge --debug --keep-stdout --keep-stderr --user-code $src --testcase --input 1.in --output 1.out --testcase --input 2.in --output-sha1 7b80e651613b8ee99d6f2a578062673a7e40bb65,6b979d97a529a16aced6c9cc59375fa4fe3962e7 2> $DEBUG_LOG | cat`
  EXITCODE=$?
  if [ "$EXITCODE" != 0 ] || [ -z "$RESULT" ] || (echo1 "$RESULT" | grep -qi ERROR) || (echo1 "$RESULT" | grep -qv ACCEPT); then
    # Log error
    echo `date` 'Error running' $i 'test (exit code ' $EXITCODE ')' >> $ERROR_LOG
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

# Test the legacy checker with the wrong user code. AC on first case and WA on second case.
echo -n 'Test legacy checker: '
src=wa.c
RESULT=`ljudge --debug --keep-stdout --keep-stderr --user-code $src --testcase --input 1.in --output 1.out --testcase --input 2.in --output 2.out --checker-code legacy_checker.c 2> $DEBUG_LOG | cat`
EXITCODE=$?
if [ "$EXITCODE" != 0 ] || [ -z "$RESULT" ] || (echo1 "$RESULT" | grep -qi ERROR) || (echo1 "$RESULT" | grep -qv ACCEPT) || (echo1 "$RESULT" | grep -qv WRONG_ANSWER); then
  # Log error
  echo `date` 'Error running' $i 'test (exit code ' $EXITCODE ')' >> $ERROR_LOG
  echo1 "$RESULT" >> $ERROR_LOG
  cat $DEBUG_LOG >> $ERROR_LOG
  echo >> $ERROR_LOG
  # notify user
  echo1 'ERROR' "$RESULT"
  echo
  echo 'To re-run: ljudge -u '$src' -i 1.in -o 1.out -i 2.in -o 2.out -c legacy_checker.c'
  echo
  else
  echo OKAY
fi


[ -e $DEBUG_LOG ] && unlink $DEBUG_LOG
