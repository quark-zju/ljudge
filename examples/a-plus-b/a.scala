import scala.io.StdIn

object Main {
  def main(args: Array[String]): Unit = {
    while(true) {
      val line = StdIn.readLine()
      if (line == null) {
        return
      }
      println(line.split(" ").map{ case i => i.toInt }.sum)
    }
  }
}
