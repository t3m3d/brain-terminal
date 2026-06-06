#import "TermView.h"
#import "MetalRenderer.h"
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

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
    int        _scrollOffset;   // rows scrolled up from the live bottom (0 = bottom)
    // Selection (in current-viewport row/col coordinates).
    BOOL       _selecting;
    BOOL       _hasSelection;
    int        _selStartRow, _selStartCol;
    int        _selEndRow,   _selEndCol;
    BOOL       _caretOn;
    NSTimer*   _blinkTimer;
    BOOL       _metal;             // KTERM_RENDERER=metal
    CAMetalLayer* _metalLayer;
    MetalRenderer* _renderer;
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
    const char* rend = getenv("KTERM_RENDERER");
    _metal = (rend && strcmp(rend, "metal") == 0);
    // Defer terminal/PTY creation until the view is in the window with its
    // real laid-out size (see viewDidMoveToWindow) — spawning here with the
    // pre-layout frame can give the shell a slightly-wrong column count, so
    // p10k draws the prompt frame short until the first resize.

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
            dispatch_async(dispatch_get_main_queue(), ^{ [u refresh]; });
        });

        _pty->setOutputCallback([u](const std::vector<char>& data) {
            std::vector<char> copy = data;  // hop off the read thread
            dispatch_async(dispatch_get_main_queue(), ^{
                u->_term->onPTYOutput(copy);
                [u refresh];
            });
        });

        _pty->spawnShell(_shell);

        // Startup nudge: p10k's "instant prompt" draws a cached prompt at the
        // previous terminal's width and only repaints on a real SIGWINCH.
        // Once the shell has loaded, briefly wobble the row count to force one
        // SIGWINCH so the prompt redraws at our actual column width.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.6 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            if (u->_pty && u->_term && u->_rows > 1) {
                u->_pty->resize(u->_cols, u->_rows - 1);
                u->_pty->resize(u->_cols, u->_rows);
            }
        });

        // Blinking caret.
        _caretOn = YES;
        _blinkTimer = [NSTimer scheduledTimerWithTimeInterval:0.53 repeats:YES block:^(NSTimer* t){
            u->_caretOn = !u->_caretOn;
            if (u->_scrollOffset == 0) [u refresh];
        }];
    } else {
        _term->resize(cols, rows);
        if (_pty) _pty->resize(cols, rows);
    }
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (!self.window) return;
    if (_metal && !_renderer) {
        CGFloat scale = self.window.backingScaleFactor ?: 1.0;
        _renderer = [[MetalRenderer alloc] initWithFont:_font bold:_fontBold italic:_fontItalic
                                             boldItalic:_fontBoldItalic
                                                  cellW:_cellW cellH:_cellH scale:scale];
        if (![_renderer ready]) { NSLog(@"kterm: Metal unavailable, using CPU"); _metal = NO; _renderer = nil; }
        [self updateDrawableSize];
    }
    // Spawn the shell once the view has its real on-screen size, so the PTY
    // gets the correct column count from the first prompt.
    if (!_term) [self rebuildTerminalForSize:self.bounds.size];
}

- (CALayer*)makeBackingLayer {
    if (_metal) {
        _metalLayer = [CAMetalLayer layer];
        _metalLayer.device = MTLCreateSystemDefaultDevice();
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _metalLayer.framebufferOnly = YES;
        _metalLayer.contentsScale = self.window ? self.window.backingScaleFactor : 2.0;
        return _metalLayer;
    }
    return [super makeBackingLayer];
}

- (void)updateDrawableSize {
    if (!_metalLayer) return;
    CGFloat scale = self.window ? self.window.backingScaleFactor : 2.0;
    _metalLayer.contentsScale = scale;
    CGSize sz = self.bounds.size;
    _metalLayer.drawableSize = CGSizeMake(sz.width * scale, sz.height * scale);
}

// Redraw via whichever renderer is active.
- (void)refresh {
    if (_metal && _renderer) [self renderMetal];
    else [self setNeedsDisplay:YES];
}

- (void)renderMetal {
    if (!_renderer || !_metalLayer || !_term) return;
    [_renderer renderTerminal:_term layer:_metalLayer
                        viewW:self.bounds.size.width viewH:self.bounds.size.height
                 scrollOffset:_scrollOffset cols:_cols rows:_rows
                 hasSelection:_hasSelection
                     selStart:NSMakePoint(_selStartCol, _selStartRow)
                       selEnd:NSMakePoint(_selEndCol, _selEndRow)
                      caretOn:_caretOn defaultFg:_defaultFg defaultBg:_defaultBg];
}

