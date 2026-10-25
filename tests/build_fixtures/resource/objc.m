#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
int main(void) { @autoreleasepool { NSString *s = [NSString stringWithFormat:@"%d", 7]; puts(s.UTF8String); (void)MTLCreateSystemDefaultDevice(); NSApplication *a = NSApp; (void)a; } return 0; }
