// ----------------------------------------------------------------------------
// 
// PlatformCommandLine-mac.mm
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "PlatformCommandLine.h"
#import <Foundation/Foundation.h>


const std::__ndk1::vector <std::string> & CMDLine::Get() {
	static std::vector<std::string> ret;
    static bool toInit = true;

	if(toInit) {
		toInit = false;
		NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
		ret.reserve([args count]);
		for(NSString *arg in args) {
			ret.push_back([arg UTF8String]);
		}
	}
	return ret;
}

