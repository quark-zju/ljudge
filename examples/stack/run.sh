#!/bin/sh

echo1() {
  # echo in 1 line
  echo "$@" | tr -d "\n"
}

DEBUG_LOG=.debug.$$.log
ERROR_LOG=error.log

# Test configuring stack size
src=large_stack.c
# Use default stack size (8M) will cause SEGMENTATION_FAULT
echo -n 'Test limitation of stack size: '
RESULT=`ljudge --debug --keep-stdout --keep-stderr --user-code $src --testcase --input 1.in --output 1.out --testcase --input 2.in --output 2.out 2> $DEBUG_LOG | cat`
EXITCODE=$?
if [ "$EXITCODE" != 0 ] || [ -z "$RESULT" ] || (echo1 "$RESULT" | grep -qi ERROR) || (echo1 "$RESULT" | grep -qv SEGMENTATION_FAULT); then
  # Log error
  echo `date` 'Error running' $i 'test (exit code ' $EXITCODE ')' >> $ERROR_LOG
  echo1 "$RESULT" >> $ERROR_LOG
  cat $DEBUG_LOG >> $ERROR_LOG
  echo >> $ERROR_LOG
  # notify user
  echo1 'ERROR' "$RESULT"
  echo
  echo 'To re-run: ljudge -u '$src' -i 1.in -o 1.out -i 2.in -o 2.out'
  echo
  else
  echo OKAY
fi
# Use larger stack size to eliminate SEGMENTATION_FAULT
echo -n 'Test larger stack size: '
RESULT=`ljudge --debug --keep-stdout --keep-stderr --user-code $src --max-stack 128M --testcase --input 1.in --output 1.out --testcase --input 2.in --output 2.out 2> $DEBUG_LOG | cat`
EXITCODE=$?
if [ "$EXITCODE" != 0 ] || [ -z "$RESULT" ] || (echo1 "$RESULT" | grep -qi ERROR) || (echo1 "$RESULT" | grep -q SEGMENTATION_FAULT); then
  # Log error
  echo `date` 'Error running' $i 'test (exit code ' $EXITCODE ')' >> $ERROR_LOG
  echo1 "$RESULT" >> $ERROR_LOG
  cat $DEBUG_LOG >> $ERROR_LOG
  echo >> $ERROR_LOG
  # notify user
  echo1 'ERROR' "$RESULT"
  echo
  echo 'To re-run: ljudge -u '$src' -i 1.in -o 1.out -i 2.in -o 2.out --max-stack 128M'
  echo
  else
  echo OKAY
fi

[ -e $DEBUG_LOG ] && unlink $DEBUG_LOG
