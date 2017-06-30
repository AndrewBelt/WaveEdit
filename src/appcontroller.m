static char *appdir;

@implementation AppController

- (void)method {
  appdir = [[[NSBundle mainBundle] bundlePath] UTF8String];
}

@end