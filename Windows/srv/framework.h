#pragma once

#ifndef STRICT
#define STRICT
#endif

#include "targetver.h"

//#define _ATL_SINGLE_THREADED
//#define _ATL_APARTMENT_THREADED
#define _ATL_FREE_THREADED

#define _ATL_NO_AUTOMATIC_NAMESPACE

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// some CString constructors will be explicit


#define ATL_NO_ASSERT_ON_DESTROY_NONEXISTENT_WINDOW

//#define _ATL_STATIC_REGISTRY

#include "resource.h"
#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>
