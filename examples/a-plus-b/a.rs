use std::io;

fn main() {
    let mut input = String::new();

    io::stdin()
        .read_line(&mut input)
        .unwrap();

    let mut s = input.trim().split(' ');

    let a_str = s.next().unwrap();
    let a: i32 = a_str.parse().unwrap();

    let b_str = s.next().unwrap();
    let b: i32 = b_str.parse().unwrap();

    println!("{}", a + b);
}
