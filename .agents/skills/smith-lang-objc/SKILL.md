---
name: smith-lang-objc
description: "Objective-C from a .smith file: import <AppKit/AppKit.h>; and call classes under objc:: (objc::NSWindow::setTitle(w, t)), the _ naming of multi-part selectors, the class_ prefix, typed calls, linking with no flag, the c effect. macOS only. Load when a .smith file imports a framework header such as Foundation, AppKit, Metal, or writes objc::."
---

# Objective-C from .smith

`import <Framework/Header.h>;` of an Apple framework reads the header as Objective-C, apart from your file, and its classes, protocols, methods, enums, constants, functions and globals appear under `objc::`. There is no `.m` file, no binding file and no flag: the frameworks are linked by the import.

```smith
module main;

import <Foundation/Foundation.h>;
import smith::console;

i32 main() {
    objc::NSString *text = objc::NSString::stringWithUTF8String("hello");
    objc::NSMutableArray *list = objc::NSMutableArray::array();
    objc::NSMutableArray::addObject(list, text);
    console::print("length ", (u64)objc::NSString::length(text), "\n");
    return 0;
}
```

## Rules

- **A call** is `objc::Class::method(receiver, args...)`: the receiver of an instance method comes first, a class method has none (`objc::NSScreen::mainScreen()`). A selector with several parts joins them with `_`: `initWithContentRect:styleMask:backing:defer:` is `objc::NSWindow::initWithContentRect_styleMask_backing_defer(w, rect, style, objc::NSBackingStoreBuffered, false)`.
- **`class_`** goes in front of a class method only when an instance method of the class (or its superclasses and protocols) has the same name: `objc::NSObject::description(obj)` and `objc::NSObject::class_description()`. `alloc`, `new` and `mainScreen` keep their names.
- **A class or protocol is a type**: `objc::NSWindow *w`, `objc::MTLDevice *device` (a protocol is the same). An instance method is found on the class you name and on its superclasses and protocols, so a subclass pointer takes a superclass's name only by a cast: `objc::NSArray::count((objc::NSArray *)list)`. `id` and `Class` are `void *` and a class pointer.
- **Typed**: a call is `objc_msgSend` cast to the method's exact type, so an argument of another type is a compile error, in a call and in a struct passed by value (`objc::NSRect`, `objc::NSRange`). A method whose type C cannot spell (a block, a variadic `stringWithFormat:`) is not there.
- **Names**: `objc::NSRect`, `objc::NSBackingStoreBuffered`, `objc::MTLCreateSystemDefaultDevice()`, `objc::NSDefaultRunLoopMode` are the header's names; `BOOL` is `bool`. A string literal converts to `const char *` parameters.
- **Linking**: the import writes `-framework X` and `-lobjc` into the object; `smith build` needs nothing in `build.c`.
- **Effects**: a call has the `c` effect, so `effects::within(f, effects::c)` accepts it and `effects::none` rejects it.
- **C is C**: the header is read by a separate compile (`SMITH_OBJC_INDEX`, under `.smith/objc/`, made once per header, SDK and smith); the file never sees `id`, `SEL`, `BOOL` or `YES` from Objective-C, so a C name by any of them means what it means in C.
- **Not yet** (issue #318 part 2): `objc::Owned`, `retain`/`release` as ownership, pools, `objc::string`/`objc::str`; call `release` as a method. Not yet (#319): defining classes; blocks wait for lambdas (#316). A framework header with no class in it is read as a C header.

Reference: [knowledge/objc.md](../../../knowledge/objc.md).
