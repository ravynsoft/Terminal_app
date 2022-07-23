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

#include <unistd.h>
#import "TerminalView.h"

NSString * const PREFS_TERM_SIZE = @"TerminalSize";
NSString * const PREFS_TERM_FONT_NAME = @"FontName";
NSString * const PREFS_TERM_FONT_SIZE = @"FontSize";
NSString * const PREFS_FG_COLOR = @"ForegroundColor";
NSString * const PREFS_BG_COLOR = @"BackgroundColor";
NSString * const PREFS_CURSOR_COLOR = @"CursorColor";

BOOL ready = NO;

static void TMTCallback(tmt_msg_t m, TMT *vt, const void *arg, void *p) {
    const TMTPOINT *curs = tmt_cursor(vt);

    // when the virtual terminal is updated, we basically want to force a
    // redraw of the view rect so that any changes get rendered
    switch(m) {
        case TMT_MSG_BELL: // ring the terminal bell
            NSLog(@"beep beep");
            break;
        case TMT_MSG_ANSWER:
            NSLog(@"terminal answered %s", (const char *)arg);
            break;
        case TMT_MSG_UPDATE:
        case TMT_MSG_MOVED:
        case TMT_MSG_CURSOR:
            [(__bridge TerminalView *)p updateScreen];
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
    _screenCtx = NULL;
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
    if(i == 0)
        i = 0xFF; // fully opaque black
    _fgColor = colorWithHexRGBA(i);
    [_prefs setObject:[NSString stringWithFormat:@"%08X",i] forKey:PREFS_FG_COLOR];

    i = 0;
    s = [_prefs objectForKey:PREFS_BG_COLOR]; 
    if(s && [s length] == 8)
        i = strtoul([s cString], NULL, 16);
    if(i == 0)
        i = 0xFAFCF5F0;
    _bgColor = colorWithHexRGBA(i);
    [_prefs setObject:[NSString stringWithFormat:@"%08X",i] forKey:PREFS_BG_COLOR];

    _attr = [NSDictionary dictionaryWithObjects:@[_font, _fgColor, _bgColor]
        forKeys:@[NSFontAttributeName,NSForegroundColorAttributeName,
        NSBackgroundColorAttributeName]];
    NSAttributedString *as = [[NSAttributedString alloc] initWithString:@"M" attributes:_attr];
    _fontSize = [as size];

    i = 0;
    s = [_prefs objectForKey:PREFS_CURSOR_COLOR];
    if(s && [s length] == 8)
        i = strtoul([s cString], NULL, 16);
    if(i == 0)
        i = 0x333333FF; // fully opaque dark gray
    _cursorColor = colorWithHexRGBA(i);
    [_prefs setObject:[NSString stringWithFormat:@"%08X",i] forKey:PREFS_CURSOR_COLOR];

    [_prefs synchronize];

    ansi[TMT_COLOR_BLACK] = [NSColor blackColor];
    ansi[TMT_COLOR_RED] = [NSColor redColor];
    ansi[TMT_COLOR_GREEN] = [NSColor greenColor];
    ansi[TMT_COLOR_YELLOW] = [NSColor yellowColor];
    ansi[TMT_COLOR_BLUE] = [NSColor blueColor];
    ansi[TMT_COLOR_MAGENTA] = [NSColor magentaColor];
    ansi[TMT_COLOR_CYAN] = [NSColor cyanColor];
    ansi[TMT_COLOR_WHITE] = [NSColor whiteColor];

    NSRect frame = NSMakeRect(0,0,_termSize.width*_fontSize.width,_termSize.height*_fontSize.height);
    return [self initWithFrame:frame];
}

- (void)dealloc {
    ready = NO; // stop any callbacks
    if(_tmt)
        tmt_close(_tmt);
    if(_screenCtx)
        CGContextRelease(_screenCtx);
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void)updateScreen {
    if(!ready)
        return;

    if(!_screenCtx) {
        _cgColorSpace = CGColorSpaceCreateDeviceRGB();
        _screenCtx = CGBitmapContextCreate(NULL, _frame.size.width, _frame.size.height,
            8, 0, _cgColorSpace, kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Little);
    }

    const TMTPOINT *curs = tmt_cursor(_tmt);
    const TMTSCREEN *screen = tmt_screen(_tmt);

    NSGraphicsContext *ctx = [NSGraphicsContext
        graphicsContextWithGraphicsPort:_screenCtx flipped:NO];
    NSGraphicsContext *current = [NSGraphicsContext currentContext];
    [NSGraphicsContext setCurrentContext:ctx];

    NSMutableDictionary *attrs = [NSMutableDictionary new];
    [attrs setDictionary:_attr];

    // render the screen
    char buffer[screen->ncol + 1];
    for(size_t row = 0; row < screen->nline; ++row) {
        if(screen->lines[row]->dirty) {
            for(size_t col = 0; col < screen->ncol; ++col) {
                buffer[col] = screen->lines[row]->chars[col].c;
            }
            buffer[screen->ncol] = 0;
            
            NSString *str = [[NSString alloc] initWithUTF8String:buffer];
            NSMutableAttributedString *as = [[NSMutableAttributedString alloc]
                initWithString:str attributes:attrs];
            for(size_t col = 0; col < screen->ncol; ++col) {
                int fg = screen->lines[row]->chars[col].a.fg;
                int bg = screen->lines[row]->chars[col].a.bg;
                [attrs setObject:(fg > 0 && fg < TMT_COLOR_MAX) ? ansi[fg] : _fgColor
                    forKey:NSForegroundColorAttributeName];
                [attrs setObject:(bg > 0 && bg < TMT_COLOR_MAX) ? ansi[bg] : _bgColor
                    forKey:NSBackgroundColorAttributeName];
                [as setAttributes:attrs range:NSMakeRange(col,1)];
            }
            NSRect lineRect = NSMakeRect(0, _frame.size.height - ((1 + row) * _fontSize.height),
                _frame.size.width, _fontSize.height);
            [as drawInRect:lineRect];
        }
    }

    // draw the cursor, remembering our coords are inverted to the terminal's
    NSRect cursor = NSZeroRect;
    cursor.origin.x = curs->c * _fontSize.width;
    cursor.origin.y = _frame.size.height - ((1 + curs->r) * _fontSize.height);
    cursor.size = _fontSize;
    [_cursorColor set]; 
    [NSBezierPath fillRect:cursor];

    [NSGraphicsContext setCurrentContext:current];
    tmt_clean(_tmt);
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    if(!_screenCtx) {
        return;
    }

    CGImageRef image = CGBitmapContextCreateImage(_screenCtx);
    CGContextDrawImage([[NSGraphicsContext currentContext] graphicsPort], _frame, image);
    CGImageRelease(image);
}

- (void)setFrame:(NSRect)frame {
    [super setFrame:frame];
//    tmt_resize(...);
}

// called on _every_ pty input so keep this efficient!
- (void)handlePTYInput {
    static char buf[16384];
    int bytes = read(_pty, buf, sizeof(buf));
    if(bytes)
        tmt_write(_tmt, buf, bytes);
}

- (void)keyDown:(NSEvent *)event {
    const char *s = [[event characters] cString];
    int len = [[event characters] length];
    // FIXME: handle key repeat if held down
    write(_pty, s, len);
}

- (void)keyUp:(NSEvent *)event {
}

- (void)setPTY:(int)pty {
    _pty = pty;
    ready = YES;
}

@end

