#include "term.hpp"
#include <cstdio>
#include <unistd.h>

void term::set(int attr, int fg, int bg) {
  if (!isatty(1)) return;
  printf("\x1b[%d;%d;%dm", attr, fg, bg);
}

void term::set(int attr, int fg) {
  if (!isatty(1)) return;
  printf("\x1b[%d;%dm", attr, fg);
}

void term::set(int attr) {
  if (!isatty(1)) return;
  printf("\x1b[%dm", attr);
}
