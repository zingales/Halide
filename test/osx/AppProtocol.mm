//
//  AppProtocol.m
//  test_osx
//
//  Created by abstephens on 1/22/15.
//
//

#import "AppProtocol.h"

#include "AppDelegate.h"
#include "BufferT.h"

#include <dlfcn.h>

#include "HalideRuntime.h"

// This app does not explicitly include a generated filter header, instead it
// dlsym's a generated function out of the current process. As a result, the
// buffer_t declaration usually placed in the generated header is not available.
#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED
#include <stdint.h>
typedef struct buffer_t {
  uint64_t dev;
  uint8_t* host;
  int32_t extent[4];
  int32_t stride[4];
  int32_t min[4];
  int32_t elem_size;
  bool host_dirty;
  bool dev_dirty;
} buffer_t;
#endif

NSString* kAppProtocolURLScheme = @"app";

@implementation AppProtocol

+ (BOOL)canInitWithRequest:(NSURLRequest *)request {

  // Check if the request matches the app custom URL scheme
  return [request.URL.scheme isEqualToString:kAppProtocolURLScheme];
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {

  // Caching is not supported by this protocol so canonicalization of the
  // request is not necessary.
  return request;
}

+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b {

  // Caching is not supported by this protocol, each request results in a
  // the test app or halide runtime performing some action which may have side
  // effects
  return NO;
}

- (void)startLoading {

  // Look up the URL in the app database
  AppDelegate* app = [NSApp delegate];

  NSString* key = self.request.URL.absoluteString;
  BufferT* entry = app.database[key];

  // Check to see if the key was found, if so, obtain a BufferT instance from it
  // and create an output image from the buffer_t
  NSImage* image = nil;
  if (entry) {
    image = [entry dataAsImage];
  } else {
    // Otherwise, return a placeholder image
    image = [NSImage imageNamed:NSImageNameStopProgressTemplate];
  }

  // Convert the image to a PNG
  NSData* tiffData = [image TIFFRepresentation];
  NSBitmapImageRep* rep = [NSBitmapImageRep imageRepsWithData:tiffData][0];
  NSData* responseData = [rep representationUsingType:NSPNGFileType properties:nil];
  NSString* responseMimeType = @"image/png";

  // Create a response to send back to the webview
  NSURLResponse *response = [[NSURLResponse alloc] initWithURL:self.request.URL
                                                      MIMEType:responseMimeType
                                         expectedContentLength:responseData.length
                                              textEncodingName:nil];

  // Send the initial response
  [self.client URLProtocol:self
        didReceiveResponse:response
        cacheStoragePolicy:NSURLCacheStorageNotAllowed];

  // Send the data
  [self.client URLProtocol:self didLoadData:responseData];

  // Finish
  [self.client URLProtocolDidFinishLoading:self];
}

- (void)stopLoading {

  // Operations in this protocol should be nearly instantaneous, so this
  // method is empty.
}

@end
