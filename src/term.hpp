#pragma once

namespace term {
  namespace attr {
    const int RESET      = 0;
    const int BOLD       = 1;
    const int UNDERSCORE = 4;
    const int BLINK      = 5;
    const int REVERSE    = 7;
    const int CONCEALED  = 8;
  }

  namespace fg {
    const int  BLACK     = 30;
    const int  RED       = 31;
    const int  GREEN     = 32;
    const int  YELLOW    = 33;
    const int  BLUE      = 34;
    const int  MAGENTA   = 35;
    const int  CYAN      = 36;
    const int  WHITE     = 37;
  }

  namespace bg {
    const int  BLACK     = 40;
    const int  RED       = 41;
    const int  GREEN     = 42;
    const int  YELLOW    = 43;
    const int  BLUE      = 44;
    const int  MAGENTA   = 45;
    const int  CYAN      = 46;
    const int  WHITE     = 47;
  }

  extern void set(int attr, int fg);
  extern void set(int attr, int fg, int bg);
  extern void set(int attr = attr::RESET);
}
