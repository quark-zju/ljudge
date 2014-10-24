STDIN.each_line do |line|
  puts line.split.map(&:to_i).inject(:+)
end
