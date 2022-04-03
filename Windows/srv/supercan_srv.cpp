/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2022 Jean Gressmann <jean@0x42.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */


#include "pch.h"

#include "resource.h"
#include "CoSuperCAN.h"

#include "../inc/supercan_dll.h"


class CSuperCANSrvModule : public ATL::CAtlExeModuleT< CSuperCANSrvModule >
{
public :
	DECLARE_LIBID(LIBID_SuperCAN)
	DECLARE_REGISTRY_APPID_RESOURCEID(IDR_SUPERCANSRV, "{945BB09D-DA86-4183-9C9A-D85220638156}")
};



CSuperCANSrvModule _AtlModule;



//
extern "C" int WINAPI _tWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
								LPTSTR /*lpCmdLine*/, int nShowCmd)
{
	static const char NAME[] = "Local\\sc-singleton{BA10F9A1-4ABC-46F1-A1C5-BF22F91A7620}";

	HANDLE singleton = NULL;

	singleton = CreateEventA(NULL, TRUE, FALSE, NAME);

	if (singleton) {
		LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "Created singleton event name='%s'\n", NAME);
	}
	else {
		if (ERROR_ALREADY_EXISTS != GetLastError()) {
			LOG_SRV(SC_DLL_LOG_LEVEL_ERROR, "Failed to create singleton event name='%s' error=%lu\n", NAME, GetLastError());
			return 1;
		}
	}

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "WinMain start\n");

	auto rc = _AtlModule.WinMain(nShowCmd);

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "WinMain end\n");

	if (singleton) {
		CloseHandle(singleton);
		singleton = NULL;
		LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "Destroyed singleton event name='%s'\n", NAME);
	}

	return rc;
}

