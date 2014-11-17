Module Program

  Sub Main()
    Do
      Dim line As String = Console.ReadLine()
      If line Is Nothing Then Exit Do
      Dim a() As String = line.Split(" "c)
      Console.WriteLine(CInt(a(0)) + CInt(a(1)))
    Loop
  End Sub

End Module
