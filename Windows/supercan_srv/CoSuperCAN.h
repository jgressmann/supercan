/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jean Gressmann <jean@0x42.de>
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


#pragma once

#include "framework.h"

#include "supercansrv_i.h"


class ATL_NO_VTABLE CSuperCAN :
    //public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
	public ATL::CComObjectRoot,
    public ATL::CComCoClass<CSuperCAN, &CLSID_CSuperCAN>,
    public ISuperCAN
{
public:
	DECLARE_REGISTRY_RESOURCEID(IDR_SUPERCANSRV)

	BEGIN_COM_MAP(CSuperCAN)
		COM_INTERFACE_ENTRY(ISuperCAN)
	END_COM_MAP()

public:
	~CSuperCAN();
	CSuperCAN();

public:
	STDMETHOD(DeviceScan)(unsigned long* count);
	STDMETHOD(DeviceGetCount)(unsigned long* count);

	STDMETHOD(DeviceOpen)(
		unsigned long index, 
		boolean request_config_access,
		ISuperCANDevice** dev);

private:
	ISuperCAN* m_Instance;
};




