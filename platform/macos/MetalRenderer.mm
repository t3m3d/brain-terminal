#import "MetalRenderer.h"
#import <Metal/Metal.h>
#include <unordered_map>
#include <vector>
#include <cmath>

#include "kterm/renderer/Grid.hpp"
#include "kterm/renderer/Cell.hpp"

// One vertex. Field order matches the shader's VIn (pos, uv, color) so that
// float4 color lands at a 16-byte-aligned offset with no hidden padding.
struct MVertex { float x, y, u, v, r, g, b, a; };

struct GlyphInfo { float u0, v0, u1, v1; float w, h; bool valid; };

static NSString* const kShaderSrc = @R"METAL(
#include <metal_stdlib>
using namespace metal;
struct VIn  { float2 pos; float2 uv; float4 color; };   // matches MVertex layout
struct VOut { float4 pos [[position]]; float4 color; float2 uv; };
vertex VOut v_main(uint vid [[vertex_id]],
                   constant VIn* verts [[buffer(0)]],
                   constant float2& vp [[buffer(1)]]) {
    VOut o;
    float2 p = verts[vid].pos;                 // points, top-left origin
    o.pos   = float4((p.x/vp.x)*2.0 - 1.0, 1.0 - (p.y/vp.y)*2.0, 0.0, 1.0);
    o.color = verts[vid].color;
    o.uv    = verts[vid].uv;
    return o;
}
fragment float4 f_main(VOut in [[stage_in]],
                       texture2d<float> atlas [[texture(0)]],
                       sampler s [[sampler(0)]]) {
    if (in.uv.x < 0.0) return in.color;        // solid quad (bg/cursor/selection)
    float cov = atlas.sample(s, in.uv).r;      // glyph coverage from alpha atlas
    return float4(in.color.rgb, in.color.a * cov);
}
)METAL";

@implementation MetalRenderer {
    id<MTLDevice>              _device;
    id<MTLCommandQueue>        _queue;
    id<MTLRenderPipelineState> _pipe;
    id<MTLSamplerState>        _sampler;
    id<MTLTexture>             _atlas;
    id<MTLBuffer>              _vbuf;

    NSFont* _font; NSFont* _bold; NSFont* _italic; NSFont* _boldItalic;
    CGFloat _cellW, _cellH, _scale;

    int _atlasDim, _tileW, _tileH, _atlasX, _atlasY;
    std::unordered_map<uint64_t, GlyphInfo>* _glyphs;
    std::vector<MVertex>* _verts;
    BOOL _ready;
}

- (instancetype)initWithFont:(NSFont*)font bold:(NSFont*)bold italic:(NSFont*)italic
                  boldItalic:(NSFont*)boldItalic cellW:(CGFloat)cellW cellH:(CGFloat)cellH
                       scale:(CGFloat)scale {
    self = [super init];
    if (!self) return nil;
    _font = font; _bold = bold; _italic = italic; _boldItalic = boldItalic;
    _cellW = cellW; _cellH = cellH; _scale = scale > 0 ? scale : 1.0;
    _glyphs = new std::unordered_map<uint64_t, GlyphInfo>();
    _verts  = new std::vector<MVertex>();

    _device = MTLCreateSystemDefaultDevice();
    if (!_device) return self;       // _ready stays NO -> caller falls back to CPU
    _queue = [_device newCommandQueue];

    NSError* err = nil;
    id<MTLLibrary> lib = [_device newLibraryWithSource:kShaderSrc options:nil error:&err];
    if (!lib) { NSLog(@"kterm metal: shader compile failed: %@", err); return self; }

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction   = [lib newFunctionWithName:@"v_main"];
    pd.fragmentFunction = [lib newFunctionWithName:@"f_main"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor      = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    _pipe = [_device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!_pipe) { NSLog(@"kterm metal: pipeline failed: %@", err); return self; }

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    _sampler = [_device newSamplerStateWithDescriptor:sd];

    // Glyph atlas (single-channel coverage). Tiles up to 2 cells wide.
    _atlasDim = 2048;
    _tileW = (int)ceil(_cellW * 2.0 * _scale);
    _tileH = (int)ceil(_cellH * _scale);
    _atlasX = 0; _atlasY = 0;
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                           width:_atlasDim height:_atlasDim mipmapped:NO];
    _atlas = [_device newTextureWithDescriptor:td];

    _ready = YES;
    return self;
}

- (BOOL)ready { return _ready; }
- (id<MTLDevice>)mtlDevice { return _device; }

- (void)dealloc { delete _glyphs; delete _verts; }

