# The Smith standard library

The modules of the standard library, one file each, named for the module. A module that declares an effect lists the effect's operations.

- [list.md](list.md): `List`, an array kept together with its size, which can grow.
- [slice.md](slice.md): `Slice`, a run of elements that cannot change and is shared freely, made from a `List` by `freeze`.
- [list-functions.md](list-functions.md): `std.list`'s functions that search and compare any list, a text among them, and `append`.
- [text.md](text.md): `std.text`'s `split_once` and `strip_prefix`, which cut a text into pieces that share its memory, and `Text`, a text that grows and becomes a `str` with nothing read again.
- [int.md](int.md): `std.int`, `narrow` and `clamp`, which take a value into the range expected, and arithmetic that wraps, for the ranges that fill a size.
- [float.md](float.md): `std.float`, `floor`, `ceil`, `trunc`, and `round`, which round a float to a whole number that is still a float.
- [bits.md](bits.md): `std.bits`, the names of sizes `u8` to `i64`, the `Bits` trait, and its functions on the bits of a range that fills a size.
- [ascii.md](ascii.md): `std.ascii`, which says whether a byte is a digit, a hexadecimal digit, a letter, or a space.
- [utf8.md](utf8.md): `std.utf8`'s `decode` and `encode`, which turn between one character's number and its bytes.
- [fmt.md](fmt.md): `std.fmt`'s `hex` and `binary`, which write a whole number in base 16 or base 2, padded with zeros.
- [map.md](map.md): `Map`, which finds a value by its key, kept in the order its entries were added, and the `Hash` trait a key needs.
- [serialize.md](serialize.md): `Serialize`, by which a value describes its shape to a writer that knows a format, and `Writer`, which a format has.
- [deserialize.md](deserialize.md): `Deserialize`, by which a value is built from a reader that knows a format, `Reader`, which a format has, and what is done with a missing field.
- [describe.md](describe.md): `Describe`, by which a type says what it is made of with no value in hand, each part with its comment, and `Describer`, which a format for descriptions has.
- [json.md](json.md): `std.json`, which writes a value that has `Serialize` as JSON text and reads JSON text into a value that has `Deserialize`, the settings for how a case and a field's name look, and `RawJson`.
- [http.md](http.md): `std.http`, with which a program answers HTTP requests with plain functions, and `at`, which puts a server of messages at a path.
- [html.md](html.md): `std.html`, with which a page is written as calls of plain functions that write it as they run, with every text escaped.
- [mcp.md](mcp.md): `std.mcp`, with which a program is a Model Context Protocol server whose tools are plain functions described by their comments and parameters.
- [secret.md](secret.md): `Secret[T]`, a value the time a program takes must not depend on, which cannot steer a branch or be an index.
- [random.md](random.md): random bits from a key, distributions as ordinary structs with the `Sample` and `LogProb` traits, and how a program adds its own.
- [fs.md](fs.md): the `fs` effect, for files: a file open for a block, ranges of it read as values, and reading and writing a whole file.
- [store.md](store.md): the `store` effect, which keeps bytes under keys past the end of the program, and `save` and `load`, which keep any value that has `Serialize`.
- [console.md](console.md): the `console` effect, for a program's standard input, output, and error.
