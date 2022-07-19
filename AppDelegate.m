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

#import <Foundation/NSSelectInputSource.h>
#import <Foundation/NSSocket.h>
#import "AppDelegate.h"
#import "tmt.h"

void TMTCallback(tmt_msg_t m, TMT *vt, const void *arg, void *p) {
    switch(m) {
        case TMT_MSG_BELL: // ring the terminal bell
            NSLog(@"beep beep");
            break;
        case TMT_MSG_UPDATE: // virtual screen has been updated
        {
            NSMutableString *str = [NSMutableString new];
            const TMTSCREEN *screen = tmt_screen(vt);
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

            [[NSApp delegate] updateView:str];
            tmt_clean(vt);
            break;
        }
        case TMT_MSG_ANSWER:
            NSLog(@"terminal answered %s", (const char *)arg);
            break;
        case TMT_MSG_MOVED:
        case TMT_MSG_CURSOR:
        {
            const TMTPOINT *curs = tmt_cursor(vt);
            NSLog(@"cursor moved to %zd,%zd", curs->r, curs->c);
            break;
        }
    }
}

@implementation AppDelegate
- (AppDelegate *)init {
    // terminal font
    _font = [NSFont userFixedPitchFontOfSize:12.0];

    NSAttributedString *as = [[NSAttributedString alloc]
        initWithString:@"01234567890123456789012345678901234567890123456789012345678901234567890123456789"
        attributes:[NSDictionary dictionaryWithObjects:@[_font] forKeys:@[NSFontAttributeName]]];
    _fontSize = [as size];

    // terminal window and view
    NSRect frame = NSMakeRect(0,0,_fontSize.width,25*_fontSize.height);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"Terminal"];
    _view = [[NSTextView alloc] initWithFrame:frame];
    [_view setSelectable:NO];
    [[_window contentView] addSubview:_view];
    [_window makeKeyAndOrderFront:self];

    // virtual terminal screen buffer
    _tmt = tmt_open(25, 80, TMTCallback, NULL, NULL);
    if(!_tmt)
        return nil;

    return self;
}

- (void)dealloc {
    if(_tmt)
        tmt_close(_tmt);
}

// called _every_ update of virtual screen so keep this efficient! (it is currently not)
- (void)updateView:(NSString *)s {
    [_view selectAll:nil];
    [_view insertText:s];
    [_view setFont:_font];
    [_view setNeedsDisplay:YES];
}

- (void)setSize:(NSSize)size {
    NSRect frame = NSZeroRect;
    frame.size = size;
    [_window setFrame:frame display:YES];
    [_view setFrame:[_window contentRectForFrameRect:frame]];
}

// called on _every_ pty input so keep this efficient!
- (void)selectInputSource:(NSSelectInputSource *)inputSource selectEvent:(NSUInteger)selectEvent {
    if(!_handle)
        _handle = [[NSFileHandle alloc] initWithFileDescriptor:[[inputSource socket] fileDescriptor]];

    NSData *data = [_handle availableData];
    tmt_write(_tmt, [data bytes], [data length]);
}

@end

