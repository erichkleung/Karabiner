#import "AXApplicationObserver.h"
#import "AXUtilities.h"
#import "NotificationKeys.h"

@interface AXApplicationObserver () {
  AXUIElementRef applicationElement_;
  AXUIElementRef focusedWindowElementForAXTitleChangedNotification_;
  AXObserverRef observer_;
}

@property NSString* title;
@property NSString* role;

- (void)updateTitle;
- (void)updateRole:(AXUIElementRef)element;

@end

static void
observerCallback(AXObserverRef observer, AXUIElementRef element, CFStringRef notification, void* refcon) {
  AXApplicationObserver* self = (__bridge AXApplicationObserver*)(refcon);
  if (!self) return;

  @synchronized(self) {
    if (CFStringCompare(notification, kAXTitleChangedNotification, 0) == kCFCompareEqualTo) {
      [self updateTitle];
      [self postNotification];
    }
    if (CFStringCompare(notification, kAXFocusedUIElementChangedNotification, 0) == kCFCompareEqualTo) {
      [self updateTitle];
      [self updateRole:element];
      [self postNotification];
    }
    if (CFStringCompare(notification, kAXFocusedWindowChangedNotification, 0) == kCFCompareEqualTo) {
      // ----------------------------------------
      // refresh observer.
      [self observeTitleChangedNotification];

      [self updateTitle];
      [self postNotification];
    }
  }
}

@implementation AXApplicationObserver

- (instancetype)initWithRunningApplication:(NSRunningApplication*)runningApplication {
  self = [super init];

  if (self) {
    self.runningApplication = runningApplication;
    self.title = @"";
    self.role = @"";

    // ----------------------------------------
    bool observable = YES;
    if (!AXIsProcessTrusted()) {
      observable = NO;
    }

    // We do not need to observe EventViewer because all events in EventViewer will be ignored in Karabiner.app.
    if ([[runningApplication bundleIdentifier] isEqualToString:@"org.pqrs.Karabiner.EventViewer"]) {
      observable = NO;
    }

    // Java apps will be crash if observe. (We confirm crash in SQLDeveloper.)
    if ([[[runningApplication executableURL] absoluteString] hasSuffix:@"/java"] ||
        [[[runningApplication executableURL] absoluteString] hasSuffix:@"/JavaApplicationStub"] ||
        [[[runningApplication executableURL] absoluteString] hasSuffix:@"/JavaAppLauncher"]) {
#if 0
      NSLog(@"Ignore Java app to avoid Java app's crash: %@", [runningApplication bundleIdentifier]);
#endif
      observable = NO;
    }

    // LibreOffice will be crash if observe.
    // https://github.com/tekezo/Karabiner/issues/243
    if ([[runningApplication bundleIdentifier] isEqualToString:@"org.libreoffice.script"]) {
      observable = NO;
    }

    // Matlab will be crash if observe.
    // https://github.com/tekezo/Karabiner/issues/259
    if ([[runningApplication bundleIdentifier] isEqualToString:@"com.mathworks.matlab"]) {
      observable = NO;
    }

    // ----------------------------------------
    if (observable) {
      pid_t pid = [self.runningApplication processIdentifier];

      // ----------------------------------------
      // Create applicationElement_

      applicationElement_ = AXUIElementCreateApplication(pid);
      if (!applicationElement_) {
        @throw [NSException exceptionWithName:@"AXApplicationObserverException"
                                       reason:@"AXUIElementCreateApplication is failed."
                                     userInfo:@{ @"runningApplication" : self.runningApplication }];
      }

      // ----------------------------------------
      // Create observer_

      {
        AXError error = AXObserverCreate(pid, observerCallback, &observer_);
        if (error != kAXErrorSuccess) {
          @throw [NSException exceptionWithName:@"AXApplicationObserverException"
                                         reason:@"AXObserverCreate is failed."
                                       userInfo:@{ @"runningApplication" : self.runningApplication,
                                                   @"error" : @(error) }];
        }
      }

      // ----------------------------------------
      // Observe notifications

      // AXObserverAddNotification might be failed when just application launched.
      if (![self observeAXNotification:applicationElement_ notification:kAXFocusedUIElementChangedNotification add:YES]) {
        @throw [NSException exceptionWithName:@"AXApplicationObserverException"
                                       reason:@"Failed to observe kAXFocusedUIElementChangedNotification."
                                     userInfo:@{ @"runningApplication" : self.runningApplication }];
      }
      if (![self observeAXNotification:applicationElement_ notification:kAXFocusedWindowChangedNotification add:YES]) {
        @throw [NSException exceptionWithName:@"AXApplicationObserverException"
                                       reason:@"Failed to observe kAXFocusedWindowChangedNotification."
                                     userInfo:@{ @"runningApplication" : self.runningApplication }];
      }

      [self updateTitle];
      [self updateRole:NULL];

      // ----------------------------------------
      CFRunLoopAddSource(CFRunLoopGetCurrent(),
                         AXObserverGetRunLoopSource(observer_),
                         kCFRunLoopDefaultMode);
    }
  }

  return self;
}

