//
//  Created by abstephens on 1/20/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "AppDelegate.h"
#import <dlfcn.h>

int main(int argc, char * argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
  }
}


// The Halide runtime will call this function to initialize and make current the
// intended implementation specific OpenGL context.
int halide_opengl_create_context(void *user_context) {

  static EAGLContext* context = nil;

  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
  });

  [EAGLContext setCurrentContext:context];

  return 0;
}

// The Halide runtime will call this function to load function pointers for
// GL entry points to store in a table. (This approach is necessary on certain
// desktop implementations of OpenGL that support more than one OpenGL
// implementation.) We look the symbol up in the current process and return its
// address.
void *halide_opengl_get_proc_address(void *user_context, const char *name)
{
  void* symbol = dlsym(RTLD_NEXT,name);

  return symbol;
}
