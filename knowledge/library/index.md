# The Smith standard library

The modules of the standard library, one file each, named for the module. A module that declares an effect lists the effect's operations.

- [list.md](list.md): `List`, an array kept together with its size, which can grow.
- [map.md](map.md): `Map`, which finds a value by its key, kept in the order its entries were added, and the `Hash` trait a key needs.
- [random.md](random.md): random bits from a key, distributions as ordinary structs with the `Sample` and `LogProb` traits, and how a program adds its own.
- [fs.md](fs.md): the `fs` effect, for files: a file open for a block, ranges of it read as values, and reading and writing a whole file.
- [console.md](console.md): the `console` effect, for a program's standard input, output, and error.
