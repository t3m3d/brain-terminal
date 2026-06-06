#pragma once
#import <Cocoa/Cocoa.h>

// Native macOS terminal view: owns the C++ Terminal core + PTY, draws the
// grid with CoreText/AppKit, and forwards keystrokes to the shell.
// (Milestone 1 frontend — CPU CoreText draw; a Metal glyph-atlas path is
// the planned next step. See platform/macos/DESIGN.md.)
@interface TermView : NSView
- (instancetype)initWithFrame:(NSRect)frame
                        shell:(NSString*)shell
                     fontName:(NSString*)fontName
                     fontSize:(CGFloat)fontSize;
@end
