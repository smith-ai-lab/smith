# The Smith standard library

The modules of the standard library, one file each, named for the module. A module that declares an effect lists the effect's operations.

- [list.md](list.md): `List`, an array kept together with its size, which can grow.
- [random.md](random.md): random bits from a key, distributions as ordinary structs with the `Sample` and `LogProb` traits, and how a program adds its own.
- [console.md](console.md): the `console` effect, for a program's standard input, output, and error.
