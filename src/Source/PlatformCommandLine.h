// ----------------------------------------------------------------------------
// 
// PlatformCommandLine.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once
#include <string>
#include <vector>
#include <map>

class CMDLine
{
	public:
		static const std::vector<std::string> & Get();
		static const std::map<std::string, std::string>& Map();
		static const std::map<std::string, std::string>::const_iterator& End();
};
