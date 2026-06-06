#pragma once
#import <Cocoa/Cocoa.h>
#import "Config.h"

// Native macOS terminal view: owns the C++ Terminal core + PTY, draws the
// grid with CoreText/AppKit (or Metal), and forwards keystrokes to the shell.
@interface TermView : NSView
- (instancetype)initWithFrame:(NSRect)frame
                        shell:(NSString*)shell
                       config:(BrainConfig*)config;
@end
