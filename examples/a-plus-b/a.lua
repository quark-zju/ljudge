for a, b in io.read('*a'):gmatch('([%d-]+) ([%d-]+)') do
  print(tonumber(a) + tonumber(b))
end
