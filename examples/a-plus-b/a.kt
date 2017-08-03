import java.util.Scanner

fun main(args: Array<String>) {
    val input = Scanner(System.`in`)
    while (input.hasNextInt()) {
        val a = input.nextInt()
        val b = input.nextInt()
        println(a + b)
    }
}
