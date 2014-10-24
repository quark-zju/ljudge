main = interact $ unlines . map (show . sum . map read . words) . lines
