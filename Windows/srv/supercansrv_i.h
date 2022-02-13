

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


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



/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */


#ifndef __supercansrv_i_h__
#define __supercansrv_i_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __ISuperCANDevice_FWD_DEFINED__
#define __ISuperCANDevice_FWD_DEFINED__
typedef interface ISuperCANDevice ISuperCANDevice;

#endif 	/* __ISuperCANDevice_FWD_DEFINED__ */


#ifndef __ISuperCAN_FWD_DEFINED__
#define __ISuperCAN_FWD_DEFINED__
typedef interface ISuperCAN ISuperCAN;

#endif 	/* __ISuperCAN_FWD_DEFINED__ */


#ifndef __CSuperCAN_FWD_DEFINED__
#define __CSuperCAN_FWD_DEFINED__

#ifdef __cplusplus
typedef class CSuperCAN CSuperCAN;
#else
typedef struct CSuperCAN CSuperCAN;
#endif /* __cplusplus */

#endif 	/* __CSuperCAN_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __SuperCAN_LIBRARY_DEFINED__
#define __SuperCAN_LIBRARY_DEFINED__

/* library SuperCAN */
/* [helpstring][version][uuid] */ 

struct SuperCANRingBufferMapping
    {
    BSTR MemoryName;
    BSTR EventName;
    unsigned long Elements;
    unsigned long Bytes;
    } ;
struct SuperCANBitTimingParams
    {
    unsigned short brp;
    unsigned short tseg1;
    byte tseg2;
    byte sjw;
    } ;
struct SuperCANDeviceData
    {
    BSTR name;
    struct SuperCANBitTimingParams nm_min;
    struct SuperCANBitTimingParams nm_max;
    struct SuperCANBitTimingParams dt_min;
    struct SuperCANBitTimingParams dt_max;
    unsigned long can_clock_hz;
    unsigned short feat_perm;
    unsigned short feat_conf;
    byte fw_ver_major;
    byte fw_ver_minor;
    byte fw_ver_patch;
    byte sn_bytes[ 16 ];
    byte sn_length;
    } ;

EXTERN_C const IID LIBID_SuperCAN;

#ifndef __ISuperCANDevice_INTERFACE_DEFINED__
#define __ISuperCANDevice_INTERFACE_DEFINED__

/* interface ISuperCANDevice */
/* [oleautomation][unique][uuid][object] */ 


