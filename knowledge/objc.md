---
type: contract
title: Objective-C from .smith
description: import <AppKit/AppKit.h>; reads a framework header as Objective-C in a separate compile and gives its classes, methods, enums and constants under objc::, each call typed and lowered to objc_msgSend.
tags: [objective-c, import, macos]
---
# Objective-C from .smith

**`import <Framework/Header.h>;` of an Apple framework gives `objc::`** (issue #318; `runtime/lang_objc.c`, `shim/objc_index.cpp`, `SmithHost.objc_index` in `shim/hook.cpp`). The owner's north star is no `.m` file and no generated binding files, and a header that always matches the SDK.

- **Read apart.** AppKit cannot be read as C, and reading the file as Objective-C would change what its C means (`id`, `SEL`, `BOOL`, `YES`, `nil` are names of Objective-C). So the host runs this binary again, `smith cc -x objective-c -fsyntax-only`, on a file with `#import <Header>` and `SMITH_OBJC_INDEX=<folder>`; a plugin (`shim/objc_index.cpp`, like the affine one) writes `types.h` (C: an opaque struct per class and protocol, the records, typedefs, enum constants, functions and globals, all named `objc_<name>`, with Mach-O asm labels) and `methods.idx` (a block per class and protocol, a line per method with C types). The folder is `.smith/objc/<key>/`, a build output (never committed); the key is the header, the SDK, the target, the flags and the binary. A framework header whose index has no class is a C header and is imported as before. The editor reads the folder and starts no compile.
- **Lowering.** `objc::X` is `objc_X`. At the last import the lowering includes each `types.h` and writes, in front of the first declaration, a `static inline` typed function for every method of each class whose name the file writes after `objc::` (found in the text, `SmithHost.main_text`), with its superclasses and protocols (`objc_NSWindow_setTitle`): the call is `objc_msgSend` (asm label `_objc_msgSend`, no header of Objective-C is included) cast to the method's exact type. Classes are distinct opaque structs, so a wrong argument is a C error. No `static` cache of the selector: a write would be a `globals` effect; the call through a pointer is `c`.
- **Names.** Parts of a selector are joined with `_`. A class method is renamed `class_<name>` only when an instance method of the same name is visible in the class's chain.
- **Linking.** `.linker_option "-framework", "X"` and `"-lobjc"` in file-scope asm become `LC_LINKER_OPTION`, which lld follows.
- **x86_64.** A method that returns a record over 16 bytes uses `objc_msgSend_stret` and `long double` `_fpret` (flags in the index); arm64 has neither. Only arm64 is tested.
- **Not here yet.** Ownership (`objc::Owned`, pools, strings) is part 2 of #318, classes #319, blocks #316, a subclass pointer taking a superclass's parameter without a cast.
