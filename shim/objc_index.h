// What the hook tells runtime/lang_objc.c about the compile (shim/objc_index.cpp): the parts of an Objective-C import
// that need clang's C++.
#pragma once

namespace clang {
class Preprocessor;
}

namespace smith {

// Lines `sysroot=<dir>`, `triple=<t>`, `framework=<dir>` for each -F folder, and `editor=1` in a tool that must not
// start a compile. Valid until the next call.
const char *objcFacts(clang::Preprocessor &pp, bool inEditor);

} // namespace smith
