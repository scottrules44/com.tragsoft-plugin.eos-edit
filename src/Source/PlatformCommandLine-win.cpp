// ----------------------------------------------------------------------------
// 
// PlatformCommandLine-win.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "PlatformCommandLine.h"
#include <windows.h>
#include <codecvt>

const std::vector<std::string> & CMDLine::Get() {
	static std::vector<std::string> ret;
	static bool toInit = true;

	if (toInit) {
		toInit = false;
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		ret.reserve(argc);
		for (int i = 0; argv && i < argc; i++) {
			int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, 0, 0, 0, 0);

			char* buf = new char[len];

			WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, buf, len, 0, 0);

			ret.push_back(buf);
			delete[] buf;
		}
	}
	return ret;
}

