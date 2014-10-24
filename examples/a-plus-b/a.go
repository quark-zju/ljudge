package main

import "fmt"

func main() {
  var a, b int
  for {
    n, _ := fmt.Scanf("%d %d", &a, &b)
    if (n != 2) { break }
    fmt.Println(a + b)
  }
}
