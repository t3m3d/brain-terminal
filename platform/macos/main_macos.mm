#import <Cocoa/Cocoa.h>
#import "TermView.h"
#include <cstdlib>

// Minimal app delegate: quit when the last window closes.
@interface TerkAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation TerkAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }
@end

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        TerkAppDelegate* delegate = [[TerkAppDelegate alloc] init];
        [app setDelegate:delegate];

        NSRect frame = NSMakeRect(0, 0, 900, 560);
        NSWindow* window =
            [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled |
                                                   NSWindowStyleMaskClosable |
                                                   NSWindowStyleMaskMiniaturizable |
                                                   NSWindowStyleMaskResizable)
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        [window setTitle:@"kterm"];

        const char* shellEnv = getenv("SHELL");
        NSString* shell = shellEnv ? [NSString stringWithUTF8String:shellEnv] : @"/bin/zsh";

        // Prefer a Nerd Font (powerline/icon glyphs for prompts like p10k);
        // TermView falls back to the system monospaced font if it's absent.
        TermView* view = [[TermView alloc] initWithFrame:frame
                                                   shell:shell
                                                fontName:@"JetBrainsMono Nerd Font Mono"
                                                fontSize:13.0];
        [window setContentView:view];
        [window makeFirstResponder:view];
        [window center];
        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];

        [app run];
    }
    return 0;
}
