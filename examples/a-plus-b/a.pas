var
  a, b: integer;
begin
  while not eof(input) do begin
    readln(a, b);
    writeln(a + b);
  end;
end.