EXTERN_C const IID IID_ISuperCANDevice;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("434A1140-50D8-4C49-AB8F-9FD1A7327CE0")
    ISuperCANDevice : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE AcquireConfigurationAccess( 
            /* [out] */ boolean *config_access,
            /* [out] */ unsigned long *timeout_ms) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ReleaseConfigurationAccess( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetRingBufferMappings( 
            /* [out] */ struct SuperCANRingBufferMapping *rx,
            /* [out] */ struct SuperCANRingBufferMapping *tx) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDeviceData( 
            /* [out] */ struct SuperCANDeviceData *data) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetBus( 
            /* [in] */ boolean on) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetFeatureFlags( 
            /* [in] */ unsigned long flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetNominalBitTiming( 
            /* [in] */ struct SuperCANBitTimingParams param) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetDataBitTiming( 
            /* [in] */ struct SuperCANBitTimingParams param) = 0;
        
    };
    
    
#else 	/* C style interface */

    typedef struct ISuperCANDeviceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            ISuperCANDevice * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            ISuperCANDevice * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            ISuperCANDevice * This);
        
        HRESULT ( STDMETHODCALLTYPE *AcquireConfigurationAccess )( 
            ISuperCANDevice * This,
            /* [out] */ boolean *config_access,
            /* [out] */ unsigned long *timeout_ms);
        
        HRESULT ( STDMETHODCALLTYPE *ReleaseConfigurationAccess )( 
            ISuperCANDevice * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetRingBufferMappings )( 
            ISuperCANDevice * This,
            /* [out] */ struct SuperCANRingBufferMapping *rx,
            /* [out] */ struct SuperCANRingBufferMapping *tx);
        
        HRESULT ( STDMETHODCALLTYPE *GetDeviceData )( 
            ISuperCANDevice * This,
            /* [out] */ struct SuperCANDeviceData *data);
        
        HRESULT ( STDMETHODCALLTYPE *SetBus )( 
            ISuperCANDevice * This,
            /* [in] */ boolean on);
        
        HRESULT ( STDMETHODCALLTYPE *SetFeatureFlags )( 
            ISuperCANDevice * This,
            /* [in] */ unsigned long flags);
        
        HRESULT ( STDMETHODCALLTYPE *SetNominalBitTiming )( 
            ISuperCANDevice * This,
            /* [in] */ struct SuperCANBitTimingParams param);
        
        HRESULT ( STDMETHODCALLTYPE *SetDataBitTiming )( 
            ISuperCANDevice * This,
            /* [in] */ struct SuperCANBitTimingParams param);
        
        END_INTERFACE
    } ISuperCANDeviceVtbl;

    interface ISuperCANDevice
    {
        CONST_VTBL struct ISuperCANDeviceVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISuperCANDevice_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISuperCANDevice_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISuperCANDevice_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISuperCANDevice_AcquireConfigurationAccess(This,config_access,timeout_ms)	\
    ( (This)->lpVtbl -> AcquireConfigurationAccess(This,config_access,timeout_ms) ) 

#define ISuperCANDevice_ReleaseConfigurationAccess(This)	\
    ( (This)->lpVtbl -> ReleaseConfigurationAccess(This) ) 

#define ISuperCANDevice_GetRingBufferMappings(This,rx,tx)	\
    ( (This)->lpVtbl -> GetRingBufferMappings(This,rx,tx) ) 

#define ISuperCANDevice_GetDeviceData(This,data)	\
    ( (This)->lpVtbl -> GetDeviceData(This,data) ) 

#define ISuperCANDevice_SetBus(This,on)	\
    ( (This)->lpVtbl -> SetBus(This,on) ) 

#define ISuperCANDevice_SetFeatureFlags(This,flags)	\
    ( (This)->lpVtbl -> SetFeatureFlags(This,flags) ) 

#define ISuperCANDevice_SetNominalBitTiming(This,param)	\
    ( (This)->lpVtbl -> SetNominalBitTiming(This,param) ) 

#define ISuperCANDevice_SetDataBitTiming(This,param)	\
    ( (This)->lpVtbl -> SetDataBitTiming(This,param) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISuperCANDevice_INTERFACE_DEFINED__ */


#ifndef __ISuperCAN_INTERFACE_DEFINED__
#define __ISuperCAN_INTERFACE_DEFINED__

/* interface ISuperCAN */
/* [oleautomation][unique][uuid][object] */ 


EXTERN_C const IID IID_ISuperCAN;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("8F8C4375-2DFE-4335-8947-036F965BD927")
    ISuperCAN : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DeviceScan( 
            /* [out] */ unsigned long *count) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DeviceGetCount( 
            /* [out] */ unsigned long *count) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DeviceOpen( 
            /* [in] */ unsigned long index,
            /* [out] */ ISuperCANDevice **dev) = 0;
        
    };
    
    
#else 	/* C style interface */

    typedef struct ISuperCANVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            ISuperCAN * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            ISuperCAN * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            ISuperCAN * This);
        
        HRESULT ( STDMETHODCALLTYPE *DeviceScan )( 
            ISuperCAN * This,
            /* [out] */ unsigned long *count);
        
        HRESULT ( STDMETHODCALLTYPE *DeviceGetCount )( 
            ISuperCAN * This,
            /* [out] */ unsigned long *count);
        
        HRESULT ( STDMETHODCALLTYPE *DeviceOpen )( 
            ISuperCAN * This,
            /* [in] */ unsigned long index,
            /* [out] */ ISuperCANDevice **dev);
        
        END_INTERFACE
    } ISuperCANVtbl;

    interface ISuperCAN
    {
        CONST_VTBL struct ISuperCANVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define ISuperCAN_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define ISuperCAN_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define ISuperCAN_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define ISuperCAN_DeviceScan(This,count)	\
    ( (This)->lpVtbl -> DeviceScan(This,count) ) 

#define ISuperCAN_DeviceGetCount(This,count)	\
    ( (This)->lpVtbl -> DeviceGetCount(This,count) ) 

#define ISuperCAN_DeviceOpen(This,index,dev)	\
    ( (This)->lpVtbl -> DeviceOpen(This,index,dev) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __ISuperCAN_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_CSuperCAN;

#ifdef __cplusplus

class DECLSPEC_UUID("E6214AB1-56AD-4215-8688-095D3816F260")
CSuperCAN;
#endif
#endif /* __SuperCAN_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


