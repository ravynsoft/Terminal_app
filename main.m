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

#import <Foundation/Foundation.h>
#import <Foundation/NSSocket_bsd.h>
#import <Foundation/NSSelectInputSource.h>
#import <AppKit/AppKit.h>
#import "AppDelegate.h"

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <libutil.h>
#include <paths.h>

extern char *const *environ;

static void __attribute__((noreturn)) runShell() {
    char **argv = (char*[]){
        getenv("SHELL") ? : _PATH_BSHELL,
        "-il",
        NULL
    };

    setenv("TERM", "ansi", 1);
    execve(argv[0], argv, environ);
    exit(1);
}

int main(int argc, const char *argv[]) {
    __NSInitializeProcess(argc, argv);

    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    [NSApplication sharedApplication];

    AppDelegate *del = [AppDelegate new];
    if(!del)
        exit(EXIT_FAILURE);
    [NSApp setDelegate:del];

    struct winsize ws = {.ws_row = 25, .ws_col = 80};
    int pty;

    pid_t pid = forkpty(&pty, NULL, NULL, &ws);
    if(pid < 0)
        return -1;
    else if(pid == 0) {
        setsid();
        signal(SIGCHLD, SIG_DFL);
        runShell();
        return -1;
    }

    NSSelectInputSource *inputSource = [NSSelectInputSource 
        socketInputSourceWithSocket:[NSSocket_bsd socketWithDescriptor:pty]];
    [inputSource setDelegate:del];
    [inputSource setSelectEventMask:NSSelectReadEvent];
    [[NSRunLoop mainRunLoop] addInputSource:inputSource forMode:NSDefaultRunLoopMode];

    [pool drain];
    [NSApp run];
    return 0;
}
