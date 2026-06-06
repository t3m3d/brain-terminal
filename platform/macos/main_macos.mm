#import <Cocoa/Cocoa.h>
#import "TermView.h"
#include <cstdlib>

// Minimal app delegate: quit when the last window closes.
@interface TerkAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation TerkAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }
@end

// Minimal menu bar so Cmd-C/V/A route to the first responder (TermView).
static void installMenu(NSApplication* app) {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Quit kterm" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];

    NSMenuItem* editItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:editItem];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Copy"       action:@selector(copy:)      keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste"      action:@selector(paste:)     keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    [editItem setSubmenu:editMenu];

    [app setMainMenu:mainMenu];
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        // `open kterm.app --args --metal` selects the GPU renderer (env also works).
        for (int i = 1; i < argc; i++)
            if (strcmp(argv[i], "--metal") == 0) setenv("KTERM_RENDERER", "metal", 1);
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        TerkAppDelegate* delegate = [[TerkAppDelegate alloc] init];
        [app setDelegate:delegate];
        installMenu(app);

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