// Rasterize a glyph into the atlas (once), returning its UV rect.
- (GlyphInfo)glyphFor:(uint32_t)cp variant:(int)variant {
    uint64_t key = (uint64_t)cp | ((uint64_t)variant << 40);
    auto it = _glyphs->find(key);
    if (it != _glyphs->end()) return it->second;

    GlyphInfo gi = {0,0,0,0,0,0,false};
    if (_atlasY + _tileH > _atlasDim) { (*_glyphs)[key] = gi; return gi; }   // atlas full

    NSFont* f = _font;
    if (variant == 3) f = _boldItalic; else if (variant == 1) f = _bold; else if (variant == 2) f = _italic;

    int W = _tileW, Hh = _tileH;
    std::vector<unsigned char> buf((size_t)W * Hh, 0);
    CGContextRef ctx = CGBitmapContextCreate(buf.data(), W, Hh, 8, W, NULL, (CGBitmapInfo)kCGImageAlphaOnly);
    if (!ctx) { (*_glyphs)[key] = gi; return gi; }
    CGContextScaleCTM(ctx, _scale, _scale);     // rasterize at backing scale

    NSGraphicsContext* g = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:YES];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:g];
    NSString* s = [[NSString alloc] initWithBytes:&cp length:sizeof(cp)
                                         encoding:NSUTF32LittleEndianStringEncoding];
    if (s) [s drawAtPoint:NSMakePoint(0, 0)
           withAttributes:@{ NSFontAttributeName: f,
                             NSForegroundColorAttributeName: [NSColor whiteColor] }];
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(ctx);

    [_atlas replaceRegion:MTLRegionMake2D(_atlasX, _atlasY, W, Hh)
              mipmapLevel:0 withBytes:buf.data() bytesPerRow:W];

    gi.u0 = (float)_atlasX / _atlasDim;
    gi.v0 = (float)_atlasY / _atlasDim;
    gi.u1 = (float)(_atlasX + W) / _atlasDim;
    gi.v1 = (float)(_atlasY + Hh) / _atlasDim;
    gi.w = W / _scale; gi.h = Hh / _scale; gi.valid = true;

    _atlasX += W;
    if (_atlasX + W > _atlasDim) { _atlasX = 0; _atlasY += Hh; }

    (*_glyphs)[key] = gi;
    return gi;
}

- (void)addSolidX:(float)x y:(float)y w:(float)w h:(float)h
                r:(float)r g:(float)gg b:(float)b a:(float)a {
    MVertex v0{x,y,-1,-1,r,gg,b,a}, v1{x+w,y,-1,-1,r,gg,b,a}, v2{x,y+h,-1,-1,r,gg,b,a};
    MVertex v3{x+w,y,-1,-1,r,gg,b,a}, v4{x+w,y+h,-1,-1,r,gg,b,a}, v5{x,y+h,-1,-1,r,gg,b,a};
    _verts->insert(_verts->end(), {v0,v1,v2,v3,v4,v5});
}

- (void)addGlyph:(GlyphInfo)gi x:(float)x y:(float)y r:(float)r g:(float)gg b:(float)b {
    float w = gi.w, h = gi.h;
    // V flipped (top quad edge samples gi.v1): CoreGraphics rasters are bottom-up
    // in memory, Metal samples top-down, so the atlas tile is stored upside down.
    MVertex v0{x,y,gi.u0,gi.v1,r,gg,b,1}, v1{x+w,y,gi.u1,gi.v1,r,gg,b,1}, v2{x,y+h,gi.u0,gi.v0,r,gg,b,1};
    MVertex v3{x+w,y,gi.u1,gi.v1,r,gg,b,1}, v4{x+w,y+h,gi.u1,gi.v0,r,gg,b,1}, v5{x,y+h,gi.u0,gi.v0,r,gg,b,1};
    _verts->insert(_verts->end(), {v0,v1,v2,v3,v4,v5});
}

static inline void argb(uint32_t c, float* r, float* g, float* b, float* a) {
    *a = ((c>>24)&0xFF)/255.0f; *r = ((c>>16)&0xFF)/255.0f; *g = ((c>>8)&0xFF)/255.0f; *b = (c&0xFF)/255.0f;
}

