#include "Preferences.h"
#import <AppKit/AppKit.h>
bool systemReduceMotion() { return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion]; }
