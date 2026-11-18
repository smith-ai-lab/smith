# The Smith standard library

The modules of the standard library, one file each, named for the module. A module that declares an effect lists the effect's operations.

- [list.md](list.md): `List`, an array kept together with its size, which can grow.
- [list-functions.md](list-functions.md): `std.list`'s functions that search and compare any list, a text among them, and `append`.
- [text.md](text.md): `std.text`'s `split_once` and `strip_prefix`, which cut a text into pieces that share its memory.
- [int.md](int.md): `std.int`, arithmetic that wraps, saturates, or is checked, and `narrow`.
- [bits.md](bits.md): `std.bits`, the `Bits` trait and its functions on the bits of a whole number.
- [ascii.md](ascii.md): `std.ascii`, which says whether a byte is a digit, a hexadecimal digit, a letter, or a space.
- [utf8.md](utf8.md): `std.utf8`'s `decode` and `encode`, which turn between one character's number and its bytes.
- [fmt.md](fmt.md): `std.fmt`'s `hex` and `binary`, which write a whole number in base 16 or base 2, padded with zeros.
- [map.md](map.md): `Map`, which finds a value by its key, kept in the order its entries were added, and the `Hash` trait a key needs.
- [random.md](random.md): random bits from a key, distributions as ordinary structs with the `Sample` and `LogProb` traits, and how a program adds its own.
- [fs.md](fs.md): the `fs` effect, for files: a file open for a block, ranges of it read as values, and reading and writing a whole file.
- [console.md](console.md): the `console` effect, for a program's standard input, output, and error.