- (void)renderTerminal:(kterm::core::Terminal*)term layer:(CAMetalLayer*)layer
                 viewW:(CGFloat)viewW viewH:(CGFloat)viewH scrollOffset:(int)scrollOffset
                  cols:(int)cols rows:(int)rows hasSelection:(BOOL)hasSelection
              selStart:(NSPoint)selStart selEnd:(NSPoint)selEnd caretOn:(BOOL)caretOn
             defaultFg:(NSColor*)defaultFg defaultBg:(NSColor*)defaultBg {
    if (!_ready || !term) return;
    _verts->clear();

    float dfR,dfG,dfB,dfA; { CGFloat r,g,b,a; [[defaultFg colorUsingColorSpace:NSColorSpace.sRGBColorSpace] getRed:&r green:&g blue:&b alpha:&a]; dfR=r;dfG=g;dfB=b;dfA=a; }
    float dbR,dbG,dbB,dbA; { CGFloat r,g,b,a; [[defaultBg colorUsingColorSpace:NSColorSpace.sRGBColorSpace] getRed:&r green:&g blue:&b alpha:&a]; dbR=r;dbG=g;dbB=b;dbA=a; }

    const auto& grid = term->grid();
    int H = grid.historyLines();
    int s = scrollOffset; if (s < 0) s = 0; if (s > H) s = H;
    const auto& live = grid.rows();
    int R = (int)live.size();

    for (int vr = 0; vr < R; ++vr) {
        int idx = (H - s) + vr;
        if (idx < 0) continue;
        const std::vector<kterm::renderer::Cell>* line;
        if (idx < H) line = &grid.historyRow(idx);
        else { int lr = idx - H; if (lr >= R) continue; line = &live[lr]; }

        float y = vr * (float)_cellH;
        for (int c = 0; c < (int)line->size(); ++c) {
            const kterm::renderer::Cell& cell = (*line)[c];
            float x = c * (float)_cellW;
            uint8_t at = cell.attrs;

            float fr=dfR,fg=dfG,fb=dfB; bool hasBg=false; float br=0,bg=0,bb=0;
            if (cell.fg != 0xFFFFFFFF) { float a; argb(cell.fg,&fr,&fg,&fb,&a); }
            if (((cell.bg>>24)&0xFF) != 0) { float a; argb(cell.bg,&br,&bg,&bb,&a); hasBg=true; }
            if (at & kterm::renderer::ATTR_INVERSE) {
                float tr=fr,tg=fg,tb=fb;
                fr = hasBg?br:dbR; fg = hasBg?bg:dbG; fb = hasBg?bb:dbB;
                br=tr; bg=tg; bb=tb; hasBg=true;
            }
            if (hasBg) [self addSolidX:x y:y w:(float)_cellW h:(float)_cellH r:br g:bg b:bb a:1.0];

            uint32_t cp = cell.ch;
            if (cp != ' ' && cp != 0) {
                int variant = ((at&kterm::renderer::ATTR_BOLD)&&(at&kterm::renderer::ATTR_ITALIC))?3
                            : (at&kterm::renderer::ATTR_BOLD)?1 : (at&kterm::renderer::ATTR_ITALIC)?2 : 0;
                GlyphInfo gi = [self glyphFor:cp variant:variant];
                if (gi.valid) [self addGlyph:gi x:x y:y r:fr g:fg b:fb];
            }
        }
    }

    // Selection overlay (translucent).
    if (hasSelection) {
        int sr=(int)selStart.y, sc=(int)selStart.x, er=(int)selEnd.y, ec=(int)selEnd.x;
        if (er<sr || (er==sr&&ec<sc)) { int t; t=sr;sr=er;er=t; t=sc;sc=ec;ec=t; }
        for (int vr=sr; vr<=er && vr<R; ++vr) {
            int c0=(vr==sr)?sc:0, c1=(vr==er)?ec:cols;
            if (c1>c0) [self addSolidX:c0*_cellW y:vr*_cellH w:(c1-c0)*_cellW h:_cellH r:0.30 g:0.50 b:0.90 a:0.35];
        }
    }

    // Caret.
    if (s == 0 && caretOn && term->cursorVisible()) {
        int cr = grid.cursorRow(), cc = grid.cursorCol();
        [self addSolidX:cc*_cellW y:cr*_cellH w:_cellW h:_cellH r:0.55 g:0.78 b:1.0 a:0.55];
    }

    // Encode + present.
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(dbR, dbG, dbB, 1.0);

    id<MTLCommandBuffer> cb = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    if (!_verts->empty()) {
        // setVertexBytes is capped at 4 KB; our geometry is much larger, so it
        // must go through an MTLBuffer.
        size_t bytes = _verts->size() * sizeof(MVertex);
        if (!_vbuf || _vbuf.length < bytes)
            _vbuf = [_device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        memcpy(_vbuf.contents, _verts->data(), bytes);

        [enc setRenderPipelineState:_pipe];
        [enc setVertexBuffer:_vbuf offset:0 atIndex:0];
        float vp[2] = { (float)viewW, (float)viewH };
        [enc setVertexBytes:vp length:sizeof(vp) atIndex:1];
        [enc setFragmentTexture:_atlas atIndex:0];
        [enc setFragmentSamplerState:_sampler atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_verts->size()];
    }
    [enc endEncoding];
    [cb presentDrawable:drawable];
    [cb commit];
}
@end