- (BOOL)isFlipped { return YES; }            // top-left origin, like the grid
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsLayer { return YES; }

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self rebuildTerminalForSize:newSize];
    [self updateDrawableSize];
    [self refresh];
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_term) return;
    CGFloat dy = event.scrollingDeltaY;
    if (dy == 0) return;
    CGFloat perLine = event.hasPreciseScrollingDeltas ? _cellH : 1.0;
    int lines = (int)(dy / perLine);
    if (lines == 0) lines = (dy > 0 ? 1 : -1);   // positive dy = scroll up = older
    int H = _term->grid().historyLines();
    _scrollOffset += lines;
    if (_scrollOffset < 0) _scrollOffset = 0;
    if (_scrollOffset > H) _scrollOffset = H;
    _hasSelection = NO;               // selection is viewport-relative; drop it on scroll
    [self refresh];
}

// ── Mouse selection ───────────────────────────────────────────────────
- (void)cellForEvent:(NSEvent*)event row:(int*)row col:(int*)col {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    int r = (int)(p.y / _cellH);
    int c = (int)(p.x / _cellW);
    if (r < 0) r = 0;  if (r >= _rows) r = _rows - 1;
    if (c < 0) c = 0;  if (c > _cols)  c = _cols;   // one past end = select to EOL
    *row = r; *col = c;
}

- (void)mouseDown:(NSEvent*)event {
    int r, c; [self cellForEvent:event row:&r col:&c];
    _selStartRow = _selEndRow = r; _selStartCol = _selEndCol = c;
    _selecting = YES; _hasSelection = NO;
    [self refresh];
}
- (void)mouseDragged:(NSEvent*)event {
    if (!_selecting) return;
    int r, c; [self cellForEvent:event row:&r col:&c];
    _selEndRow = r; _selEndCol = c; _hasSelection = YES;
    [self refresh];
}
- (void)mouseUp:(NSEvent*)event { _selecting = NO; }

- (void)normSelStart:(int*)sr col:(int*)sc end:(int*)er col:(int*)ec {
    int aR=_selStartRow, aC=_selStartCol, bR=_selEndRow, bC=_selEndCol;
    if (bR < aR || (bR == aR && bC < aC)) { int t; t=aR;aR=bR;bR=t; t=aC;aC=bC;bC=t; }
    *sr=aR; *sc=aC; *er=bR; *ec=bC;
}

// Cells of visible row vr (history or live), matching drawRect's mapping.
- (const std::vector<kterm::renderer::Cell>*)viewportRow:(int)vr {
    const auto& grid = _term->grid();
    int H = grid.historyLines();
    int s = _scrollOffset; if (s < 0) s = 0; if (s > H) s = H;
    int idx = (H - s) + vr;
    if (idx < 0) return nullptr;
    if (idx < H) return &grid.historyRow(idx);
    int lr = idx - H;
    const auto& live = grid.rows();
    if (lr < 0 || lr >= (int)live.size()) return nullptr;
    return &live[lr];
}

- (void)copySelection {
    if (!_hasSelection || !_term) return;
    int sr, sc, er, ec; [self normSelStart:&sr col:&sc end:&er col:&ec];
    NSMutableString* out = [NSMutableString string];
    for (int vr = sr; vr <= er; ++vr) {
        const std::vector<kterm::renderer::Cell>* line = [self viewportRow:vr];
        if (!line) continue;
        int n  = (int)line->size();
        int c0 = (vr == sr) ? sc : 0;
        int c1 = (vr == er) ? ec : n;
        if (c1 > n) c1 = n;
        std::u32string u;
        for (int c = c0; c < c1; ++c) u.push_back((char32_t)(*line)[c].ch);
        while (!u.empty() && u.back() == U' ') u.pop_back();   // trim trailing spaces
        NSString* rs = [[NSString alloc] initWithBytes:u.data()
                                                length:u.size() * 4
                                              encoding:NSUTF32LittleEndianStringEncoding];
        if (rs) [out appendString:rs];
        if (vr < er) [out appendString:@"\n"];
    }
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:out forType:NSPasteboardTypeString];
}

