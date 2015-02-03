//
//  Created by abstephens on 2/10/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import <UIKit/UIKit.h>

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

@class BufferT;

// This class thinly wraps the Halide type buffer_t in an ObjC object and
// provides host data storage and coversion methods.
@interface BufferT : NSObject

@property (retain) NSMutableData* hostData;

+(BufferT*)createWithCBufferT:(const buffer_t*)buffer;
+(BufferT*)createWithExtent:(NSArray*)extent ElemSize:(NSNumber*)elemSize;

// This method returns a URL which may be used to load the image via a custom
// app specific URL scheme
-(NSString*)imageURL;

// This method returns a pointer to the wrapped buffer_t and is used to pass
// arguments to the generated halide function
-(buffer_t*)ptr;

// This method returns a string containing the contents of the image formatted
-(NSString*)dataAsString;

// This method converts the buffer_t data into an NSImage, from which it can be
// turned into a PNG file
-(UIImage*)dataAsImage;

@end
