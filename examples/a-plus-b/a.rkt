#lang racket

(define (f)
  (let ([n (read)])
    (if (eof-object? n)
        (void)
        (begin (displayln (+ (read) n))
               (f)))))

(f)
