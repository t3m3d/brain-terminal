#import "TermView.h"
#import "MetalRenderer.h"
#import <QuartzCore/QuartzCore.h>
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
    NSFont*    _fallbackFont;   // Nerd Font for glyphs the body font lacks (prompt icons)
    NSColor*   _defaultBg;
    NSColor*   _defaultFg;
    NSColor*   _cursorColor;
    double     _opacity;        // 0..1; < 1 => translucent background
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
    BOOL       _metal;             // BRAIN_RENDERER=metal
    CAMetalLayer* _metalLayer;
    MetalRenderer* _renderer;
    CADisplayLink* _displayLink;   // coalesces Metal renders to the refresh rate
    BOOL       _needsRender;
    std::string _shell;
}

- (instancetype)initWithFrame:(NSRect)frame
                        shell:(NSString*)shell
                       config:(BrainConfig*)config {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    CGFloat fontSize = config.fontSize;
    _font = [NSFont fontWithName:config.fontName size:fontSize];
    if (!_font) _font = [NSFont monospacedSystemFontOfSize:fontSize weight:NSFontWeightRegular];
    NSFontManager* fm = [NSFontManager sharedFontManager];
    _fontBold       = [fm convertFont:_font toHaveTrait:NSBoldFontMask];
    _fontItalic     = [fm convertFont:_font toHaveTrait:NSItalicFontMask];
    _fontBoldItalic = [fm convertFont:_fontBold toHaveTrait:NSItalicFontMask];
    // Icon fallback: always a Nerd Font so prompt glyphs survive a font change.
    _fallbackFont   = [NSFont fontWithName:@"JetBrainsMono Nerd Font Mono" size:fontSize];

    _opacity     = config.opacity;
    _cursorColor = config.cursorColor;
    _defaultFg   = config.foreground;
    // Fold opacity into the background alpha so empty cells render translucent.
    _defaultBg   = [config.background colorWithAlphaComponent:_opacity];

    // Monospace cell metrics.
    NSDictionary* attrs = @{ NSFontAttributeName: _font };
    _cellW = [@"M" sizeWithAttributes:attrs].width;
    _cellH = ceil(_font.ascender - _font.descender + _font.leading);
    if (_cellW < 1) _cellW = fontSize * 0.6;
    if (_cellH < 1) _cellH = fontSize * 1.2;

    _shell = std::string([shell UTF8String]);
    const char* rend = getenv("BRAIN_RENDERER");
    _metal = (rend && strcmp(rend, "metal") == 0);
    // config `renderer = metal|cpu` overrides the env/flag default.
    if ([config.renderer isEqualToString:@"metal"]) _metal = YES;
    else if ([config.renderer isEqualToString:@"cpu"]) _metal = NO;

    // CAMetalLayer has to be layer-hosting: assign self.layer before wantsLayer.
    // Going through makeBackingLayer lets AppKit clear it, giving a blank window.
    if (_metal) {
        _metalLayer = [CAMetalLayer layer];
        _metalLayer.device = MTLCreateSystemDefaultDevice();
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _metalLayer.framebufferOnly = YES;
        _metalLayer.opaque = (_opacity >= 1.0);   // translucent if opacity < 1
        self.layer = _metalLayer;
    }
    self.wantsLayer = YES;

    // Defer terminal/PTY creation until the view is in the window with its
    // real laid-out size (see viewDidMoveToWindow).
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

        // p10k instant-prompt caches the old terminal's width and only repaints
        // on SIGWINCH, so wobble the row count once to force a redraw.
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
                                             boldItalic:_fontBoldItalic fallback:_fallbackFont
                                                  cellW:_cellW cellH:_cellH scale:scale];
        if (![_renderer ready]) { NSLog(@"brain: Metal unavailable, using CPU"); _metal = NO; _renderer = nil; }
        else {
            _renderer.caretColor = _cursorColor;
            if (_metalLayer) _metalLayer.device = [_renderer mtlDevice];  // SAME device as the renderer
            if (@available(macOS 14.0, *)) {
                _displayLink = [self displayLinkWithTarget:self selector:@selector(displayLinkFired:)];
                [_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
            }
            _needsRender = YES;   // draw the first frame
        }
        [self updateDrawableSize];
    }
    // Spawn the shell once the view has its real on-screen size, so the PTY
    // gets the correct column count from the first prompt.
    if (!_term) [self rebuildTerminalForSize:self.bounds.size];
}

- (void)updateDrawableSize {
    if (!_metalLayer) return;
    CGFloat scale = self.window ? self.window.backingScaleFactor : 2.0;
    _metalLayer.contentsScale = scale;
    CGSize sz = self.bounds.size;
    _metalLayer.drawableSize = CGSizeMake(sz.width * scale, sz.height * scale);
}

// Metal: mark dirty and let the display link coalesce a burst of output into
// one frame. CPU: just invalidate.
- (void)refresh {
    if (_metal && _renderer) {
        if (_displayLink) _needsRender = YES;
        else [self renderMetal];          // no display link available: render now
    } else {
        [self setNeedsDisplay:YES];
    }
}

