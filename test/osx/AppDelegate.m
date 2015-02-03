//
//  AppDelegate.m
//
//  Created by abstephens on 1/21/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import <WebKit/WebKit.h>
#import <JavaScriptCore/JavaScriptCore.h>

#import <dlfcn.h>

#import "AppDelegate.h"
#import "AppProtocol.h"
#import "BufferT.h"

// Test functions are loaded from the app process using a set of strings
// encoded in the loaded HTML page. These functions take no arguments and return
// zero on success. They may use Halide runtime calls to log info, which are
// wired up to output in the WebView.
typedef int (*test_function_t)(void);

@interface AppDelegate ()
@property (retain) NSWindow *window;
@property (retain) WebView *outputView;
@end

@implementation AppDelegate

- (instancetype)init
{
  self = [super init];
  if (self) {
    _window = [[NSWindow alloc] init];
    _outputView = [[WebView alloc] init];
    _database = [[NSMutableDictionary alloc] init];
  }
  return self;
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification {

  // Setup the application protocol handler
  [NSURLProtocol registerClass:[AppProtocol class]];

  // Setup a very basic main menu
  NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
  [[NSApplication sharedApplication] setMainMenu:menu];

  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@""
                                                action:nil
                                         keyEquivalent:@""];

  NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

  [item setSubmenu:fileMenu];
  [menu addItem:item];

  [fileMenu addItemWithTitle:@"Quit"
                      action:@selector(terminate:)
               keyEquivalent:@"q"];

  // Setup the application window
  [self.window setFrame:CGRectMake(0, 0, 768, 1024) display:NO];
  [self.window setContentView:self.outputView];
  [self.window setStyleMask:self.window.styleMask |
    NSResizableWindowMask |
    NSClosableWindowMask |
    NSMiniaturizableWindowMask |
    NSTitledWindowMask ];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {

  // Setup the main menu
  [self.window makeKeyAndOrderFront:self];

  // Setup the page load delegate
  self.outputView.frameLoadDelegate = self;

  // Load the test document
  NSURL* url = [[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html"];
  [self.outputView.mainFrame loadRequest:[NSURLRequest requestWithURL:url]];
}

// This method is called after the main webpage is loaded. It calls the test
// function that will eventually output to the page via the echo method below.
- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame
{
  // Obtain a comma delimited list of test functions to call
  NSString* names = [sender stringByEvaluatingJavaScriptFromString:@"AppTestSymbols;"];

  // Check to see if any test symbols were specified
  if (names == nil || names.length == 0) {
    [self echo:@"Define the function getTestSymbols() to return a string containing a comma delimited list of symbols."];
    return;
  }

  // Parse the name list
  for (NSString* name in [names componentsSeparatedByString:@","]) {

    // Attempt to load the symbol
    test_function_t func = dlsym(RTLD_DEFAULT, name.UTF8String);
    if (!func) {
      [self echo:[NSString stringWithFormat:@"<div class='error'>%@ not found</div>",name]];
      continue;
    }

    // Execute the function
    int result = func();

    [self echo:[NSString stringWithFormat:@"%@ returned %d",name,result]];
  }
}

// This message appends the specified string, which may contain HTML tags to the
// document displayed in the webview.
- (void)echo:(NSString*)message {
  NSString* htmlMessage = [message stringByReplacingOccurrencesOfString:@"\n" withString:@"<br>"];

  htmlMessage = [NSString stringWithFormat:@"echo(\"%@\");",htmlMessage];

  [self.outputView stringByEvaluatingJavaScriptFromString:htmlMessage];
}

- (BufferT*)createBufferTWithExtent:(NSArray*)extent ElemSize:(NSNumber*)elemSize
{
  // Create the BufferT struct
  BufferT* buffer = [BufferT createWithExtent:extent ElemSize:elemSize];

  // Add it to the app database. Clients may load the contents of the buffer
  // into an image by loading the URL
  self.database[buffer.imageURL] = buffer;

  return buffer;
}

@end

void halide_print(void *user_context, const char * message)
{
  AppDelegate* app = [NSApp delegate];
  [app echo:[NSString stringWithCString:message encoding:NSUTF8StringEncoding]];

  NSLog(@"%s",message);
}

void halide_error(void *user_context, const char * message)
{
  AppDelegate* app = [NSApp delegate];
  [app echo:[NSString stringWithFormat:@"<div class='error'>%s</div>",message]];

  NSLog(@"%s",message);
}

int halide_buffer_display(const buffer_t* buffer)
{
  BufferT* obj = [BufferT createWithCBufferT:buffer];

  // Add the buffer to the result database
  AppDelegate* app = [NSApp delegate];
  app.database[obj.imageURL] = obj;

  // Load the image through a URL
  [app echo:[NSString stringWithFormat:@"<img src='%@'></img>",obj.imageURL]];

  return 0;
}

int halide_buffer_print(const buffer_t* buffer)
{
  BufferT* obj = [BufferT createWithCBufferT:buffer];

  // Add the buffer to the result database
  AppDelegate* app = [NSApp delegate];
  app.database[obj.imageURL] = obj;

  // Output the buffer as a string
  [app echo:[NSString stringWithFormat:@"<pre class='data'>%@</pre><br>",obj.dataAsString]];

  return 0;
}

