#import <Cocoa/Cocoa.h>

// brain user configuration, loaded from ~/.config/brain/config (override the
// path with the BRAIN_CONFIG env var). Missing keys keep their defaults, so an
// empty or absent file reproduces the built-in look.
@interface BrainConfig : NSObject
@property (nonatomic, copy)   NSString* fontName;
@property (nonatomic)         CGFloat   fontSize;
@property (nonatomic, strong) NSColor*  foreground;
@property (nonatomic, strong) NSColor*  background;
@property (nonatomic, strong) NSColor*  cursorColor;
@property (nonatomic)         double    opacity;     // 0..1, applied to the background
@property (nonatomic, copy)   NSString* renderer;    // @"metal" / @"cpu" / nil = leave default

// Read the config file (if any) and return the resolved settings.
+ (instancetype)loadConfig;
@end
