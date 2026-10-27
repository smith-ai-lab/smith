---
name: smith-lang-objc-classes
description: "Defining Objective-C classes in a .smith file with no .m file: objc::methods(objc::P, Data) tables, objc::class_define, objc::self and objc::data, super_ calls (objc::NSWindow::super_canBecomeKeyWindow), and objc::block for completion handlers such as Metal's addCompletedHandler:. Load when a .smith file writes objc::class_define, objc::methods or objc::block, or defines an app delegate or an NSWindow subclass."
---

# Objective-C classes from .smith

Needs the calls of [smith-lang-objc](../smith-lang-objc/SKILL.md).

`import smith::objc;` (also `objc::`) adds `objc::class_define`, `objc::self`, `objc::data` and `objc::block`. `objc::methods(objc::P, Data)` is a struct with a function pointer for each instance method of the class or protocol `P` and of what it inherits, named like a call (`windowWillResize_toSize`), with `Data *` where Objective-C passes the object. Fill the ones you implement; `objc::class_define(name, objc::Super, Data, tables...)` registers a subclass of `Super` (an `objc::id`, null when the name exists), adds each table's methods and conforms to its protocol. `Data` is a `struct Data {...};` of the file, kept inline in the object. Allocate with `objc::alloc(cls)` (an `objc::id`; cast it) and `init` as usual; `objc::data(Data, object)` reads the data.

- **Errors**: a function of the wrong type is C's incompatible function pointer error on the field; a misspelled method is `no member named`; a method that a protocol requires and the literal omits is `does not set .name, a method that P requires` (only a `(objc::methods(...)){...}` literal is checked).
- **self and super**: a method gets the `Data *`; `objc::self(objc::NSWindow, data)` is the object (an `objc::NSWindow *`). `objc::NSWindow::super_canBecomeKeyWindow(w)` sends the message to `w` starting above `NSWindow`, the class you gave `class_define`; a smith-defined superclass cannot be named yet.
- **Block**: `struct Done { objc::Block block; i32 calls; };` and `objc::block(done, &context)` make a block whose function is `done(Done *, arguments...)`; the context must outlive the block (a static or heap object). `addCompletedHandler:` takes it.
- **Types at run time**: the method types come from the superclass or the protocol; a method nothing declares traps. The run loop is `objc::NSApplication::run(app)`; a test closes its window from code instead.

```smith
module delegate;

import <AppKit/AppKit.h>;
import smith::objc;

struct App {
    i32 closed;
};

void will_close(App *app, objc::NSNotification *) {
    app->closed += 1;
}

export objc::id define() {
    return objc::class_define("MacosApp", objc::NSObject, App,
        (objc::methods(objc::NSWindowDelegate, App)){.windowWillClose = will_close});
}
```

Reference: [knowledge/objc.md](../../../knowledge/objc.md).
