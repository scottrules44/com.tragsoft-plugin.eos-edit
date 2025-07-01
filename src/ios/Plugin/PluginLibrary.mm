#import <UIKit/UIKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <AuthenticationServices/AuthenticationServices.h>

@interface PresentationContextProvider : NSObject <ASWebAuthenticationPresentationContextProviding>
@property (nonatomic, assign) UIWindow *window;
@end

@implementation PresentationContextProvider

- (ASPresentationAnchor)presentationAnchorForWebAuthenticationSession:(ASWebAuthenticationSession *)session {
    return self.window;
}

@end

extern "C" void* CreateWebAuthContextProvider() {
    // Try to find the top-most view controller
    UIViewController* vc = [UIApplication sharedApplication].keyWindow.rootViewController;
    while (vc.presentedViewController != nil) {
        vc = vc.presentedViewController;
    }

    UIWindow* window = vc.view.window ?: [UIApplication sharedApplication].keyWindow;
    if (!window) {
        NSLog(@"[CreateWebAuthContextProvider] No valid window found");
        return nullptr;
    }

    // Wrap the window in a PresentationContextProvider
    PresentationContextProvider* provider = [[PresentationContextProvider alloc] init];
    provider.window = window;

    return (void *)CFBridgingRetain(provider);
}