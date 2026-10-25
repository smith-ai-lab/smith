#import <Foundation/Foundation.h>

@interface Thing : NSObject {
    int _count;
    NSString *Mixed_Name;
}
@property(nonatomic, assign) int size_of;
- (void)setCount:(int)count withName:(NSString *)name;
+ (instancetype)Make_Thing;
@end

@implementation Thing
- (void)setCount:(int)count withName:(NSString *)name {
    _count = count;
    Mixed_Name = name;
}
+ (instancetype)Make_Thing {
    return nil;
}
@end
