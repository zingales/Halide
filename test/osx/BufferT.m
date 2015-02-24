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

// Counter for producing unique buffer_t image names in the app
static int counter = 0;

-(instancetype) initWithCBufferTCopy:(const buffer_t*)rhs
{
  self = [super init];
  if (self) {
    buffer = *rhs;

    // If the input buffer_t does not have a host pointer allocated, create
    // an NSData containing
    if (!buffer.host) {
    } else {

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

-(NSImage*)dataAsImage
{

  return image;
}

@end
