import Foundation
while let line = readLine() {
    let words: [String] = line.components(separatedBy: NSCharacterSet.whitespaces)
    let inputs: [Int] = words.map { Int($0)! }
    print(inputs[0] + inputs[1])
}
