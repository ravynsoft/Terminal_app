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

@implementation AppDelegate
- (AppDelegate *)init {
    // terminal window and view
    _view = [TerminalView new];
    NSRect frame = [_view frame];

    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"Terminal"];

    // allow terminal to be transparent :)
    [_window setBackgroundColor:[NSColor colorWithDeviceRed:1. green:1. blue:1. alpha:0]];

    [[_window contentView] addSubview:_view];
    [_window makeKeyAndOrderFront:self];

    return self;
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

    [_view handlePTYInput:[_handle availableData]];
}

@end

