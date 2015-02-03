//
//  ViewController.m
//  test_ios
//
//  Created by abstephens on 1/20/15.
//
//

#import "ViewController.h"
#import "AppDelegate.h"
#import "BufferT.h"

#import <dlfcn.h>

// Test functions are loaded from the app process using a set of strings
// encoded in the loaded HTML page. These functions take no arguments and return
// zero on success. They may use Halide runtime calls to log info, which are
// wired up to output in the WebView.
typedef int (*test_function_t)(void);

@implementation ViewController

- (instancetype)init {
  self = [super init];
  if (self) {
    _outputView = [[UIWebView alloc] initWithFrame:CGRectZero];

    // Enable zooming in and out of content
    _outputView.scalesPageToFit = YES;
  }
  return self;
}

- (void)viewDidLoad {
  [super viewDidLoad];

  [self.view addSubview:self.outputView];

  self.outputView.delegate = self;

  NSURL* url = [[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html"];
  [self.outputView loadRequest:[NSURLRequest requestWithURL:url]];
}

- (void)viewWillLayoutSubviews {

  self.outputView.frame = self.view.frame;

  self.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.view.autoresizesSubviews = YES;
}

- (void)webViewDidFinishLoad:(UIWebView *)sender
{
  self.database = [NSMutableDictionary dictionary];

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

void halide_print(void *user_context, const char * message)
{
  ViewController* app = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  [app echo:[NSString stringWithCString:message encoding:NSUTF8StringEncoding]];

  NSLog(@"%s",message);
}

void halide_error(void *user_context, const char * message)
{
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  [ctrl echo:[NSString stringWithFormat:@"<div class='error'>%s</div>",message]];

  NSLog(@"%s",message);
}

int halide_buffer_display(const buffer_t* buffer)
{
  BufferT* obj = [BufferT createWithCBufferT:buffer];

  // Add the buffer to the result database
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  ctrl.database[obj.imageURL] = obj;

  // Load the image through a URL
  [ctrl echo:[NSString stringWithFormat:@"<img src='%@'></img>",obj.imageURL]];

  return 0;
}

int halide_buffer_print(const buffer_t* buffer)
{
  BufferT* obj = [BufferT createWithCBufferT:buffer];

  // Add the buffer to the result database
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  ctrl.database[obj.imageURL] = obj;

  // Output the buffer as a string
  [ctrl echo:[NSString stringWithFormat:@"<pre class='data'>%@</pre><br>",obj.dataAsString]];

  return 0;
}


@end
