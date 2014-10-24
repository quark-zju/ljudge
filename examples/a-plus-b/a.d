import std.stdio;

void main() {
  int a, b;
  while (readf(" %s %s", &a, &b)) {
    writeln(a + b);
  }
}
