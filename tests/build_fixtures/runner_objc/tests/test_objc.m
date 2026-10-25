#import <Foundation/Foundation.h>

#include <smith/test.h>

@interface Probe : NSObject
- (void)requirePositive:(int)value;
@end

@implementation Probe
- (void)requirePositive:(int)value {
    smith_check(value > 0);
}
@end

void test_objc_fails(void) {
    Probe *probe = [[Probe alloc] init];
    [probe requirePositive:-1];
}
