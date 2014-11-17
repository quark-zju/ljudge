import java.util.Scanner;

public class Main {
	public static void main(String[] args) {
		Scanner in = new Scanner(System.in);
    try {
      while (in.hasNextInt()) {
        int a = in.nextInt();
        int b = in.nextInt();
        System.out.println(a + b);
      }
    } catch (NullPointerException ex) {
      // gcj Scanner has a bug that throws NPE
      ;
    }
	}
}
