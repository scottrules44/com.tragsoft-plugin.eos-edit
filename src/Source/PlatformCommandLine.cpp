// ----------------------------------------------------------------------------
// 
// PlatformCommandLine.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "PlatformCommandLine.h"

const std::map<std::string, std::string>& CMDLine::Map() {
	static std::map<std::string, std::string> ret;
    static bool toInit = true;

	if(toInit) {
		toInit = false;
		for(auto &arg : Get()) {
			auto p = arg.find_first_of("=");
			if(p != std::string::npos) {
				auto key = arg.substr(0, p);
				auto val = arg.substr(p + 1);
				if(key.length() > 0 && key[0] == '-') {
					key = key.substr(1);
				}
				if(key.length() > 0) {
					ret[key] = val;
				}
			}
		}
	}
	return ret;
}

const std::map<std::string, std::string>::const_iterator& CMDLine::End() {
	static auto ret = Map().end();
	return ret;
}

const std::vector<std::string> & CMDLine::Get() {
    static std::vector<std::string> emptyList;
    return emptyList;
}