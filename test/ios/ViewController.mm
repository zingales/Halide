//
//  ViewController.m
//  test_ios
//
//  Created by abstephens on 1/20/15.
//
//
#import "ViewController.h"
#import "AppDelegate.h"
#import "AppProtocol.h"
#import "HalideRuntime.h"
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

  NSURL* url = [[NSBundle mainBundle] URLForResource:@"index"
                                       withExtension:@"html"];
  [self.outputView loadRequest:[NSURLRequest requestWithURL:url]];
}

- (void)viewWillLayoutSubviews {

  self.outputView.frame = self.view.frame;

  self.view.autoresizingMask = UIViewAutoresizingFlexibleWidth |
    UIViewAutoresizingFlexibleHeight;
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
    test_function_t func = (test_function_t)dlsym(RTLD_DEFAULT, name.UTF8String);
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

// The Halide runtime will call this function to initialize and make current the
// intended implementation specific OpenGL context.
extern "C"
int halide_opengl_create_context(void *user_context) {

  static EAGLContext* context = nil;

  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
  });

  [EAGLContext setCurrentContext:context];

  return 0;
}

// This method is called by the Halide runtime to populate a function pointer
// lookup table. Since there is only one GLES implementation available on iOS
// we simply forward the lookup to dlsym
extern "C"
void *halide_opengl_get_proc_address(void *user_context, const char *name)
{
  void* symbol = dlsym(RTLD_NEXT,name);

  return symbol;
}

extern "C"
void halide_print(void *user_context, const char * message)
{
  ViewController* app = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  [app echo:[NSString stringWithCString:message encoding:NSUTF8StringEncoding]];

  NSLog(@"%s",message);
}

extern "C"
void halide_error(void *user_context, const char * message)
{
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  [ctrl echo:[NSString stringWithFormat:@"<div class='error'>%s</div>",message]];

  NSLog(@"%s",message);
}

extern "C"
int halide_buffer_display(const buffer_t* buffer)
{
  // Convert the buffer_t to an NSImage

  // TODO: This code should check whether or not the data is planar and handle
  // channel types larger than one byte.
  void* data_ptr = buffer->host;

  size_t width            = buffer->extent[0];
  size_t height           = buffer->extent[1];
  size_t channels         = buffer->extent[2];
  size_t bitsPerComponent = buffer->elem_size*8;

  // For planar data, there is one channel across the row
  size_t src_bytesPerRow      = width*buffer->elem_size;
  size_t dst_bytesPerRow      = width*channels*buffer->elem_size;

  size_t totalBytes = width*height*channels*buffer->elem_size;

  // Unlike Mac OS X Cocoa which directly supports planar data via
  // NSBitmapImageRep, in iOS we must create a CGImage from the pixel data and
  // Quartz only supports interleaved formats.
  unsigned char* src_buffer = (unsigned char*)data_ptr;
  unsigned char* dst_buffer = (unsigned char*)malloc(totalBytes);

  // Interleave the data
  for (size_t c=0;c!=buffer->extent[2];++c) {
    for (size_t y=0;y!=buffer->extent[1];++y) {
      for (size_t x=0;x!=buffer->extent[0];++x) {
        size_t src = x + y*src_bytesPerRow + c * (height*src_bytesPerRow);
        size_t dst = c + x*channels + y*dst_bytesPerRow;
        dst_buffer[dst] = src_buffer[src];
      }
    }
  }

  CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, dst_buffer, totalBytes, NULL);
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

  CGImageRef cgImage = CGImageCreate(width,
                                     height,
                                     bitsPerComponent,
                                     bitsPerComponent*channels,
                                     dst_bytesPerRow,
                                     colorSpace,
                                     kCGBitmapByteOrderDefault,
                                     provider,
                                     NULL,
                                     NO,
                                     kCGRenderingIntentDefault);

  // Note there is a slight difference in the APIs used here between this
  // version of halide_buffer_display and the one in the Mac OS X app
  UIImage* image = [UIImage imageWithCGImage:cgImage];

  // Cleanup
  CGImageRelease(cgImage);
  CGColorSpaceRelease(colorSpace);
  CGDataProviderRelease(provider);

  // Convert the image to a PNG
  NSData* data = UIImagePNGRepresentation(image);

  // Construct a name for the image resource
  static int counter = 0;
  NSString* url = [NSString stringWithFormat:@"%@:///buffer_t%d",kAppProtocolURLScheme,counter++];

  // Add the buffer to the result database
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  ctrl.database[url] = data;

  // Load the image through a URL
  [ctrl echo:[NSString stringWithFormat:@"<img src='%@'></img>",url]];

  return 0;
}

extern "C"
int halide_buffer_print(const buffer_t* buffer)
{
  NSMutableArray* output = [NSMutableArray array];

  // TODO sort the stride to determine the fastest changing dimension

  // For 2D + color channels images, the third dimension extent is usually zero
  int extent3 = buffer->extent[3] ? buffer->extent[3] : 1;

  for (int z = 0; z != extent3; ++z) {
    for (int y = 0; y != buffer->extent[2]; ++y) {
      for (int x = 0; x != buffer->extent[1]; ++x) {
        for (int c = 0; c != buffer->extent[0]; ++c) {
          int idx = z*buffer->stride[3] + y*buffer->stride[2] + x*buffer->stride[1] + c*buffer->stride[0];
          switch (buffer->elem_size) {
            case 1:
              [output addObject:[NSString stringWithFormat:@"%d,",((unsigned char*)buffer->host)[idx]]];
              break;
            case 4:
              [output addObject:[NSString stringWithFormat:@"%f,",((float*)buffer->host)[idx]]];
              break;
          }
        }
        [output addObject:@"\n"];
      }
      [output addObject:@"\n"];
    }
    [output addObject:@"\n\n"];
  }

  NSString* text = [output componentsJoinedByString:@""];

  // Output the buffer as a string
  ViewController* ctrl = (ViewController*)[UIApplication sharedApplication].delegate.window.rootViewController;
  [ctrl echo:[NSString stringWithFormat:@"<pre class='data'>%@</pre><br>",text]];
  
  return 0;
}

@end
