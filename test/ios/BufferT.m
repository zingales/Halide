#import "BufferT.h"
#import "AppProtocol.h"
#import "AppDelegate.h"

@interface BufferT ()
{
  buffer_t buffer;
}

@property (retain) NSURL* url;
@end

@implementation BufferT

static int counter = 0;

+(BufferT*)createWithCBufferT:(const buffer_t*)rhs
{
  return [[BufferT alloc] initWithCBufferT:rhs];
}

-(instancetype) initWithCBufferT:(const buffer_t*)rhs
{
  self = [super init];
  if (self) {
    buffer = *rhs;

    if (!buffer.host) {
      size_t size_bytes = buffer.elem_size;
      for (int i = 0; i != 4; ++i) {
        size_bytes *= buffer.extent[i] ? buffer.extent[i] : 1;
      }

      self.hostData = [NSMutableData dataWithLength:size_bytes];
      buffer.host = self.hostData.mutableBytes;
    }

    // Set the URL within the app for this buffer_t
    self.url = [NSURL URLWithString:[NSString stringWithFormat:@"%@:///buffer/%d",
                                     kAppProtocolURLScheme,
                                     counter++]];
  }
  return self;
}

+(BufferT*)createWithExtent:(NSArray*)extent ElemSize:(NSNumber*)elemSize
{
  return [[BufferT alloc] initWithExtent:extent ElemSize:elemSize];
}

-(instancetype) initWithExtent:(NSArray*)extent ElemSize:(NSNumber*)elemSize
{
  self = [super init];
  if (self) {
    // Set the parameters for the buffer_t
    for (int i = 0; i != extent.count; ++i)
      buffer.extent[i] = [extent[i] intValue];

    buffer.stride[0] = 1;
    for (int i = 1; i != extent.count; ++i)
      buffer.stride[i] = buffer.stride[i-1]*buffer.extent[i-1];

    buffer.elem_size = [elemSize intValue];

    // Allocate a host buffer
    size_t size_bytes = buffer.elem_size;
    for (int i = 0; i != 4; ++i) {
      size_bytes *= buffer.extent[i] ? buffer.extent[i] : 1;
    }

    self.hostData = [NSMutableData dataWithLength:size_bytes];
    buffer.host = self.hostData.mutableBytes;

    // Set the URL within the app for this buffer_t
    self.url = [NSURL URLWithString:[NSString stringWithFormat:@"%@:///buffer/%d",
                                kAppProtocolURLScheme,
                                counter++]];
  }

  return self;
}

-(NSString*)imageURL
{
  return self.url.absoluteString;
}

-(buffer_t*)ptr;
{
  return &buffer;
}

-(NSString*)dataAsString
{
  NSMutableArray* output = [NSMutableArray array];

  // TODO sort the stride to determine the fastest changing dimension

  // For 2D + color channels images, the third dimension extent is usually zero
  int extent3 = buffer.extent[3] ? buffer.extent[3] : 1;

  for (int z = 0; z != extent3; ++z) {
    for (int y = 0; y != buffer.extent[2]; ++y) {
      for (int x = 0; x != buffer.extent[1]; ++x) {
        for (int c = 0; c != buffer.extent[0]; ++c) {
          int idx = z*buffer.stride[3] + y*buffer.stride[2] + x*buffer.stride[1] + c*buffer.stride[0];
          switch (buffer.elem_size) {
            case 1:
              [output addObject:[NSString stringWithFormat:@"%d,",((unsigned char*)buffer.host)[idx]]];
              break;
            case 4:
              [output addObject:[NSString stringWithFormat:@"%f,",((float*)buffer.host)[idx]]];
              break;
          }
        }
        [output addObject:@"\n"];
      }
      [output addObject:@"\n"];
    }
    [output addObject:@"\n\n"];
  }

  return [output componentsJoinedByString:@""];
}

-(UIImage*)dataAsImage
{

#if 0
  void* data_ptr = buffer.host;

  size_t width            = buffer.extent[0];
  size_t height           = buffer.extent[1];
  size_t channels         = buffer.extent[2];
  size_t bitsPerComponent = buffer.elem_size*8;

  // For planar data, there is one channel across the row
  size_t bytesPerRow      = width*buffer.elem_size;
  size_t bitsPerPixel     = bitsPerComponent;

#if 0
  for (int c=0;c!=channels;++c) {
    for (int y=0;y!=height;++y) {
      for (int x=0;x!=width;++x) {
        int idx = x + y*bytesPerRow + c * (height*bytesPerRow);
        switch (c) {
          case 1:
            ((unsigned char*)data_ptr)[idx] = ((float)x/(float)width) * 255.0f;
            break;
          case 2:
            ((unsigned char*)data_ptr)[idx] = ((float)y/(float)height) * 255.0f;
            break;
          default:
            ((unsigned char*)data_ptr)[idx] = 255;
            break;
        }
      }
    }
  }
#endif

  // Create an array of points to the image planes.
  unsigned sliceBytes = width*height*buffer.elem_size;
  unsigned char* planes[] = {
    data_ptr,
    data_ptr+sliceBytes,
    data_ptr+sliceBytes*2,
    data_ptr+sliceBytes*3,
  };

  // Create an image data object to copy into
  NSBitmapImageRep* imageRep =
  [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char**)planes
                                          pixelsWide:width
                                          pixelsHigh:height
                                       bitsPerSample:bitsPerComponent
                                     samplesPerPixel:channels
                                            hasAlpha:YES
                                            isPlanar:YES
                                      colorSpaceName:NSDeviceRGBColorSpace
                                         bytesPerRow:bytesPerRow
                                        bitsPerPixel:0];

  // Create the output image and add the data representation
  NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
  [image addRepresentation:imageRep];
  [image lockFocus];

  [NSGraphicsContext saveGraphicsState];

  // Draw into the output image
  [imageRep drawInRect:NSMakeRect(0, 0, [image size].width, [image size].height)];

  // Cleanup
  [NSGraphicsContext restoreGraphicsState];
  [image unlockFocus];
#else
  void* data_ptr = buffer.host;

  size_t width            = buffer.extent[0];
  size_t height           = buffer.extent[1];
  size_t channels         = buffer.extent[2];
  size_t bitsPerComponent = buffer.elem_size*8;

  // For planar data, there is one channel across the row
  size_t src_bytesPerRow      = width*buffer.elem_size;
  size_t dst_bytesPerRow      = width*channels*buffer.elem_size;

  size_t totalBytes = width*height*channels*buffer.elem_size;

  // Unlike Mac OS X Cocoa which directly supports planar data via
  // NSBitmapImageRep, in iOS we must create a CGImage from the pixel data and
  // Quartz only supports interleaved formats.
  unsigned char* src_buffer = (unsigned char*)data_ptr;
  unsigned char* dst_buffer = (unsigned char*)malloc(totalBytes);

  // Interleave the data
  for (size_t c=0;c!=buffer.extent[2];++c) {
    for (size_t y=0;y!=buffer.extent[1];++y) {
      for (size_t x=0;x!=buffer.extent[0];++x) {
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

  UIImage* image = [UIImage imageWithCGImage:cgImage];

  CGColorSpaceRelease(colorSpace);
  CGDataProviderRelease(provider);

#endif
  return image;
}

@end
