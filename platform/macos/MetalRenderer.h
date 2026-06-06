#pragma once
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include "kterm/core/Terminal.hpp"

// GPU renderer for the terminal grid: one CoreText glyph atlas + a single
// batched draw (cell backgrounds, glyphs, selection, cursor). Opt-in via
// KTERM_RENDERER=metal; the CPU CoreText path stays the default fallback.
@interface MetalRenderer : NSObject

- (instancetype)initWithFont:(NSFont*)font
                        bold:(NSFont*)bold
                      italic:(NSFont*)italic
                  boldItalic:(NSFont*)boldItalic
                       cellW:(CGFloat)cellW
                       cellH:(CGFloat)cellH
                       scale:(CGFloat)scale;

- (BOOL)ready;   // pipeline + device came up
- (id<MTLDevice>)mtlDevice;

- (void)renderTerminal:(kterm::core::Terminal*)term
                 layer:(CAMetalLayer*)layer
                 viewW:(CGFloat)viewW
                 viewH:(CGFloat)viewH
          scrollOffset:(int)scrollOffset
                  cols:(int)cols
                  rows:(int)rows
          hasSelection:(BOOL)hasSelection
              selStart:(NSPoint)selStart       // (col,row)
                selEnd:(NSPoint)selEnd
               caretOn:(BOOL)caretOn
             defaultFg:(NSColor*)defaultFg
             defaultBg:(NSColor*)defaultBg;
@end
