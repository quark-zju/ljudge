public class Program {
  public static void Main() {
    string line;
    while ((line = System.Console.ReadLine ()) != null) {
      string[] tokens = line.Split();
      System.Console.WriteLine(int.Parse(tokens[0]) + int.Parse(tokens[1]));
    }
  }
}
