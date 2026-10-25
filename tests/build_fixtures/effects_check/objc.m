#import <Foundation/Foundation.h>

@interface Mine : NSObject
- (int)value;
@end
@implementation Mine
- (int)value {
    return 1;
}
@end

int objc(void) {
    Mine *mine = [[Mine alloc] init];
    NSFileManager *manager = [NSFileManager defaultManager];
    return [mine value] + (int)[manager fileExistsAtPath:@"/tmp"];
}
