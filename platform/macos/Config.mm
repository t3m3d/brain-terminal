#import "Config.h"

// Parse "#RGB", "#RRGGBB", or "#RRGGBBAA" into an sRGB NSColor (nil on failure).
static NSColor* parseColor(NSString* s) {
    s = [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if (![s hasPrefix:@"#"]) return nil;
    NSString* hex = [s substringFromIndex:1];
    unsigned len = (unsigned)hex.length;
    if (len != 3 && len != 6 && len != 8) return nil;

    if (len == 3) {  // #RGB -> #RRGGBB
        unichar r = [hex characterAtIndex:0], g = [hex characterAtIndex:1], b = [hex characterAtIndex:2];
        hex = [NSString stringWithFormat:@"%C%C%C%C%C%C", r,r, g,g, b,b];
        len = 6;
    }
    unsigned int v = 0;
    if (![[NSScanner scannerWithString:hex] scanHexInt:&v]) return nil;

    CGFloat r, g, b, a = 1.0;
    if (len == 8) {
        r = ((v>>24)&0xFF)/255.0; g = ((v>>16)&0xFF)/255.0; b = ((v>>8)&0xFF)/255.0; a = (v&0xFF)/255.0;
    } else {
        r = ((v>>16)&0xFF)/255.0; g = ((v>>8)&0xFF)/255.0; b = (v&0xFF)/255.0;
    }
    return [NSColor colorWithSRGBRed:r green:g blue:b alpha:a];
}

@implementation BrainConfig

+ (instancetype)loadConfig {
    BrainConfig* c = [[BrainConfig alloc] init];
    // Built-in defaults (match the previous hardcoded look).
    c.fontName    = @"JetBrainsMono Nerd Font Mono";
    c.fontSize    = 13.0;
    c.foreground  = [NSColor colorWithSRGBRed:0.92 green:0.92 blue:0.96 alpha:1.0];
    c.background  = [NSColor colorWithSRGBRed:0.07 green:0.07 blue:0.09 alpha:1.0];
    c.cursorColor = [NSColor colorWithSRGBRed:0.55 green:0.78 blue:1.00 alpha:1.0];
    c.opacity     = 1.0;
    c.renderer    = nil;

    const char* envPath = getenv("BRAIN_CONFIG");
    NSString* path = envPath ? [NSString stringWithUTF8String:envPath]
                             : [NSHomeDirectory() stringByAppendingPathComponent:@".config/brain/config"];

    NSError* err = nil;
    NSString* text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!text) return c;   // no file -> defaults

    for (NSString* rawLine in [text componentsSeparatedByString:@"\n"]) {
        NSString* line = [rawLine stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if (line.length == 0 || [line hasPrefix:@"#"] || [line hasPrefix:@";"]) continue;

        NSRange eq = [line rangeOfString:@"="];
        if (eq.location == NSNotFound) continue;
        NSString* key = [[line substringToIndex:eq.location]
                            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        NSString* val = [[line substringFromIndex:eq.location + 1]
                            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        key = key.lowercaseString;

        if ([key isEqualToString:@"font"])                 c.fontName = val;
        else if ([key isEqualToString:@"font-size"])       c.fontSize = val.doubleValue;
        else if ([key isEqualToString:@"foreground"])    { NSColor* col = parseColor(val); if (col) c.foreground  = col; }
        else if ([key isEqualToString:@"background"])    { NSColor* col = parseColor(val); if (col) c.background  = col; }
        else if ([key isEqualToString:@"cursor"])        { NSColor* col = parseColor(val); if (col) c.cursorColor = col; }
        else if ([key isEqualToString:@"opacity"])         c.opacity  = MAX(0.0, MIN(1.0, val.doubleValue));
        else if ([key isEqualToString:@"renderer"])        c.renderer = val.lowercaseString;
    }
    return c;
}

@end
