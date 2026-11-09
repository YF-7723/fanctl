#include <QWindow>

#import <Cocoa/Cocoa.h>

static void (*g_trayCallback)(int) = nullptr;
static NSStatusItem *g_statusItem = nil;

// winId() 在 macOS 上返回的是 NSView*
static NSWindow *nsWindowOf(QWindow *window) {
    if (!window) return nil;
    NSView *view = (__bridge NSView *)(void *)window->winId();
    return view ? [view window] : nil;
}

void macHideTitlebar(QWindow *window) {
    NSWindow *ns = nsWindowOf(window);
    if (!ns) return;
    ns.titlebarAppearsTransparent = YES;
    ns.titleVisibility = NSWindowTitleHidden;
    ns.styleMask |= NSWindowStyleMaskFullSizeContentView;

    // 固定尺寸：去掉可缩放位（绿按钮变灰、全屏入口一并失效）
    ns.styleMask &= ~NSWindowStyleMaskResizable;
    ns.collectionBehavior &= ~NSWindowCollectionBehaviorFullScreenPrimary;
    ns.collectionBehavior &= ~NSWindowCollectionBehaviorFullScreenAuxiliary;
    ns.movableByWindowBackground = NO;
}

void macSetAppearance(QWindow *window, bool dark) {
    NSWindow *ns = nsWindowOf(window);
    if (!ns) return;
    NSString *name = dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua;
    ns.appearance = [NSAppearance appearanceNamed:name];
}

bool macStartDrag(QWindow *window) {
    NSWindow *ns = nsWindowOf(window);
    if (!ns) return false;

    NSEvent *event = [NSApp currentEvent];
    const bool isMouseDown = event && event.type == NSEventTypeLeftMouseDown;

    if (!ns.isKeyWindow || ![NSApp isActive]) {
        [NSApp activateIgnoringOtherApps:YES];
        [ns makeKeyAndOrderFront:nil];
    }

    if (isMouseDown) {
        [ns performWindowDragWithEvent:event];
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- 菜单栏图标

@interface FanctlTrayTarget : NSObject
- (void)onItem:(NSMenuItem *)item;
@end

@implementation FanctlTrayTarget
- (void)onItem:(NSMenuItem *)item {
    if (g_trayCallback) g_trayCallback((int)item.tag);
}
@end

static FanctlTrayTarget *g_trayTarget = nil;

void macInstallStatusItem(void (*cb)(int)) {
    g_trayCallback = cb;
    if (g_statusItem) return;

    g_statusItem = [[NSStatusBar systemStatusBar]
        statusItemWithLength:NSVariableStatusItemLength];
    g_statusItem.button.title = @"自动";
    g_statusItem.button.font = [NSFont monospacedDigitSystemFontOfSize:11
                                                              weight:NSFontWeightMedium];

    NSMenu *menu = [[NSMenu alloc] init];
    g_trayTarget = [[FanctlTrayTarget alloc] init];

    NSMenuItem *show = [[NSMenuItem alloc] initWithTitle:@"显示主窗口"
                                                  action:@selector(onItem:)
                                           keyEquivalent:@""];
    show.target = g_trayTarget;
    show.tag = 0;
    [menu addItem:show];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *autoItem = [[NSMenuItem alloc] initWithTitle:@"自动控制"
                                                      action:@selector(onItem:)
                                               keyEquivalent:@""];
    autoItem.target = g_trayTarget;
    autoItem.tag = 1;
    [menu addItem:autoItem];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"退出 FanCtl"
                                                  action:@selector(onItem:)
                                           keyEquivalent:@""];
    quit.target = g_trayTarget;
    quit.tag = 4;
    [menu addItem:quit];

    g_statusItem.menu = menu;
}

void macSetStatusTitle(const char *utf8) {
    if (!g_statusItem || !utf8) return;
    NSString *title = [NSString stringWithUTF8String:utf8];
    if (title && ![g_statusItem.button.title isEqualToString:title])
        g_statusItem.button.title = title;
}
