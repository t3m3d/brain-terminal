#import <Cocoa/Cocoa.h>
#import "TermView.h"
#import "Config.h"
#include <cstdlib>

// Minimal app delegate: quit when the last window closes.
@interface BrainAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation BrainAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }
@end

// Minimal menu bar so Cmd-C/V/A route to the first responder (TermView).
static void installMenu(NSApplication* app) {
    NSMenu* mainMenu = [[NSMenu alloc] init];

    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Quit brain" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];

    NSMenuItem* editItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:editItem];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Copy"       action:@selector(copy:)      keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste"      action:@selector(paste:)     keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    [editItem setSubmenu:editMenu];

    // Format menu: the native font panel (Cmd-T) changes the font live.
    NSMenuItem* fmtItem = [[NSMenuItem alloc] init];
    [mainMenu addItem:fmtItem];
    NSMenu* fmtMenu = [[NSMenu alloc] initWithTitle:@"Format"];
    NSMenuItem* fonts = [fmtMenu addItemWithTitle:@"Show Fonts"
                                           action:@selector(orderFrontFontPanel:)
                                    keyEquivalent:@"t"];
    [fonts setTarget:[NSFontManager sharedFontManager]];
    [fmtItem setSubmenu:fmtMenu];

    [app setMainMenu:mainMenu];
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        // `open brain.app --args --metal` selects the GPU renderer (env also works).
        for (int i = 1; i < argc; i++)
            if (strcmp(argv[i], "--metal") == 0) setenv("BRAIN_RENDERER", "metal", 1);
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        BrainAppDelegate* delegate = [[BrainAppDelegate alloc] init];
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
        [window setTitle:@"brain"];

        // User config: ~/.config/brain/config (font, colors, opacity, renderer).
        BrainConfig* config = [BrainConfig loadConfig];

        // Window-level transparency so a translucent background shows the desktop.
        if (config.opacity < 1.0) {
            window.opaque = NO;
            window.backgroundColor = [NSColor clearColor];
        }

        const char* shellEnv = getenv("SHELL");
        NSString* shell = shellEnv ? [NSString stringWithUTF8String:shellEnv] : @"/bin/zsh";

        TermView* view = [[TermView alloc] initWithFrame:frame
                                                   shell:shell
                                                  config:config];
        [window setContentView:view];
        [window makeFirstResponder:view];
        [window center];
        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];

        [app run];
    }
    return 0;
}