- (void)pasteClipboard {
    if (!_pty) return;
    NSString* str = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (str.length == 0) return;
    const char* u = [str UTF8String];
    if (!u) return;
    std::string data(u);
    // Bracketed paste: wrap so the shell treats it as literal text (newlines
    // don't auto-execute) when the app has requested it (ESC[?2004h).
    if (_term && _term->bracketedPaste())
        data = std::string("\x1b[200~") + data + std::string("\x1b[201~");
    _pty->writeInput(data);
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        NSString* ch = event.charactersIgnoringModifiers;
        if ([ch isEqualToString:@"c"] && _hasSelection) { [self copySelection]; return YES; }
        if ([ch isEqualToString:@"v"]) { [self pasteClipboard]; return YES; }
        if ([ch isEqualToString:@"a"]) { [self selectAll:nil]; return YES; }
    }
    return [super performKeyEquivalent:event];
}

// Let a drag start (and select) even when the click also activates the window.
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

// Right-click (or control-click / two-finger tap) context menu.
- (NSMenu*)menuForEvent:(NSEvent*)event {
    NSMenu* m = [[NSMenu alloc] init];
    NSMenuItem* copyItem = [m addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@""];
    [copyItem setTarget:self];
    [copyItem setEnabled:_hasSelection];
    NSMenuItem* pasteItem = [m addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@""];
    [pasteItem setTarget:self];
    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* allItem = [m addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@""];
    [allItem setTarget:self];
    return m;
}

// Standard responder actions (routed by the Edit menu / Cmd-C/V/A).
- (void)copy:(id)sender  { [self copySelection]; }
- (void)paste:(id)sender { [self pasteClipboard]; }
- (void)selectAll:(id)sender {
    if (!_term) return;
    _selStartRow = 0; _selStartCol = 0;
    _selEndRow = _rows - 1; _selEndCol = _cols;
    _hasSelection = YES;
    [self refresh];
}

- (void)drawRect:(NSRect)dirtyRect {
    if (_metal) return;             // Metal renders via renderMetal, not drawRect
    [_defaultBg set];
    NSRectFill(self.bounds);

    if (!_term) return;
    const auto& grid = _term->grid();
    int H = grid.historyLines();
    int s = _scrollOffset; if (s < 0) s = 0; if (s > H) s = H;
    const auto& live = grid.rows();
    int R = (int)live.size();

    for (int vr = 0; vr < R; ++vr) {
        // Map this visible row to [history(0..H-1) ++ live(0..R-1)], offset up by s.
        int idx = (H - s) + vr;
        if (idx < 0) continue;
        const std::vector<kterm::renderer::Cell>* line;
        if (idx < H) line = &grid.historyRow(idx);
        else { int lr = idx - H; if (lr >= R) continue; line = &live[lr]; }

        CGFloat y = vr * _cellH;
        for (int c = 0; c < (int)line->size(); ++c) {
            const kterm::renderer::Cell& cell = (*line)[c];
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

    // Selection highlight (translucent overlay, viewport rows).
    if (_hasSelection) {
        int sr, sc, er, ec; [self normSelStart:&sr col:&sc end:&er col:&ec];
        [[NSColor colorWithSRGBRed:0.30 green:0.50 blue:0.90 alpha:0.35] set];
        for (int vr = sr; vr <= er && vr < R; ++vr) {
            int c0 = (vr == sr) ? sc : 0;
            int c1 = (vr == er) ? ec : _cols;
            if (c1 > c0)
                NSRectFillUsingOperation(NSMakeRect(c0 * _cellW, vr * _cellH,
                                                    (c1 - c0) * _cellW, _cellH),
                                         NSCompositingOperationSourceOver);
        }
    }

    // Caret (live bottom only, when visible + in the "on" blink phase).
    if (s == 0 && _caretOn && _term->cursorVisible()) {
        int cr = grid.cursorRow();
        int cc = grid.cursorCol();
        [[NSColor colorWithSRGBRed:0.55 green:0.78 blue:1.0 alpha:0.55] set];
        NSRectFillUsingOperation(NSMakeRect(cc * _cellW, cr * _cellH, _cellW, _cellH),
                                 NSCompositingOperationSourceOver);
    }
}

// ── Keyboard → PTY ────────────────────────────────────────────────────
- (void)keyDown:(NSEvent*)event {
    if (!_pty) return;
    _scrollOffset = 0;   // typing snaps back to the live bottom
    _hasSelection = NO;  // and clears any selection
    _caretOn = YES;      // keep the caret solid while actively typing
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
    [_blinkTimer invalidate];
    delete _term;
    delete _pty;
}

@end
