#import "TermView.h"
#include <vector>
#include <string>
#include <cstdint>

#include "kterm/core/Terminal.hpp"
#include "kterm/pty/PTY.hpp"
#include "kterm/renderer/Grid.hpp"
#include "kterm/renderer/Cell.hpp"

using kterm::core::Terminal;
using kterm::pty::PTY;

// Decode the grid's 0xAARRGGBB packing into an NSColor.
static NSColor* colorFromARGB(uint32_t c) {
    CGFloat a = ((c >> 24) & 0xFF) / 255.0;
    CGFloat r = ((c >> 16) & 0xFF) / 255.0;
    CGFloat g = ((c >> 8)  & 0xFF) / 255.0;
    CGFloat b = ( c        & 0xFF) / 255.0;
    return [NSColor colorWithSRGBRed:r green:g blue:b alpha:a];
}

@implementation TermView {
    Terminal*  _term;
    PTY*       _pty;
    NSFont*    _font;
    NSFont*    _fontBold;
    NSFont*    _fontItalic;
    NSFont*    _fontBoldItalic;
    NSColor*   _defaultBg;
    NSColor*   _defaultFg;
    CGFloat    _cellW;
    CGFloat    _cellH;
    int        _cols;
    int        _rows;
    std::string _shell;
}

- (instancetype)initWithFrame:(NSRect)frame
                        shell:(NSString*)shell
                     fontName:(NSString*)fontName
                     fontSize:(CGFloat)fontSize {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _font = [NSFont fontWithName:fontName size:fontSize];
    if (!_font) _font = [NSFont monospacedSystemFontOfSize:fontSize weight:NSFontWeightRegular];
    NSFontManager* fm = [NSFontManager sharedFontManager];
    _fontBold       = [fm convertFont:_font toHaveTrait:NSBoldFontMask];
    _fontItalic     = [fm convertFont:_font toHaveTrait:NSItalicFontMask];
    _fontBoldItalic = [fm convertFont:_fontBold toHaveTrait:NSItalicFontMask];
    _defaultBg = [NSColor colorWithSRGBRed:0.07 green:0.07 blue:0.09 alpha:1.0];
    _defaultFg = [NSColor colorWithSRGBRed:0.92 green:0.92 blue:0.96 alpha:1.0];

    // Monospace cell metrics.
    NSDictionary* attrs = @{ NSFontAttributeName: _font };
    _cellW = [@"M" sizeWithAttributes:attrs].width;
    _cellH = ceil(_font.ascender - _font.descender + _font.leading);
    if (_cellW < 1) _cellW = fontSize * 0.6;
    if (_cellH < 1) _cellH = fontSize * 1.2;

    _shell = std::string([shell UTF8String]);
    [self rebuildTerminalForSize:frame.size];

    return self;
}

- (void)rebuildTerminalForSize:(NSSize)size {
    int cols = (int)(size.width  / _cellW);
    int rows = (int)(size.height / _cellH);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    if (_term && cols == _cols && rows == _rows) return;
    _cols = cols; _rows = rows;

    if (!_term) {
        _term = new Terminal(cols, rows);
        _pty  = new PTY();

        __unsafe_unretained TermView* u = self;

        _term->setRenderCallback([u]() {
            dispatch_async(dispatch_get_main_queue(), ^{ [u setNeedsDisplay:YES]; });
        });

        _pty->setOutputCallback([u](const std::vector<char>& data) {
            std::vector<char> copy = data;  // hop off the read thread
            dispatch_async(dispatch_get_main_queue(), ^{
                u->_term->onPTYOutput(copy);
                [u setNeedsDisplay:YES];
            });
        });

        _pty->spawnShell(_shell);
    } else {
        _term->resize(cols, rows);
        if (_pty) _pty->resize(cols, rows);
    }
}

- (BOOL)isFlipped { return YES; }            // top-left origin, like the grid
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsLayer { return YES; }

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self rebuildTerminalForSize:newSize];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    [_defaultBg set];
    NSRectFill(self.bounds);

    if (!_term) return;
    const auto& rows = _term->grid().rows();

    for (int r = 0; r < (int)rows.size(); ++r) {
        const auto& line = rows[r];
        CGFloat y = r * _cellH;
        for (int c = 0; c < (int)line.size(); ++c) {
            const kterm::renderer::Cell& cell = line[c];
            CGFloat x = c * _cellW;
            uint8_t a = cell.attrs;

            // Effective colors (fg sentinel 0xFFFFFFFF / bg alpha 0 = defaults).
            NSColor* fg = (cell.fg != 0xFFFFFFFF) ? colorFromARGB(cell.fg) : _defaultFg;
            NSColor* bg = (((cell.bg >> 24) & 0xFF) != 0) ? colorFromARGB(cell.bg) : nil;
            if (a & kterm::renderer::ATTR_INVERSE) {       // swap fg/bg
                NSColor* prevFg = fg;
                fg = bg ? bg : _defaultBg;
                bg = prevFg;
            }

            if (bg) {
                [bg set];
                NSRectFill(NSMakeRect(x, y, _cellW, _cellH));
            }

            uint32_t cp = cell.ch;
            if (cp != ' ' && cp != 0) {
                NSFont* f = _font;
                if      ((a & kterm::renderer::ATTR_BOLD) && (a & kterm::renderer::ATTR_ITALIC)) f = _fontBoldItalic;
                else if (a & kterm::renderer::ATTR_BOLD)   f = _fontBold;
                else if (a & kterm::renderer::ATTR_ITALIC) f = _fontItalic;

                NSMutableDictionary* attrs = [@{ NSFontAttributeName: f,
                                                 NSForegroundColorAttributeName: fg } mutableCopy];
                if (a & kterm::renderer::ATTR_UNDERLINE)
                    attrs[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);

                NSString* s = [[NSString alloc] initWithBytes:&cp
                                                       length:sizeof(cp)
                                                     encoding:NSUTF32LittleEndianStringEncoding];
                if (s) [s drawAtPoint:NSMakePoint(x, y) withAttributes:attrs];
            }
        }
    }

    // Caret (simple block, translucent).
    int cr = _term->grid().cursorRow();
    int cc = _term->grid().cursorCol();
    [[NSColor colorWithSRGBRed:0.55 green:0.78 blue:1.0 alpha:0.55] set];
    NSRectFillUsingOperation(NSMakeRect(cc * _cellW, cr * _cellH, _cellW, _cellH),
                             NSCompositingOperationSourceOver);
}

// ── Keyboard → PTY ────────────────────────────────────────────────────
- (void)keyDown:(NSEvent*)event {
    if (!_pty) return;
    NSString* chars = event.characters;
    if (chars.length == 0) return;

    unichar u0 = [chars characterAtIndex:0];
    std::string out;

    switch (u0) {
        case NSUpArrowFunctionKey:    out = "\x1b[A"; break;
        case NSDownArrowFunctionKey:  out = "\x1b[B"; break;
        case NSRightArrowFunctionKey: out = "\x1b[C"; break;
        case NSLeftArrowFunctionKey:  out = "\x1b[D"; break;
        case NSHomeFunctionKey:       out = "\x1b[H"; break;
        case NSEndFunctionKey:        out = "\x1b[F"; break;
        case NSDeleteCharacter:       out = "\x7f";   break;  // backspace
        default: {
            const char* utf8 = [chars UTF8String];
            if (utf8) out = std::string(utf8);
            break;
        }
    }
    if (!out.empty()) _pty->writeInput(out);
}

- (void)dealloc {
    delete _term;
    delete _pty;
}

@end
