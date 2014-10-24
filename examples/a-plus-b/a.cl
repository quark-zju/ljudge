(loop for n = (read t nil nil)
      while n
      do (format t "~d~C" (+ n (read)) #\linefeed))