- (void)displayLinkFired:(CADisplayLink*)link {
    if (_needsRender) { _needsRender = NO; [self renderMetal]; }
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
- (BOOL)isOpaque  { return _opacity >= 1.0; } // translucent bg needs a non-opaque view
- (BOOL)acceptsFirstResponder { return YES; }

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self rebuildTerminalForSize:newSize];
    [self updateDrawableSize];
    [self refresh];
}

// Switch to a new font from the font panel: recompute metrics, rebuild the
// Metal atlas (cell size is baked into it), reflow the grid.
- (void)applyFont:(NSFont*)f {
    if (!f) return;
    _font = f;
    NSFontManager* fm = [NSFontManager sharedFontManager];
    _fontBold       = [fm convertFont:_font toHaveTrait:NSBoldFontMask];
    _fontItalic     = [fm convertFont:_font toHaveTrait:NSItalicFontMask];
    _fontBoldItalic = [fm convertFont:_fontBold toHaveTrait:NSItalicFontMask];
    _fallbackFont   = [NSFont fontWithName:@"JetBrainsMono Nerd Font Mono" size:_font.pointSize];

    NSDictionary* a = @{ NSFontAttributeName: _font };
    _cellW = [@"M" sizeWithAttributes:a].width;
    _cellH = ceil(_font.ascender - _font.descender + _font.leading);
    if (_cellW < 1) _cellW = _font.pointSize * 0.6;
    if (_cellH < 1) _cellH = _font.pointSize * 1.2;

    if (_metal) {
        CGFloat scale = self.window.backingScaleFactor ?: 1.0;
        _renderer = [[MetalRenderer alloc] initWithFont:_font bold:_fontBold italic:_fontItalic
                                             boldItalic:_fontBoldItalic fallback:_fallbackFont
                                                  cellW:_cellW cellH:_cellH scale:scale];
        if ([_renderer ready]) {
            _renderer.caretColor = _cursorColor;
            if (_metalLayer) _metalLayer.device = [_renderer mtlDevice];
        } else { _renderer = nil; _metal = NO; }
    }

    // resize() preserves cell content, so existing text just re-renders at the
    // new size instead of clearing.
    _cols = _rows = 0;
    [self rebuildTerminalForSize:self.bounds.size];
    [self updateDrawableSize];
    [self refresh];
}

// Sent down the responder chain by the macOS font panel.
- (void)changeFont:(id)sender {
    [self applyFont:[sender convertFont:_font]];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item { return YES; }

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

#pragma mark - Selection

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

// Strip clipboard content of anything that could break out of a paste and
// reach the shell as control input. Removes ESC (so an embedded ESC[201~ can't
// close bracketed paste early — the classic paste-jacking vuln) and all other
// C0 controls except tab/newline/carriage-return, which are legitimate text.
static std::string sanitizePaste(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c == '\x1b' || c == '\x7f') continue;          // ESC, DEL
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
        out.push_back((char)c);
    }
    return out;
}

- (void)pasteClipboard {
    if (!_pty) return;
    NSString* str = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (str.length == 0) return;
    const char* u = [str UTF8String];
    if (!u) return;
    std::string data = sanitizePaste(std::string(u));
    if (data.empty()) return;

    // An embedded newline (one not at the very end) means the paste holds more
    // than one command line and, without bracketed paste, would execute
    // immediately. Confirm first — defends against "copy this" web buttons that
    // smuggle multi-line payloads onto the clipboard.
    bool bracketed = _term && _term->bracketedPaste();
    size_t nl = data.find('\n');
    bool multiline = nl != std::string::npos && nl + 1 < data.size();
    if (multiline && !bracketed) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"Paste multiple lines?";
        a.informativeText = @"The clipboard contains more than one line. Each "
                            @"line may run as a separate command immediately.";
        [a addButtonWithTitle:@"Paste"];
        [a addButtonWithTitle:@"Cancel"];
        if ([a runModal] != NSAlertFirstButtonReturn) return;
    }

    // Bracketed paste: wrap so the shell treats it as literal text (newlines
    // don't auto-execute) when the app has requested it (ESC[?2004h). Safe to
    // wrap now that the payload can no longer contain ESC[201~.
    if (bracketed)
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
                // Fallback to the Nerd Font for glyphs the body font lacks.
                if (_fallbackFont && ![[f coveredCharacterSet] longCharacterIsMember:cp]
                                  &&  [[_fallbackFont coveredCharacterSet] longCharacterIsMember:cp])
                    f = _fallbackFont;

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
        [[_cursorColor colorWithAlphaComponent:0.55] set];
        NSRectFillUsingOperation(NSMakeRect(cc * _cellW, cr * _cellH, _cellW, _cellH),
                                 NSCompositingOperationSourceOver);
    }
}

#pragma mark - Keyboard

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
    [_displayLink invalidate];
    delete _term;
    delete _pty;
}

@end
