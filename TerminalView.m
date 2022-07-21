/*
 * Copyright (C) 2022 Zoe Knox <zoe@pixin.net>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#import "TerminalView.h"

NSString * const PREFS_TERM_SIZE = @"TerminalSize";
NSString * const PREFS_TERM_FONT_NAME = @"FontName";
NSString * const PREFS_TERM_FONT_SIZE = @"FontSize";
NSString * const PREFS_FG_COLOR = @"ForegroundColor";
NSString * const PREFS_BG_COLOR = @"BackgroundColor";


static void TMTCallback(tmt_msg_t m, TMT *vt, const void *arg, void *p) {
    const TMTPOINT *curs = tmt_cursor(vt);

    // when the virtual terminal is updated, we basically want to force a
    // redraw of the view rect so that any changes get rendered
    switch(m) {
        case TMT_MSG_BELL: // ring the terminal bell
            NSLog(@"beep beep");
            break;
        case TMT_MSG_UPDATE:
            NSLog(@"vt was updated");
            [(__bridge TerminalView *)p setNeedsDisplay:YES];
            break;
        case TMT_MSG_ANSWER:
            NSLog(@"terminal answered %s", (const char *)arg);
            break;
        case TMT_MSG_MOVED:
        case TMT_MSG_CURSOR:
            NSLog(@"cursor moved to %zd,%zd", curs->r, curs->c);
            [(__bridge TerminalView *)p setNeedsDisplay:YES];
            break;
    }
}

static NSColor *colorWithHexRGBA(uint32_t hex) {
    CGFloat r = ((hex & 0xFF000000) >> 24) / 255.0;
    CGFloat g = ((hex & 0xFF0000) >> 16) / 255.0;
    CGFloat b = ((hex & 0xFF00) >> 8) / 255.0;
    CGFloat a = (hex & 0xFF) / 255.0;
    return [NSColor colorWithDeviceRed:r green:g blue:b alpha:a];
}

static NSColor *colorWithHexRGB(uint32_t hex) {
    hex = (hex << 8) | 0xFF; // fully opaque
    return colorWithHexRGBA(hex);
}

// translate single octet to a 0..1 float value
static CGFloat hexToFloat(unsigned char hex) {
    return hex / 255.0;
}

@implementation TerminalView
- (TerminalView *)init {
    _prefs = [NSUserDefaults standardUserDefaults];
    NSString *s = [_prefs objectForKey:PREFS_TERM_SIZE]; 
    _termSize = NSSizeFromString(s != nil ? s : @"{80,25}");
    [_prefs setObject:NSStringFromSize(_termSize) forKey:PREFS_TERM_SIZE];

    // virtual terminal screen buffer
    _tmt = tmt_open(_termSize.height, _termSize.width, TMTCallback, (__bridge void *)self, NULL);
    if(!_tmt)
        return nil;

    s = [_prefs objectForKey:PREFS_TERM_FONT_NAME];
    if(!s)
        s = @"DejaVu Sans Mono-Book";
    float pointsize = [_prefs floatForKey:PREFS_TERM_FONT_SIZE];
    if(pointsize < 2.0)
        pointsize = 12.0;
    [_prefs setObject:s forKey:PREFS_TERM_FONT_NAME];
    [_prefs setFloat:pointsize forKey:PREFS_TERM_FONT_SIZE];

    _font = [NSFont fontWithName:s size:pointsize];

    unsigned int i = 0;
    s = [_prefs objectForKey:PREFS_FG_COLOR];
    if(s && [s length] == 8)
        i = strtoul([s cString], NULL, 16);
    NSLog(@"s = %@, len = %d, fg int = %08x",s, [s length], i);
    if(i == 0)
        i = 0xFF; // fully opaque black
    NSColor *fgColor = colorWithHexRGBA(i);
    [_prefs setObject:[NSString stringWithFormat:@"%08X",i] forKey:PREFS_FG_COLOR];

    _attr = [NSDictionary dictionaryWithObjects:@[_font, fgColor]
        forKeys:@[NSFontAttributeName,NSForegroundColorAttributeName]];

    s = [_prefs objectForKey:PREFS_BG_COLOR]; 
    if(s && [s length] == 8)
        i = strtoul([s cString], NULL, 16);
    NSLog(@"bg int = %08x", i);
    if(i == 0)
        i = 0xFAFCF5F0;
    _bgColor = colorWithHexRGBA(i);
    [_prefs setObject:[NSString stringWithFormat:@"%08X",i] forKey:PREFS_BG_COLOR];

    NSAttributedString *as = [[NSAttributedString alloc] initWithString:@"M" attributes:_attr];
    _fontSize = [as size];

    [_prefs synchronize];
    NSRect frame = NSMakeRect(0,0,_termSize.width*_fontSize.width,_termSize.height*_fontSize.height);
    return [self initWithFrame:frame];
}

- (void)dealloc {
    if(_tmt)
        tmt_close(_tmt);
}

- (void)drawRect:(NSRect)dirtyRect {
    [_bgColor set];
    [NSBezierPath fillRect:dirtyRect];

    const TMTSCREEN *screen = tmt_screen(_tmt);
    const TMTPOINT *curs = tmt_cursor(_tmt);

    NSMutableString *str = [NSMutableString new];
    char buffer[screen->ncol + 2];
    for(size_t row = 0; row < screen->nline; ++row) {
    //    if(screen->lines[row]->dirty) {
            for(size_t col = 0; col < screen->ncol; ++col) {
                buffer[col] = screen->lines[row]->chars[col].c;
            }
            if(row < (screen->nline - 1)) {
                buffer[screen->ncol] = '\n';
                buffer[screen->ncol+1] = 0;
            } else 
                buffer[screen->ncol] = 0;
            [str appendString:[NSString stringWithUTF8String:buffer]];
    //    }
    }

    NSAttributedString *as = [[NSAttributedString alloc] initWithString:str attributes:_attr];
    [as drawInRect:[self frame]];

    tmt_clean(_tmt);
}

- (void)setFrame:(NSRect)frame {
    [super setFrame:frame];
//    tmt_resize(...);
}

- (void)handlePTYInput:(NSData *)data {
    tmt_write(_tmt, [data bytes], [data length]);
}

@end

