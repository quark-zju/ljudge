#include "term.hpp"
#include <cstdio>
#include <unistd.h>

void term::set(int attr, int fg, int bg, FILE *fp) {
  if (!isatty(fileno(fp))) return;
  fprintf(fp, "\x1b[%d;%d;%dm", attr, fg, bg);
}

void term::set(int attr, int fg, FILE *fp) {
  if (!isatty(fileno(fp))) return;
  fprintf(fp, "\x1b[%d;%dm", attr, fg);
}

void term::set(int attr, FILE *fp) {
  if (!isatty(fileno(fp))) return;
  fprintf(fp, "\x1b[%dm", attr);
}
