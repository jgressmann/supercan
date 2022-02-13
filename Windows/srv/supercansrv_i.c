

/* this ALWAYS GENERATED file contains the IIDs and CLSIDs */

/* link this file in with the server and any clients */


 /* File created by MIDL compiler version 8.01.0622 */
/* at Tue Jan 19 04:14:07 2038
 */
/* Compiler settings for supercan_srv.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.01.0622 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */



#ifdef __cplusplus
extern "C"{
#endif 


#include <rpc.h>
#include <rpcndr.h>

#ifdef _MIDL_USE_GUIDDEF_

#ifndef INITGUID
#define INITGUID
#include <guiddef.h>
#undef INITGUID
#else
#include <guiddef.h>
#endif

#define MIDL_DEFINE_GUID(type,name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        DEFINE_GUID(name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8)

#else // !_MIDL_USE_GUIDDEF_

#ifndef __IID_DEFINED__
#define __IID_DEFINED__

typedef struct _IID
{
    unsigned long x;
    unsigned short s1;
    unsigned short s2;
    unsigned char  c[8];
} IID;

#endif // __IID_DEFINED__

#ifndef CLSID_DEFINED
#define CLSID_DEFINED
typedef IID CLSID;
#endif // CLSID_DEFINED

#define MIDL_DEFINE_GUID(type,name,l,w1,w2,b1,b2,b3,b4,b5,b6,b7,b8) \
        EXTERN_C __declspec(selectany) const type name = {l,w1,w2,{b1,b2,b3,b4,b5,b6,b7,b8}}

#endif // !_MIDL_USE_GUIDDEF_

MIDL_DEFINE_GUID(IID, LIBID_SuperCAN,0xfd71338b,0x9533,0x4785,0x8c,0x2f,0x66,0x4e,0xce,0x4b,0xeb,0xee);


MIDL_DEFINE_GUID(IID, IID_ISuperCANDevice,0x434A1140,0x50D8,0x4C49,0xAB,0x8F,0x9F,0xD1,0xA7,0x32,0x7C,0xE0);


MIDL_DEFINE_GUID(IID, IID_ISuperCAN,0x8F8C4375,0x2DFE,0x4335,0x89,0x47,0x03,0x6F,0x96,0x5B,0xD9,0x27);


MIDL_DEFINE_GUID(CLSID, CLSID_CSuperCAN,0xE6214AB1,0x56AD,0x4215,0x86,0x88,0x09,0x5D,0x38,0x16,0xF2,0x60);

#undef MIDL_DEFINE_GUID

#ifdef __cplusplus
}
#endif



