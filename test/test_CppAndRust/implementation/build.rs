fn main() {
    cbindgen::generate(".")
        .unwrap()
        .write_to_file("./implementation.h");
}