- (void)dealloc {
  if (observer_) {
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(),
                          AXObserverGetRunLoopSource(observer_),
                          kCFRunLoopDefaultMode);
    CFRelease(observer_);
    observer_ = NULL;
  }

  if (applicationElement_) {
    CFRelease(applicationElement_);
    applicationElement_ = NULL;
  }
}

- (BOOL)observeAXNotification:(AXUIElementRef)element notification:(CFStringRef)notification add:(BOOL)add {
  if (!element) return YES;
  if (!observer_) return YES;

  if (add) {
    AXError error = AXObserverAddNotification(observer_,
                                              element,
                                              notification,
                                              (__bridge void*)self);
    if (error != kAXErrorSuccess) {
      if (error == kAXErrorNotificationUnsupported ||
          error == kAXErrorNotificationAlreadyRegistered) {
        // We ignore this error.
        return YES;
      }
#if 0
      NSLog(@"AXObserverAddNotification is failed: error:%d %@", error, self.runningApplication);
#endif
      return NO;
    }

  } else {
    AXError error = AXObserverRemoveNotification(observer_,
                                                 element,
                                                 notification);
    if (error != kAXErrorSuccess) {
      // Note: Ignore kAXErrorInvalidUIElement because it is expected error when focused window is closed.
      if (error == kAXErrorInvalidUIElement ||
          error == kAXErrorNotificationNotRegistered) {
        // We ignore this error.
        return YES;
      }
#if 0
      NSLog(@"AXObserverRemoveNotification is failed: error:%d %@", error, self.runningApplication);
#endif
      return NO;
    }
  }

  return YES;
}

- (void)unobserveTitleChangedNotification {
  if (focusedWindowElementForAXTitleChangedNotification_) {
    [self observeAXNotification:focusedWindowElementForAXTitleChangedNotification_
                   notification:kAXTitleChangedNotification
                            add:NO];

    focusedWindowElementForAXTitleChangedNotification_ = NULL;
  }
}

- (void)observeTitleChangedNotification {
  if (!applicationElement_) return;

  [self unobserveTitleChangedNotification];

  focusedWindowElementForAXTitleChangedNotification_ = [AXUtilities copyFocusedWindow:applicationElement_];
  if (!focusedWindowElementForAXTitleChangedNotification_) return;

  [self observeAXNotification:focusedWindowElementForAXTitleChangedNotification_
                 notification:kAXTitleChangedNotification
                          add:YES];
}

- (void)updateTitle {
  self.title = @"";

  if (!applicationElement_) return;

  // Do not cache focusedWindowElement.
  // We need to get new focusedWindowElement because
  // getting title will be failed with cached focusedWindowElement on Finder.app.

  AXUIElementRef focusedWindowElement = [AXUtilities copyFocusedWindow:applicationElement_];
  if (focusedWindowElement) {
    NSString* title = [AXUtilities titleOfUIElement:focusedWindowElement];
    if (title) {
      self.title = title;
    }
    CFRelease(focusedWindowElement);
  }
}

- (void)updateRole:(AXUIElementRef)element {
  self.role = @"";

  if (element) {
    CFRetain(element);
  } else {
    element = [AXUtilities copyFocusedUIElement];
  }
  if (element) {
    NSString* role = [AXUtilities roleOfUIElement:element];
    if (role) {
      self.role = role;
    }
    CFRelease(element);
  }
}

- (void)postNotification {
  NSDictionary* userInfo = @{
    @"bundleIdentifier" : [self.runningApplication bundleIdentifier],
    @"title" : self.title,
    @"role" : self.role,
  };
  [[NSNotificationCenter defaultCenter] postNotificationName:kFocusedUIElementChanged object:self userInfo:userInfo];
}

@end
