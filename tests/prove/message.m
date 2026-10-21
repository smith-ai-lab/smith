// An Objective-C message send: which method runs is not known.
#include <smith/contract.h>

#import <objc/NSObject.h>

#include <stdint.h>

int32_t hash_of(NSObject *object) {
    smith_requires(object != nullptr);
    return (int32_t)[object hash];
}
