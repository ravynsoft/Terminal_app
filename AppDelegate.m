/*
 * Copyright (C) 2022-2024 Zoe Knox <zoe@pixin.net>
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

    NSRect visible = [[NSScreen mainScreen] visibleFrame];
    frame.origin.x = visible.size.width / 2 - frame.size.width / 2;
    frame.origin.y = visible.size.height - frame.size.height - 100;

    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"Terminal"];

    // Create some menus
    NSMenu *mainMenu = [NSMenu new];
    [mainMenu setDelegate:self];
    [mainMenu setAutoenablesItems:YES];

    NSMenu *windows = [NSMenu new];
    [windows setValue:@"_NSWindowsMenu" forKey:@"name"];
    [windows setDelegate:self];

    NSMenuItem *item = [mainMenu addItemWithTitle:@"Window" action:NULL keyEquivalent:@""];
    [item setTarget:self];
    [item setSubmenu:windows];

    [NSApp setMenu:mainMenu];
    [NSApp addWindowsItem:_window title:[_window title] filename:NO];

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

- (void)setPTY:(int)pty {
    [_view setPTY:pty];
}

- (NSSize)terminalSize {
    return [_view terminalSize];
}

- (void)selectInputSource:(NSSelectInputSource *)inputSource selectEvent:(NSUInteger)selectEvent {
    [_view handlePTYInput];
}

@end

