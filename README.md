# ravynOS Terminal.app

This is a work in progress that replaces the Qt-based Terminal used in previous releases.
It has been written ground-up using Objective-C, AppKit, and CoreGraphics to fit seamlessly
into the new desktop environment. Although it is usable, there is still a lot to do!

![colorterm](https://user-images.githubusercontent.com/60144291/180630207-cbe566e7-e299-4238-9ec3-af11f8d1693b.png)


### Main features

* configurable background and foreground colors with alpha transparency
* configurable cursor color
* configurable font and font size
* configurable terminal size (rows x columns)
* basic PTY I/O with keyboard input and text rendering
* support for arrow and function keys
* ANSI color text

### Some of the major items left to add for v1.0 are

* menus
* prefs panel
* scroll buffer and scrollback
* copy & paste
* terminal bell
* further optimization
* key auto-repeat
* lots more!
