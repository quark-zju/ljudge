try
  while true do
    Scanf.scanf " %d %d" (fun a b -> Printf.printf "%d\n" (a + b))
  done;
  None
with
  End_of_file -> None
;;
