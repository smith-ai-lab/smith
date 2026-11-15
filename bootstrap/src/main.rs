//! The bootstrap compiler for Smith. It is written in Rust with no
//! dependencies, and is retired once Smith compiles itself.

/// The name the compiler is invoked by.
fn name() -> &'static str {
    "smith"
}

fn main() {
    println!("{}", name());
}

#[cfg(test)]
mod tests {
    use super::name;

    #[test]
    fn has_a_name() {
        assert_eq!(name(), "smith");
    }
}
