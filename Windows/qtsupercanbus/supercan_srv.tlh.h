#pragma once

#if defined(_MSC_VER) || (defined(__clang__) && !defined(__GNUC__))
    #define QT_SC_ALIGN(x) __declspec(align(x))
    #define QT_SC_UUID_INLINE(id) __declspec(uuid(id))
    #define QT_SC_UUID_EXTERN(type, id)
#elif defined(__GNUC__)
    #define QT_SC_ALIGN(x) __attribute__((aligned(x)))
    #define QT_SC_UUID_INLINE(id)
    #define QT_SC_UUID_EXTERN(type, id) \
        template<> inline GUID const& __mingw_uuidof<type>() { \
            static GUID guid; \
            static const HRESULT initialized = CLSIDFromString(L"{" id "}", &guid); \
            (void)initialized; \
            return guid; \
        }
#endif



#pragma pack(push, 8)

#include <comdef.h>

namespace SuperCAN {

//
// Forward references and typedefs
//

struct __SuperCAN;
struct SuperCANRingBufferMapping;
struct SuperCANBitTimingParams;
struct SuperCANDeviceData;
struct SuperCANVersion;
struct ISuperCANDevice;
struct ISuperCANDevice2;
struct ISuperCAN;
struct ISuperCAN2;
struct /* coclass */ CSuperCAN;



//
// Type library items
//

#pragma pack(push, 4)

struct SuperCANRingBufferMapping
{
    BSTR MemoryName;
    BSTR EventName;
    unsigned long Elements;
    unsigned long Bytes;
};

#pragma pack(pop)

#pragma pack(push, 2)

struct SuperCANBitTimingParams
{
    unsigned short brp;
    unsigned short tseg1;
    unsigned char tseg2;
    unsigned char sjw;
};

#pragma pack(pop)

#pragma pack(push, 4)

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
    unsigned char fw_ver_major;
    unsigned char fw_ver_minor;
    unsigned char fw_ver_patch;
    unsigned char sn_bytes[16];
    unsigned char sn_length;
};

#pragma pack(pop)

#pragma pack(push, 4)

struct SuperCANVersion
{
    BSTR commit;
    unsigned short major;
    unsigned short minor;
    unsigned short patch;
    unsigned short build;
};

#pragma pack(pop)

struct QT_SC_UUID_INLINE("434a1140-50d8-4c49-ab8f-9fd1a7327ce0")
ISuperCANDevice : IUnknown
{
    //
    // Raw methods provided by interface
    //

      virtual HRESULT __stdcall AcquireConfigurationAccess (
        /*[out]*/ char * config_access,
        /*[out]*/ unsigned long * timeout_ms ) = 0;
      virtual HRESULT __stdcall ReleaseConfigurationAccess ( ) = 0;
      virtual HRESULT __stdcall GetRingBufferMappings (
        /*[out]*/ struct SuperCANRingBufferMapping * rx,
        /*[out]*/ struct SuperCANRingBufferMapping * tx ) = 0;
      virtual HRESULT __stdcall GetDeviceData (
        /*[out]*/ struct SuperCANDeviceData * data ) = 0;
      virtual HRESULT __stdcall SetBus (
        /*[in]*/ char on ) = 0;
      virtual HRESULT __stdcall SetFeatureFlags (
        /*[in]*/ unsigned long flags ) = 0;
      virtual HRESULT __stdcall SetNominalBitTiming (
        /*[in]*/ struct SuperCANBitTimingParams param ) = 0;
      virtual HRESULT __stdcall SetDataBitTiming (
        /*[in]*/ struct SuperCANBitTimingParams param ) = 0;
};

struct QT_SC_UUID_INLINE("4cf5e694-1111-4719-b6cb-a9b88985e97a")
ISuperCANDevice2 : ISuperCANDevice
{
    //
    // Raw methods provided by interface
    //

      virtual HRESULT __stdcall SetLogLevel (
        int level ) = 0;
};

struct QT_SC_UUID_INLINE("8f8c4375-2dfe-4335-8947-036f965bd927")
ISuperCAN : IUnknown
{
    //
    // Raw methods provided by interface
    //

      virtual HRESULT __stdcall DeviceScan (
        /*[out]*/ unsigned long * count ) = 0;
      virtual HRESULT __stdcall DeviceGetCount (
        /*[out]*/ unsigned long * count ) = 0;
      virtual HRESULT __stdcall DeviceOpen (
        /*[in]*/ unsigned long index,
        /*[out]*/ struct ISuperCANDevice * * dev ) = 0;
};

struct QT_SC_UUID_INLINE("da4c7005-6d21-4ab7-9933-5eaa959f0621")
ISuperCAN2 : ISuperCAN
{
    //
    // Raw methods provided by interface
    //

      virtual HRESULT __stdcall GetVersion (
        struct SuperCANVersion * version ) = 0;
      virtual HRESULT __stdcall SetLogLevel (
        int level ) = 0;
};

struct QT_SC_UUID_INLINE("e6214ab1-56ad-4215-8688-095d3816f260")
CSuperCAN;

} // namespace SuperCAN

QT_SC_UUID_EXTERN(SuperCAN::__SuperCAN, "fd71338b-9533-4785-8c2f-664ece4bebee");
QT_SC_UUID_EXTERN(SuperCAN::ISuperCANDevice, "434a1140-50d8-4c49-ab8f-9fd1a7327ce0");
QT_SC_UUID_EXTERN(SuperCAN::ISuperCANDevice2, "4cf5e694-1111-4719-b6cb-a9b88985e97a");
QT_SC_UUID_EXTERN(SuperCAN::ISuperCAN, "8f8c4375-2dfe-4335-8947-036f965bd927");
QT_SC_UUID_EXTERN(SuperCAN::ISuperCAN2, "da4c7005-6d21-4ab7-9933-5eaa959f0621");
QT_SC_UUID_EXTERN(SuperCAN::CSuperCAN, "e6214ab1-56ad-4215-8688-095d3816f260");

namespace SuperCAN {

//
// Smart pointer typedef declarations
//

_COM_SMARTPTR_TYPEDEF(ISuperCANDevice, __uuidof(ISuperCANDevice));
_COM_SMARTPTR_TYPEDEF(ISuperCANDevice2, __uuidof(ISuperCANDevice2));
_COM_SMARTPTR_TYPEDEF(ISuperCAN, __uuidof(ISuperCAN));
_COM_SMARTPTR_TYPEDEF(ISuperCAN2, __uuidof(ISuperCAN2));

} // namespace SuperCAN

#pragma pack(pop)
