/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2023 Jean Gressmann <jean@0x42.de>
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

#include <cfgmgr32.h>
#include <initguid.h> // required to resolve DEVPKEY_*
#include <devpkey.h>


#include "CoSuperCAN.h"
#include "commit.h"


#include "../inc/supercan_dll.h"
#include "../inc/supercan_winapi.h"
#include "../inc/supercan_srv.h"
#include "../src/supercan_misc.h"


#ifdef min
#undef min
#endif

OBJECT_ENTRY_AUTO(CLSID_CSuperCAN, CSuperCAN)



#include <vector>
#include <memory>
#include <string>
#include <cassert>
#include <cstdio>
#include <atomic>
#include <cstdarg>


#define CMD_TIMEOUT_MS 3000
#define MAX_COM_DEVICES_PER_SC_DEVICE_BITS 3
#define MAX_COM_DEVICES_PER_SC_DEVICE (1u<<MAX_COM_DEVICES_PER_SC_DEVICE_BITS)

extern "C" int sc_map_cm_error(CONFIGRET cr);
extern "C" int sc_map_win_error(sc_dev_t * _dev, DWORD error);

namespace
{

constexpr unsigned long CONFIG_ACCESS_TIMEOUT_MS = 1ul << 13;
typedef uint8_t sc_com_dev_index_t;

inline int map_device_error(uint8_t error)
{
	switch (error) {
	case SC_ERROR_NONE:
		return SC_DLL_ERROR_NONE;
	case SC_ERROR_SHORT:
		return SC_DLL_ERROR_INVALID_PARAM;
	case SC_ERROR_PARAM:
		return SC_DLL_ERROR_INVALID_PARAM;
	case SC_ERROR_BUSY:
		return SC_DLL_ERROR_DEVICE_BUSY;
	case SC_ERROR_UNSUPPORTED:
		return SC_DLL_ERROR_DEV_NOT_IMPLEMENTED;
	default:
	case SC_ERROR_UNKNOWN:
		return SC_DLL_ERROR_UNKNOWN;

	}
}



inline uint8_t dlc_to_len(uint8_t dlc)
{
	static const uint8_t map[16] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
	};
	return map[dlc & 0xf];
}

class ATL_NO_VTABLE XSuperCANDevice;

class Guard
{
public:
	~Guard()
	{
		LeaveCriticalSection(m_Lock);
	}

	Guard(CRITICAL_SECTION& lock)
		: m_Lock(&lock)
	{
		EnterCriticalSection(m_Lock);
	}

private:
	LPCRITICAL_SECTION m_Lock;
};


struct sc_mm_data {
	wchar_t mem_name[64];
	wchar_t ev_name[64];
	
	uint32_t elements;
};

struct com_device_data {
	sc_mm_data rx;
	sc_mm_data tx;
};

class ScDev : public std::enable_shared_from_this<ScDev>
{
public:
	~ScDev();
	ScDev();

	int Init(std::wstring&& name);
	void Uninit();
	int AddComDevice(XSuperCANDevice* device);
	void RemoveComDevice(sc_com_dev_index_t index);
	bool AcquireConfigurationAccess(sc_com_dev_index_t index, unsigned long* timeout_ms);
	void ReleaseConfigurationAccess(sc_com_dev_index_t index);
	int SetBus(sc_com_dev_index_t index, bool on);
	int SetLogLevel(sc_com_dev_index_t index, int level);
	int SetFeatureFlags(sc_com_dev_index_t index, uint32_t flags);
	int SetNominalBitTiming(sc_com_dev_index_t index, SuperCANBitTimingParams params);
	int SetDataBitTiming(sc_com_dev_index_t index, SuperCANBitTimingParams params);

public:
	const std::wstring& name() const { return m_Name; }
	sc_msg_dev_info dev_info;
	sc_msg_can_info can_info;

private:
	static DWORD WINAPI RxMain(void* self);
	void RxMain();
	static DWORD WINAPI TxMain(void* self);
	void TxMain();
	static int OnRx(void* ctx, void const* ptr, uint16_t bytes);
	int OnRx(sc_msg_header const* ptr, unsigned bytes);
	static void Log(void* ctx, int level, const char* msg, size_t bytes);
	void Log(int level, const char* msg, size_t bytes);
	int Map();
	void Unmap();
	int OpenDevice();
	void CloseDevice();
	void SetDeviceError(int error);
	int SetBusOn();
	void SetBusOff();
	void BusCleanup();
	bool VerifyConfigurationAccess(sc_com_dev_index_t index) const;
	void ComDeviceAddedTx(sc_com_dev_index_t index);
	void ComDeviceRemovedTx(sc_com_dev_index_t index);
	void ComDeviceRemovedRx(sc_com_dev_index_t index);
	void Notify(uint8_t code, uint8_t value);
	void ProcessRxNotification(bool* done, bool* performed_work);
	void ProcessLog(bool* performed_work);
	void ProcessRxStream(bool* stream_error, bool* performed_work);
	void ResetTxrMap();
	void LogFormatQueue(int level, char const* fmt, ...);
	void LogFormatDirect(int level, char const* fmt, ...);
	int Cmd(uint16_t size);
	int SetFeatureFlags();
	int SetNominalBitTiming();
	int SetDataBitTiming();
	static DWORD WINAPI OnDeviceNotification(
		HCMNOTIFICATION hNotify,
		PVOID Context,
		CM_NOTIFY_ACTION Action,
		PCM_NOTIFY_EVENT_DATA EventData,
		DWORD EventDataSize);
	void OnDeviceGone();
	void OnDeviceDiscovered();
	

private:
	struct com_device_mm_data_private {
		HANDLE file;
		HANDLE ev;
		sc_can_mm_header* hdr;
		uint32_t index;
	};

	struct com_device_txr_data {
		std::atomic<uint32_t> index;
	};

	struct com_device_data_private {
		com_device_mm_data_private rx;
		com_device_mm_data_private tx;
		XSuperCANDevice* com_device;
	};

	enum {
		NOTIFICATION_NONE,
		NOTIFICATION_SHUTDOWN,
		NOTIFICATION_SET,
		NOTIFICATION_ADD,
		NOTIFICATION_REMOVE,
	};

	enum {
		LOG_BUFFER_SIZE = 116
	};

	struct log_entry {
		//uint64_t timestamp_qpc;
		int8_t level;
		uint8_t src;
		uint8_t bytes;
		uint8_t reserved;
		char data[LOG_BUFFER_SIZE];
	};

private:
	void StoreLogMessage(log_entry const * const e);

private:
	std::wstring m_Name;
	std::string m_DeviceName;
	sc_dev_t* m_Device;
	HCMNOTIFICATION m_DeviceInstanceNotification;
	sc_cmd_ctx_t m_CmdCtx;
	sc_can_stream_t *m_Stream;
	sc_dev_time_tracker m_TimeTracker;
	sc_msg_bittiming m_Nm, m_Dt;
	uint32_t m_FeatureFlags;
	HANDLE m_ThreadNotificationAcknowledgeCount;
	HANDLE m_RxThreadNotificationEvent;
	HANDLE m_TxThreadNotificationEvent;
	HANDLE m_RxThread;
	HANDLE m_TxThread;
	HANDLE m_TxFifoAvailable;
	HANDLE m_LogEvent;
	CRITICAL_SECTION m_Lock;
	CRITICAL_SECTION m_LogLock;
	com_device_data m_ComDeviceData[MAX_COM_DEVICES_PER_SC_DEVICE];
	com_device_data_private m_ComDeviceDataPrivate[MAX_COM_DEVICES_PER_SC_DEVICE];
	sc_com_dev_index_t m_ConfigurationAccessIndex;
	com_device_txr_data m_TxrMap[256];
	sc_mm_can_tx m_TxEchoMap[256];
	DWORD m_ConfigurationAccessClaimed;
	std::atomic_int m_LogLevel;
	std::atomic<uint8_t> m_RxThreadNotificationCode;
	std::atomic<uint8_t> m_TxThreadNotificationCode;
	std::atomic<uint8_t> m_RxThreadNotificationValue;
	std::atomic<uint8_t> m_TxThreadNotificationValue;
	bool m_Mapped;
	bool m_Initialized;
	bool m_Opened;
	bool m_OnBus;
	bool m_Gone;
	sc_com_dev_index_t m_RxThreadLiveComDevBuffer[MAX_COM_DEVICES_PER_SC_DEVICE];
	sc_com_dev_index_t m_RxThreadLiveComDevCount;
	uint32_t m_LogLost;
	unsigned m_LogRingGetIndex; // full range
	unsigned m_LogRingPutIndex; // full range
	log_entry m_LogRingBuffer[64];
};

using ScDevPtr = std::shared_ptr<ScDev>;


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


class ATL_NO_VTABLE XSuperCANDevice :
	public ATL::CComObjectRoot, // need lock for ScDev
	public ISuperCANDevice2
{
public:
	BEGIN_COM_MAP(XSuperCANDevice)
		COM_INTERFACE_ENTRY(ISuperCANDevice)
		COM_INTERFACE_ENTRY(ISuperCANDevice2)
	END_COM_MAP()
public:
	~XSuperCANDevice();
	XSuperCANDevice();

public:
	STDMETHOD(AcquireConfigurationAccess)(
		boolean* config_access, 
		unsigned long* timeout_ms);
	STDMETHOD(ReleaseConfigurationAccess)();
	STDMETHOD(GetRingBufferMappings)(SuperCANRingBufferMapping* rx, SuperCANRingBufferMapping* tx);
	STDMETHOD(SetBus)(boolean on);
	STDMETHOD(SetFeatureFlags)(unsigned long flags);
	STDMETHOD(SetNominalBitTiming)(SuperCANBitTimingParams params);
	STDMETHOD(SetDataBitTiming)(SuperCANBitTimingParams params);
	STDMETHOD(GetDeviceData)(SuperCANDeviceData* data);
	STDMETHOD(SetLogLevel)(int level);
	void Init(const ScDevPtr& dev, sc_com_dev_index_t index, com_device_data* mm);
	void SetSuperCAN(ISuperCAN2* sc);

private:
	ScDevPtr m_SharedDevice;
	ISuperCAN2* m_Sc;
	com_device_data* m_Mm;
	sc_com_dev_index_t m_Index;
};



// COM object is an implementation detail
// that's why the interface map is empty.
class ATL_NO_VTABLE XSuperCAN :
	public ATL::CComObjectRoot,
	public ISuperCAN2
{
public:
	BEGIN_COM_MAP(XSuperCAN)
	END_COM_MAP()
	
public:
	static ATL::CComObject<XSuperCAN>* GetOrCreate();

public:
	STDMETHOD(DeviceScan)(unsigned long* count);
	STDMETHOD(DeviceGetCount)(unsigned long* count);
	STDMETHOD(DeviceOpen)(
		unsigned long index, 
		ISuperCANDevice** dev);
	STDMETHOD(GetVersion)(SuperCANVersion* version);
	STDMETHOD(SetLogLevel)(int level);

protected:
	~XSuperCAN();
	XSuperCAN();

private:	
	static void Log(void* ctx, int level, char const* msg, size_t bytes);
	void Log(int level, char const* msg, size_t bytes);
	static void DestroyInstanceLock();
	static ATL::CComObject<XSuperCAN>* CreateInstanceLock();

private:
	std::vector<ScDevPtr> m_Devices;

private:
	static CRITICAL_SECTION ms_Lock;
	static ATL::CComObject<XSuperCAN>* ms_Instance;
};


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////



ScDev::~ScDev()
{
	Uninit();

	DeleteCriticalSection(&m_Lock);
	DeleteCriticalSection(&m_LogLock);

	if (m_LogEvent) {
		CloseHandle(m_LogEvent);
	}
}

ScDev::ScDev()
{
	m_Device = nullptr;
	m_Stream = nullptr;
	ZeroMemory(&m_CmdCtx, sizeof(m_CmdCtx));
	m_RxThread = nullptr;
	m_TxThread = nullptr;
	InitializeCriticalSection(&m_Lock);
	InitializeCriticalSection(&m_LogLock);
	m_LogEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	
	
	m_ConfigurationAccessIndex = MAX_COM_DEVICES_PER_SC_DEVICE;
	m_RxThreadNotificationCode = NOTIFICATION_NONE;
	m_TxThreadNotificationCode = NOTIFICATION_NONE;
	m_RxThreadNotificationValue = 0;
	m_TxThreadNotificationValue = 0;
	

	ZeroMemory(m_ComDeviceData, sizeof(m_ComDeviceData));
	ZeroMemory(m_ComDeviceDataPrivate, sizeof(m_ComDeviceDataPrivate));
	
	/* Create a unique id for this device.
	 * We could, of course, also use Windows USB device name
	 * passed in Init. However, that has the downside of having
	 * to deal with a dynamically sized string.
	 */
	wchar_t guid_str[48];
	GUID guid;

	ZeroMemory(&guid, sizeof(guid));
	auto hr = CoCreateGuid(&guid);
	assert(SUCCEEDED(hr));
	(void)hr;

	_snwprintf_s(
		guid_str, 
		_countof(guid_str), 
		_TRUNCATE, 
		L"%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x", 
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceData); ++i) {
		auto* data = &m_ComDeviceData[i];
		_snwprintf_s(data->rx.mem_name, _countof(data->rx.mem_name), _TRUNCATE, L"Local\\sc-i%s-com%u-rx-mem", guid_str, i);
		_snwprintf_s(data->rx.ev_name, _countof(data->rx.ev_name), _TRUNCATE, L"Local\\sc-i%s-com%u-rx-ev", guid_str, i);
		_snwprintf_s(data->tx.mem_name, _countof(data->tx.mem_name), _TRUNCATE, L"Local\\sc-i%s-com%u-tx-mem", guid_str, i);
		_snwprintf_s(data->tx.ev_name, _countof(data->tx.ev_name), _TRUNCATE, L"Local\\sc-i%s-com%u-tx-ev", guid_str, i);
		data->rx.elements = 1u<<16;
		data->tx.elements = 1u<<16;
	}

	m_TxFifoAvailable = nullptr;
	m_ThreadNotificationAcknowledgeCount = nullptr;
	m_RxThreadNotificationEvent = nullptr;
	m_TxThreadNotificationEvent = nullptr;
	m_ConfigurationAccessClaimed = 0;
	ZeroMemory(&m_TimeTracker, sizeof(m_TimeTracker));
	m_Mapped = false;
	m_Initialized = false;
	ZeroMemory(m_TxEchoMap, sizeof(m_TxEchoMap));
	m_RxThreadLiveComDevCount = 0;
	ZeroMemory(m_RxThreadLiveComDevBuffer, sizeof(m_RxThreadLiveComDevBuffer));
	ResetTxrMap();
	m_LogLevel = SC_DLL_LOG_LEVEL_OFF;
	m_LogRingGetIndex = 0;
	m_LogRingPutIndex = 0;
	m_LogLost = 0;
	ZeroMemory(m_LogRingBuffer, sizeof(m_LogRingBuffer));
	m_OnBus = false;
	m_Opened = false;
	m_Gone = false;
	memset(&m_Nm, 0, sizeof(m_Nm));
	memset(&m_Dt, 0, sizeof(m_Dt));
	m_FeatureFlags = 0;

	m_Nm.id = SC_MSG_NM_BITTIMING;
	m_Nm.len = sizeof(m_Nm);
	m_Dt.id = SC_MSG_DT_BITTIMING;
	m_Dt.len = sizeof(m_Dt);

	ZeroMemory(&dev_info, sizeof(dev_info));
	ZeroMemory(&can_info, sizeof(can_info));

	m_DeviceInstanceNotification = nullptr;
}

void ScDev::ResetTxrMap()
{
	for (size_t i = 0; i < _countof(m_TxrMap); ++i) {
		m_TxrMap[i].index.store(MAX_COM_DEVICES_PER_SC_DEVICE, std::memory_order_relaxed);
	}
}

bool ScDev::VerifyConfigurationAccess(sc_com_dev_index_t index) const
{
	assert(index < MAX_COM_DEVICES_PER_SC_DEVICE);

	auto now = GetTickCount();
	auto elapsed = now - m_ConfigurationAccessClaimed;

	if (index == m_ConfigurationAccessIndex &&
		elapsed < CONFIG_ACCESS_TIMEOUT_MS) {
		return true;
	}

	return false;
}

bool ScDev::AcquireConfigurationAccess(
	sc_com_dev_index_t index,
	unsigned long* timeout_ms)
{
	assert(index < MAX_COM_DEVICES_PER_SC_DEVICE);
	assert(timeout_ms);
	assert(m_Initialized);

	Guard g(m_Lock);

	*timeout_ms = CONFIG_ACCESS_TIMEOUT_MS;

	DWORD now = GetTickCount();

	// Special case: reclaim configuration access after having
	// claimed it last. This is to go off bus in a multi-client 
	// scenario.
	if (m_ConfigurationAccessIndex == index) {
		m_ConfigurationAccessClaimed = now;
		return true;
	}
	
	DWORD elapsed = now - m_ConfigurationAccessClaimed;

	size_t clients = 0;

	for (size_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
		clients += m_ComDeviceDataPrivate[i].com_device != nullptr;
	}

	// Don't allow configuration access to some other client in a 
	// multi-client scenario after the bus was configured and enabled.
	if (clients > 1 && m_OnBus) {
		return false;
	}

	// Allow configuration access if no client has it (first come, 
	// first serve) or the previous access is expired.
	if (m_ConfigurationAccessIndex == MAX_COM_DEVICES_PER_SC_DEVICE
		|| elapsed > CONFIG_ACCESS_TIMEOUT_MS) {
		m_ConfigurationAccessIndex = index;
		m_ConfigurationAccessClaimed = now;
		return true;
	}

	return false;
}

void ScDev::ReleaseConfigurationAccess(sc_com_dev_index_t index)
{
	assert(index < MAX_COM_DEVICES_PER_SC_DEVICE);
	assert(m_Initialized);

	Guard g(m_Lock);

	if (index == m_ConfigurationAccessIndex) {
		// Just reset timeout, keep marker in case we 
		// want to later on reclaim to go off bus.
		m_ConfigurationAccessClaimed = 0;
	}
}

void ScDev::StoreLogMessage(log_entry const * const e)
{
	for (size_t offset = 0; offset < e->bytes; ) {
		uint8_t count = 0;
		uint8_t flags = 0;

		if (offset + SC_LOG_DATA_BUFFER_SIZE < e->bytes) {
			flags |= SC_LOG_DATA_FLAG_MORE;
			count = SC_LOG_DATA_BUFFER_SIZE;
		}
		else {
			count = static_cast<uint8_t>(e->bytes - offset);
		}

		for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
			auto com_dev_index = m_RxThreadLiveComDevBuffer[i];
			auto* data = &m_ComDeviceData[com_dev_index];
			auto* priv = &m_ComDeviceDataPrivate[com_dev_index];
			auto gi = priv->rx.hdr->get_index;
			auto pi = priv->rx.hdr->put_index;
			auto used = pi - gi;

			if (pi != priv->rx.index || used > data->rx.elements) {
				// rogue client
			}
			else if (used == data->rx.elements) { // just be safe, could be a rogue client
				InterlockedIncrement(&priv->rx.hdr->log_lost);
				SetEvent(priv->rx.ev);
			}
			else {
				uint32_t index = priv->rx.index % data->rx.elements;
				sc_can_mm_slot_t* slot = &priv->rx.hdr->elements[index];

				slot->log_data.type = SC_MM_DATA_TYPE_LOG_DATA;
				slot->log_data.level = e->level;
				slot->log_data.src = e->src;
				//slot->log_data.timestamp_qpc = e->timestamp_qpc;
				slot->log_data.flags = flags;
				slot->log_data.bytes = count;
				memcpy(slot->log_data.data, e->data + offset, count);

				++priv->rx.index;

				priv->rx.hdr->put_index = priv->rx.index;

				SetEvent(priv->rx.ev);
			}
		}

		offset += count;
	}
}

int ScDev::SetLogLevel(sc_com_dev_index_t index, int level)
{
	assert(m_Device);
	assert(m_Initialized);

	Guard g1(m_Lock);

	if (!VerifyConfigurationAccess(index)) {
		return SC_DLL_ERROR_ACCESS_DENIED;
	}

	Guard g2(m_LogLock);

	m_LogLevel = level;

	sc_dev_log_set_level(m_Device, level);

	return SC_DLL_ERROR_NONE;;
}

int ScDev::SetFeatureFlags(sc_com_dev_index_t index, uint32_t flags)
{
	assert(m_Initialized);

	Guard g(m_Lock);

	if (!VerifyConfigurationAccess(index)) {
		return SC_DLL_ERROR_ACCESS_DENIED;
	}

	flags &= ~SC_FEATURE_FLAG_TXR;

	if ((flags & (dev_info.feat_conf | dev_info.feat_perm)) != flags) {
		return SC_DLL_ERROR_INVALID_PARAM;
	}

	m_FeatureFlags = flags | SC_FEATURE_FLAG_TXR;

	return SC_DLL_ERROR_NONE;
}

int ScDev::SetFeatureFlags()
{
	
	sc_msg_features* feat = reinterpret_cast<sc_msg_features*>(m_CmdCtx.tx_buffer);
	feat->id = SC_MSG_FEATURES;
	feat->op = SC_FEAT_OP_CLEAR;
	feat->len = sizeof(*feat);
	feat->arg = 0;

	auto error = Cmd(feat->len);

	if (SC_DLL_ERROR_NONE != error) {
		return error;
	}

	feat->op = SC_FEAT_OP_OR;
	feat->arg = m_Device->dev_to_host32(m_FeatureFlags);

	return Cmd(feat->len);
}

int ScDev::SetNominalBitTiming(sc_com_dev_index_t index, SuperCANBitTimingParams params)
{
	assert(m_Initialized);

	Guard g(m_Lock);

	if (!VerifyConfigurationAccess(index)) {
		return SC_DLL_ERROR_ACCESS_DENIED;
	}

	if (params.brp < can_info.nmbt_brp_min || params.brp > can_info.nmbt_brp_max ||
		params.sjw < 1 || params.sjw > can_info.nmbt_sjw_max ||
		params.tseg1 < can_info.nmbt_tseg1_min || params.tseg1 > can_info.nmbt_tseg1_max ||
		params.tseg2 < can_info.nmbt_tseg2_min || params.tseg2 > can_info.nmbt_tseg2_max) {
		return SC_DLL_ERROR_INVALID_PARAM;
	}

	m_Nm.brp = params.brp;
	m_Nm.sjw = params.sjw;
	m_Nm.tseg1 = params.tseg1;
	m_Nm.tseg2 = params.tseg2;

	return SC_DLL_ERROR_NONE;
}

int ScDev::SetNominalBitTiming()
{
	sc_msg_bittiming* bt = reinterpret_cast<sc_msg_bittiming*>(m_CmdCtx.tx_buffer);
	bt->id = SC_MSG_NM_BITTIMING;
	bt->len = sizeof(*bt);
	bt->brp = m_Device->dev_to_host16(m_Nm.brp);
	bt->sjw = m_Nm.sjw;
	bt->tseg1 = m_Device->dev_to_host16(m_Nm.tseg1);
	bt->tseg2 = m_Nm.tseg2;

	return Cmd(bt->len);
}

int ScDev::SetDataBitTiming(sc_com_dev_index_t index, SuperCANBitTimingParams params)
{
	assert(m_Initialized);

	Guard g(m_Lock);

	if (!VerifyConfigurationAccess(index)) {
		return SC_DLL_ERROR_ACCESS_DENIED;
	}

	if (params.brp < can_info.dtbt_brp_min || params.brp > can_info.dtbt_brp_max ||
		params.sjw < 1 || params.sjw > can_info.dtbt_sjw_max ||
		params.tseg1 < can_info.dtbt_tseg1_min || params.tseg1 > can_info.dtbt_tseg1_max ||
		params.tseg2 < can_info.dtbt_tseg2_min || params.tseg2 > can_info.dtbt_tseg2_max) {
		return SC_DLL_ERROR_INVALID_PARAM;
	}

	m_Dt.brp = params.brp;
	m_Dt.sjw = params.sjw;
	m_Dt.tseg1 = params.tseg1;
	m_Dt.tseg2 = params.tseg2;

	return SC_DLL_ERROR_NONE;
}

int ScDev::SetDataBitTiming()
{
	sc_msg_bittiming* bt = reinterpret_cast<sc_msg_bittiming*>(m_CmdCtx.tx_buffer);
	bt->id = SC_MSG_DT_BITTIMING;
	bt->len = sizeof(*bt);
	bt->brp = m_Device->dev_to_host16(m_Dt.brp);
	bt->sjw = m_Dt.sjw;
	bt->tseg1 = m_Device->dev_to_host16(m_Dt.tseg1);
	bt->tseg2 = m_Dt.tseg2;

	return Cmd(bt->len);
}

DWORD ScDev::OnDeviceNotification(
	HCMNOTIFICATION hNotify,
	PVOID Context,
	CM_NOTIFY_ACTION Action,
	PCM_NOTIFY_EVENT_DATA EventData,
	DWORD EventDataSize)
{
	(void)hNotify;
	(void)EventDataSize;
	(void)EventData;

	switch (Action) {
	case CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED:
		// sent prior to 'started' 
		break;
	case CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED:
		static_cast<ScDev*>(Context)->OnDeviceDiscovered();
		break;
	case CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED:
		static_cast<ScDev*>(Context)->OnDeviceGone();
		break;
	}

	return ERROR_SUCCESS;
}

void ScDev::OnDeviceGone()
{
	Guard g(m_Lock);

	BusCleanup();

	CloseDevice();
	
	m_Gone = true;

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
		InterlockedOr((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->flags, SC_MM_FLAG_GONE);
		InterlockedOr((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->flags, SC_MM_FLAG_GONE);

		SetEvent(m_ComDeviceDataPrivate[i].rx.ev);
	}

	LOG_SRV(SC_DLL_LOG_LEVEL_INFO, "%s: gone\n", m_DeviceName.c_str());
}

void ScDev::OnDeviceDiscovered()
{
	Guard g(m_Lock);

	// re-open the device to indicate it is 'back' 
	auto error = OpenDevice();

	if (error) {
		SetDeviceError(error);
	}
	else {
		m_Gone = false;

		for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
			InterlockedAnd((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->flags, ~SC_MM_FLAG_GONE);
			InterlockedAnd((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->flags, ~SC_MM_FLAG_GONE);
			InterlockedIncrement((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->generation);
			InterlockedIncrement((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->generation);

			SetEvent(m_ComDeviceDataPrivate[i].rx.ev);
		}

		LOG_SRV(SC_DLL_LOG_LEVEL_INFO, "%s: discovered gen=%lu\n", m_DeviceName.c_str(), m_ComDeviceDataPrivate[0].rx.hdr->generation);
	}
}

int ScDev::Cmd(uint16_t bytes_in)
{
	assert(m_Initialized);

	sc_msg_error* e = reinterpret_cast<sc_msg_error*>(m_CmdCtx.rx_buffer);
	uint16_t bytes_out = 0;
	auto error = sc_cmd_ctx_run(&m_CmdCtx, bytes_in, &bytes_out, CMD_TIMEOUT_MS);

	if (error) {
		return error;
	}

	if (bytes_out < sizeof(*e)) {
		return SC_DLL_ERROR_PROTO_VIOLATION;
	}

	if (e->id != SC_MSG_ERROR || e->len < sizeof(*e)) {
		return SC_DLL_ERROR_PROTO_VIOLATION;
	}

	return map_device_error(e->error);
}

int ScDev::OnRx(void* ctx, void const* ptr, uint16_t bytes)
{
	return static_cast<ScDev*>(ctx)->OnRx(static_cast<sc_msg_header const*>(ptr), bytes);
}

int ScDev::OnRx(sc_msg_header const* _msg, unsigned bytes)
{
	sc_msg_header* msg = const_cast<sc_msg_header*>(_msg);

	(void)bytes; // fingers crossed

	switch (msg->id) {
	case SC_MSG_CAN_TXR: {
		sc_msg_can_txr *txr = reinterpret_cast<sc_msg_can_txr*>(msg);
		uint64_t ts = 0;
		sc_com_dev_index_t tx_com_dev_index = 0;
		
		txr->timestamp_us = m_Device->dev_to_host32(txr->timestamp_us);
		ts = sc_tt_track(&m_TimeTracker, txr->timestamp_us);

		tx_com_dev_index = static_cast<sc_com_dev_index_t>(m_TxrMap[txr->track_id].index.load(std::memory_order_acquire));

		if (tx_com_dev_index < MAX_COM_DEVICES_PER_SC_DEVICE) {
			auto const* echo = &m_TxEchoMap[txr->track_id];
			auto data_len = dlc_to_len(echo->dlc);

			for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
				auto com_dev_index = m_RxThreadLiveComDevBuffer[i];
				auto* data = &m_ComDeviceData[com_dev_index];
				auto* priv = &m_ComDeviceDataPrivate[com_dev_index];
				auto gi = priv->rx.hdr->get_index;
				auto pi = priv->rx.hdr->put_index;
				auto used = pi - gi;

				if (pi != priv->rx.index || used > data->rx.elements) {
					// rogue client
				}
				else if (used == data->rx.elements) { // just be safe, could be a rogue client
					InterlockedIncrement(&priv->rx.hdr->can_lost_tx);
					SetEvent(priv->rx.ev);
				}
				else {
					uint32_t const slot_index = priv->rx.index % data->rx.elements;
					sc_can_mm_slot_t* slot = &priv->rx.hdr->elements[slot_index];

					slot->tx.type = SC_MM_DATA_TYPE_CAN_TX;
					slot->tx.can_id = echo->can_id;
					slot->tx.flags = txr->flags;
					slot->tx.dlc = echo->dlc;
					slot->tx.track_id = echo->track_id;
					slot->tx.timestamp_us = ts;
					memcpy(slot->tx.data, echo->data, data_len);

					slot->tx.echo = tx_com_dev_index == com_dev_index;

					++priv->rx.index;

					priv->rx.hdr->put_index = priv->rx.index;

					SetEvent(priv->rx.ev);
				}
			}

			m_TxrMap[txr->track_id].index.store(MAX_COM_DEVICES_PER_SC_DEVICE, std::memory_order_release);
			ReleaseSemaphore(m_TxFifoAvailable, 1, nullptr);
		}
		else {
			// happens for instance if a COM devices is removed and there are outstanding TXs
		}
	} break;
	case SC_MSG_CAN_RX: {
		sc_msg_can_rx* rx = reinterpret_cast<sc_msg_can_rx*>(msg);
		uint64_t ts = 0;

		rx->can_id = m_Device->dev_to_host32(rx->can_id);
		rx->timestamp_us = m_Device->dev_to_host32(rx->timestamp_us);
		ts = sc_tt_track(&m_TimeTracker, rx->timestamp_us);

		for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
			auto com_dev_index = m_RxThreadLiveComDevBuffer[i];
			auto* data = &m_ComDeviceData[com_dev_index];
			auto* priv = &m_ComDeviceDataPrivate[com_dev_index];
			auto gi = priv->rx.hdr->get_index;
			auto pi = priv->rx.hdr->put_index;
			auto used = pi - gi;

			if (pi != priv->rx.index || used > data->rx.elements) {
				// rogue client
			}
			else if (used == data->rx.elements) { // just be safe, could be a rogue client
				InterlockedIncrement(&priv->rx.hdr->can_lost_rx);
				SetEvent(priv->rx.ev);
			}
			else {
				uint32_t index = priv->rx.index % data->rx.elements;
				sc_can_mm_slot_t* slot = &priv->rx.hdr->elements[index];
				slot->rx.type = SC_MM_DATA_TYPE_CAN_RX;
				slot->rx.can_id = rx->can_id;
				slot->rx.dlc = rx->dlc;
				slot->rx.flags = rx->flags;
				slot->rx.timestamp_us = ts;

				if (!(slot->rx.flags & SC_CAN_FRAME_FLAG_RTR)) {
					memcpy(slot->rx.data, rx->data, dlc_to_len(rx->dlc));
				}

				++priv->rx.index;

				priv->rx.hdr->put_index = priv->rx.index;

				//std::atomic_thread_fence(std::memory_order_release);

				SetEvent(priv->rx.ev);
			}
		}
	} break;
	case SC_MSG_CAN_STATUS: {
		sc_msg_can_status* status = reinterpret_cast<sc_msg_can_status*>(msg);
		uint64_t ts = 0;

		status->rx_lost = m_Device->dev_to_host16(status->rx_lost);
		status->tx_dropped = m_Device->dev_to_host16(status->tx_dropped);
		status->timestamp_us = m_Device->dev_to_host32(status->timestamp_us);
		ts = sc_tt_track(&m_TimeTracker, status->timestamp_us);

		for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
			auto com_dev_index = m_RxThreadLiveComDevBuffer[i];
			auto* data = &m_ComDeviceData[com_dev_index];
			auto* priv = &m_ComDeviceDataPrivate[com_dev_index];
			auto gi = priv->rx.hdr->get_index;
			auto pi = priv->rx.hdr->put_index;
			auto used = pi - gi;

			if (pi != priv->rx.index || used > data->rx.elements) {
				// rogue client
			}
			else if (used == data->rx.elements) { // just be safe, could be a rogue client
				InterlockedIncrement(&priv->rx.hdr->can_lost_status);
				SetEvent(priv->rx.ev);
			}
			else {
				uint32_t index = priv->rx.index % data->rx.elements;
				sc_can_mm_slot_t* slot = &priv->rx.hdr->elements[index];

				slot->status.type = SC_MM_DATA_TYPE_CAN_STATUS;
				slot->status.flags = status->flags;
				slot->status.bus_status = status->bus_status;
				slot->status.timestamp_us = ts;
				slot->status.rx_lost = status->rx_lost;
				slot->status.tx_dropped = status->tx_dropped;
				slot->status.rx_errors = status->rx_errors;
				slot->status.tx_errors = status->tx_errors;
				slot->status.rx_fifo_size = status->rx_fifo_size;
				slot->status.tx_fifo_size = status->tx_fifo_size;


				++priv->rx.index;

				priv->rx.hdr->put_index = priv->rx.index;

				SetEvent(priv->rx.ev);
			}
		}
	} break;
	case SC_MSG_CAN_ERROR: {
		auto* error = reinterpret_cast<sc_msg_can_error*>(msg);
		uint64_t ts = 0;

		error->timestamp_us = m_Device->dev_to_host32(error->timestamp_us);
		ts = sc_tt_track(&m_TimeTracker, error->timestamp_us);

		for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
			auto com_dev_index = m_RxThreadLiveComDevBuffer[i];
			auto* data = &m_ComDeviceData[com_dev_index];
			auto* priv = &m_ComDeviceDataPrivate[com_dev_index];
			auto gi = priv->rx.hdr->get_index;
			auto pi = priv->rx.hdr->put_index;
			auto used = pi - gi;

			if (pi != priv->rx.index || used > data->rx.elements) {
				// rogue client
			}
			else if (used == data->rx.elements) { // just be safe, could be a rogue client
				InterlockedIncrement(&priv->rx.hdr->can_lost_error);
				SetEvent(priv->rx.ev);
			}
			else {
				uint32_t index = priv->rx.index % data->rx.elements;
				sc_can_mm_slot_t* slot = &priv->rx.hdr->elements[index];

				slot->error.type = SC_MM_DATA_TYPE_CAN_ERROR;
				slot->error.flags = error->flags;
				slot->error.timestamp_us = ts;
				slot->error.error = error->error;

				++priv->rx.index;

				priv->rx.hdr->put_index = priv->rx.index;

				SetEvent(priv->rx.ev);
			}
		}
	} break;
	}

	return SC_DLL_ERROR_NONE;
}

void ScDev::Uninit()
{
	if (m_Initialized) {
		if (m_OnBus) {
			SetBusOff();
		}
		else {
			BusCleanup();
		}

		CloseDevice();
	}

	if (m_DeviceInstanceNotification) {
		CM_Unregister_Notification(m_DeviceInstanceNotification);
		m_DeviceInstanceNotification = nullptr;
	}

	Unmap();

	m_LogLevel = SC_DLL_LOG_LEVEL_OFF;

	m_Name.clear();

	m_Initialized = false;
}

void ScDev::Unmap()
{
	m_Mapped = false;

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceData); ++i) {
		//auto* data = &m_ComDeviceData[i];
		auto* priv = &m_ComDeviceDataPrivate[i];

		if (priv->rx.hdr) {
			UnmapViewOfFile(priv->rx.hdr);
			priv->rx.hdr = nullptr;
		}

		if (priv->tx.hdr) {
			UnmapViewOfFile(priv->tx.hdr);
			priv->tx.hdr = nullptr;
		}

		if (priv->rx.file) {
			CloseHandle(priv->rx.file);
			priv->rx.file = nullptr;
		}

		if (priv->tx.file) {
			CloseHandle(priv->tx.file);
			priv->tx.file = nullptr;
		}

		if (priv->rx.ev) {
			CloseHandle(priv->rx.ev);
			priv->rx.ev = nullptr;
		}

		if (priv->tx.ev) {
			CloseHandle(priv->tx.ev);
			priv->tx.ev = nullptr;
		}
	}
}

int ScDev::Map()
{
	int error = SC_DLL_ERROR_NONE;

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceData); ++i) {
		auto* data = &m_ComDeviceData[i];
		auto* priv = &m_ComDeviceDataPrivate[i];

		uint64_t bytes = data->rx.elements * sizeof(sc_can_mm_slot_t) + sizeof(sc_can_mm_header);
		priv->rx.file = CreateFileMappingW(
			INVALID_HANDLE_VALUE, // hFile -> page file
			NULL, // lpFileMappingAttributes
			PAGE_READWRITE, // flProtect
			static_cast<DWORD>(bytes >> 32), // dwMaximumSizeHigh
			static_cast<DWORD>(bytes), // dwMaximumSizeLow
			data->rx.mem_name); // lpName

		if (nullptr == priv->rx.file) {
			error = SC_DLL_ERROR_OUT_OF_MEM;
			goto error_exit;
		}


		priv->rx.hdr = static_cast<sc_can_mm_header*>(MapViewOfFile(
			priv->rx.file,
			FILE_MAP_READ | FILE_MAP_WRITE,
			0,
			0,
			static_cast<SIZE_T>(bytes)));

		if (!priv->rx.hdr) {
			error = SC_DLL_ERROR_OUT_OF_MEM;
			goto error_exit;
		}

		memset(priv->rx.hdr, 0, sizeof(*priv->rx.hdr));

		bytes = data->tx.elements * sizeof(sc_can_mm_slot_t) + sizeof(sc_can_mm_header);
		priv->tx.file = CreateFileMappingW(
			INVALID_HANDLE_VALUE, // hFile -> page file
			NULL, // lpFileMappingAttributes
			PAGE_READWRITE, // flProtect
			static_cast<DWORD>(bytes >> 32), // dwMaximumSizeHigh
			static_cast<DWORD>(bytes), // dwMaximumSizeLow
			data->tx.mem_name); // lpName

		if (nullptr == priv->tx.file) {
			error = SC_DLL_ERROR_OUT_OF_MEM;
			goto error_exit;
		}


		priv->tx.hdr = static_cast<sc_can_mm_header*>(MapViewOfFile(
			priv->tx.file,
			FILE_MAP_READ | FILE_MAP_WRITE,
			0,
			0,
			static_cast<SIZE_T>(bytes)));

		if (!priv->tx.hdr) {
			error = SC_DLL_ERROR_OUT_OF_MEM;
			goto error_exit;
		}

		memset(priv->tx.hdr, 0, sizeof(*priv->tx.hdr));

		// events
		priv->rx.ev = CreateEventW(nullptr, FALSE, FALSE, data->rx.ev_name);
		if (!priv->rx.ev) {
			error = SC_DLL_ERROR_OUT_OF_MEM;
			goto error_exit;
		}

		priv->tx.ev = CreateEventW(nullptr, FALSE, FALSE, data->tx.ev_name);
		if (!priv->tx.ev) {
			error = SC_DLL_ERROR_OUT_OF_MEM;
			goto error_exit;
		}
	}

	m_Mapped = true;

error_success:
	return error;

error_exit:
	Unmap();
	goto error_success;
}

void ScDev::Log(void* ctx, int level, const char* msg, size_t bytes)
{
	static_cast<ScDev*>(ctx)->Log(level, msg, bytes);
}

void ScDev::Log(int level, const char* msg, size_t bytes)
{
	Guard g(m_LogLock);

	unsigned used = m_LogRingPutIndex - m_LogRingGetIndex;

	if (used < _countof(m_LogRingBuffer)) {
		const unsigned index = m_LogRingPutIndex % _countof(m_LogRingBuffer);
		log_entry* const e = &m_LogRingBuffer[index];

		//QueryPerformanceCounter((LARGE_INTEGER*)&e->timestamp_qpc);
		e->level = static_cast<int8_t>(level);
		e->src = SC_LOG_DATA_SRC_DLL;
		e->bytes = static_cast<uint8_t>(std::min(_countof(e->data) - 1, bytes));
		memcpy(e->data, msg, e->bytes);
		e->data[e->bytes] = 0;

		++m_LogRingPutIndex;

		SetEvent(m_LogEvent);
	}
	else {
		++m_LogLost;

		OutputDebugStringA("SC SRV: log ring buffer full\n");
		// Should we still log?
	}
}

void ScDev::LogFormatQueue(int level, const char* fmt, ...)
{
	if (level <= m_LogLevel.load(std::memory_order_relaxed)) {

		Guard g(m_LogLock);

		unsigned used = m_LogRingPutIndex - m_LogRingGetIndex;

		if (used < _countof(m_LogRingBuffer)) {
			const unsigned index = m_LogRingPutIndex % _countof(m_LogRingBuffer);
			log_entry* const e = &m_LogRingBuffer[index];
			va_list vl;

			e->level = static_cast<int8_t>(level);
			e->src = SC_LOG_DATA_SRC_SRV;
			//QueryPerformanceCounter((LARGE_INTEGER*)&e->timestamp_qpc);

			va_start(vl, fmt);
			
			e->bytes = (uint8_t)_vsnprintf_s(e->data, _countof(e->data), _TRUNCATE, fmt, vl);

			va_end(vl);

			++m_LogRingPutIndex;

			SetEvent(m_LogEvent);
		}
		else {
			++m_LogLost;

			OutputDebugStringA("SC SRV: log ring buffer full\n");
		}
	}
}


void ScDev::LogFormatDirect(int level, const char* fmt, ...)
{
	if (level <= m_LogLevel.load(std::memory_order_relaxed)) {
		log_entry e;		
		va_list vl;

		e.src = SC_LOG_DATA_SRC_SRV;
		e.level = static_cast<int8_t>(level);
		//QueryPerformanceCounter((LARGE_INTEGER*)&e.timestamp_qpc);
		

		va_start(vl, fmt);

		e.bytes = (uint8_t)_vsnprintf_s(e.data, _countof(e.data), _TRUNCATE, fmt, vl);

		va_end(vl);

		StoreLogMessage(&e);
	}
}

int ScDev::Init(std::wstring&& name)
{
	Uninit();

	m_Name = std::move(name);

	auto error = Map();

	if (error) {
		goto error_exit;
	}

	CONFIGRET cr;
	DEVPROPTYPE prop_type = 0;
	CM_NOTIFY_FILTER filter;
	ULONG size = sizeof(filter.u.DeviceInstance.InstanceId[0]) * (_countof(filter.u.DeviceInstance.InstanceId) - 1);

	ZeroMemory(&filter, sizeof(filter));

	filter.cbSize = sizeof(filter);
	filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;

	cr = CM_Get_Device_Interface_PropertyW(
		m_Name.c_str(),
		&DEVPKEY_Device_InstanceId,
		&prop_type,
		(PBYTE)filter.u.DeviceInstance.InstanceId,
		&size,
		0);

	error = sc_map_cm_error(cr);

	if (error) {
		goto error_exit;
	}

	// NOTE: the notification works even if the device is plugged into a different USB port.
	cr = CM_Register_Notification(&filter, this, &OnDeviceNotification, &m_DeviceInstanceNotification);

	error = sc_map_cm_error(cr);

	if (error) {
		goto error_exit;
	}

	error = OpenDevice();

	if (error) {
		goto error_exit;
	}

	// fetch device info
	{
		struct sc_msg_req* req = (struct sc_msg_req*)m_CmdCtx.tx_buffer;
		memset(req, 0, sizeof(*req));
		req->id = SC_MSG_DEVICE_INFO;
		req->len = sizeof(*req);
		uint16_t rep_len;
		error = sc_cmd_ctx_run(&m_CmdCtx, req->len, &rep_len, CMD_TIMEOUT_MS);
		if (error) {
			goto error_exit;
		}

		if (rep_len < sizeof(dev_info)) {
			LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "failed to get device info (too short of a reponse len=%u)\n", rep_len);
			error = SC_DLL_ERROR_PROTO_VIOLATION;
			goto error_exit;
		}

		memcpy(&dev_info, m_CmdCtx.rx_buffer, sizeof(dev_info));

		dev_info.feat_perm = m_Device->dev_to_host16(dev_info.feat_perm);
		dev_info.feat_conf = m_Device->dev_to_host16(dev_info.feat_conf);

		if (!((dev_info.feat_perm | dev_info.feat_conf) & SC_FEATURE_FLAG_TXR)) {
			LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "device doesn't support required TXR feature\n");
			error = SC_DLL_ERROR_DEV_UNSUPPORTED;
			goto error_exit;
		}

		m_DeviceName.assign(dev_info.name_bytes, dev_info.name_bytes + dev_info.name_len);

		/*fprintf(stdout, "device features perm=%#04x conf=%#04x\n", dev_info.feat_perm, dev_info.feat_conf);

		for (size_t i = 0; i < min((size_t)dev_info.sn_len, _countof(serial_str) - 1); ++i) {
			snprintf(&serial_str[i * 2], 3, "%02x", dev_info.sn_bytes[i]);
		}

		dev_info.name_len = min((size_t)dev_info.name_len, sizeof(name_str) - 1);
		memcpy(name_str, dev_info.name_bytes, dev_info.name_len);
		name_str[dev_info.name_len] = 0;

		fprintf(stdout, "device identifies as %s, serial no %s, firmware version %u.% u.% u\n",
			name_str, serial_str, dev_info.fw_ver_major, dev_info.fw_ver_minor, dev_info.fw_ver_patch);*/

	}

	// fetch can info
	{
		struct sc_msg_req* req = (struct sc_msg_req*)m_CmdCtx.tx_buffer;
		memset(req, 0, sizeof(*req));
		req->id = SC_MSG_CAN_INFO;
		req->len = sizeof(*req);
		uint16_t rep_len;
		error = sc_cmd_ctx_run(&m_CmdCtx, req->len, &rep_len, CMD_TIMEOUT_MS);
		if (error) {
			goto error_exit;
		}

		if (rep_len < sizeof(can_info)) {
			error = SC_DLL_ERROR_PROTO_VIOLATION;
			goto error_exit;
		}

		memcpy(&can_info, m_CmdCtx.rx_buffer, sizeof(can_info));

		can_info.can_clk_hz = m_Device->dev_to_host32(can_info.can_clk_hz);
		can_info.msg_buffer_size = m_Device->dev_to_host16(can_info.msg_buffer_size);
		can_info.nmbt_brp_max = m_Device->dev_to_host16(can_info.nmbt_brp_max);
		can_info.nmbt_tseg1_max = m_Device->dev_to_host16(can_info.nmbt_tseg1_max);


		// fill in nominal / data bit timings once
		m_Nm.brp = can_info.nmbt_brp_min;
		m_Nm.sjw = 1;
		m_Nm.tseg1 = can_info.nmbt_tseg1_min;
		m_Nm.tseg2 = can_info.nmbt_tseg2_min;

		m_Dt.brp = can_info.dtbt_brp_min;
		m_Dt.sjw = 1;
		m_Dt.tseg1 = can_info.dtbt_tseg1_min;
		m_Dt.tseg2 = can_info.dtbt_tseg2_min;
	}

	m_Initialized = true;

success_exit:
	return error;

error_exit:
	Uninit();

	goto success_exit;
}


void ScDev::CloseDevice()
{
	sc_cmd_ctx_uninit(&m_CmdCtx);
	ZeroMemory(&m_CmdCtx, sizeof(m_CmdCtx));

	if (m_Device) {
		sc_dev_close(m_Device);
		m_Device = nullptr;
	}

	m_Opened = false;
}

int ScDev::OpenDevice()
{
	int error = SC_DLL_ERROR_NONE;

	CloseDevice();

	error = sc_dev_open_by_id(m_Name.c_str(), &m_Device);
	if (error) {
		goto error_exit;
	}

	error = sc_dev_log_set_callback(m_Device, this, &ScDev::Log);
	if (error) {
		goto error_exit;
	}

	error = sc_dev_log_set_level(m_Device, m_LogLevel);
	if (error) {
		goto error_exit;
	}

	error = sc_cmd_ctx_init(&m_CmdCtx, m_Device);
	if (error) {
		goto error_exit;
	}

	m_Opened = true;

success_exit:
	return error;

error_exit:
	CloseDevice();

	goto success_exit;
}

void ScDev::BusCleanup()
{
	assert(m_Initialized);

	m_RxThreadNotificationCode.store(NOTIFICATION_SHUTDOWN, std::memory_order_release);
	m_TxThreadNotificationCode.store(NOTIFICATION_SHUTDOWN, std::memory_order_release);

	if (m_RxThreadNotificationEvent) {
		SetEvent(m_RxThreadNotificationEvent);
	}

	if (m_TxThreadNotificationEvent) {
		SetEvent(m_TxThreadNotificationEvent);
	}

	if (m_RxThread) {
		WaitForSingleObject(m_RxThread, INFINITE);
		m_RxThread = nullptr;
	}

	if (m_TxThread) {
		WaitForSingleObject(m_TxThread, INFINITE);
		m_TxThread = nullptr;
	}

	m_RxThreadNotificationCode.store(NOTIFICATION_NONE, std::memory_order_relaxed);
	m_TxThreadNotificationCode.store(NOTIFICATION_NONE, std::memory_order_relaxed);


	if (m_RxThreadNotificationEvent) {
		CloseHandle(m_RxThreadNotificationEvent);
		m_RxThreadNotificationEvent = nullptr;
	}

	if (m_TxThreadNotificationEvent) {
		CloseHandle(m_TxThreadNotificationEvent);
		m_TxThreadNotificationEvent = nullptr;
	}

	if (m_ThreadNotificationAcknowledgeCount) {
		CloseHandle(m_ThreadNotificationAcknowledgeCount);
		m_ThreadNotificationAcknowledgeCount = nullptr;
	}

	if (m_TxFifoAvailable) {
		CloseHandle(m_TxFifoAvailable);
		m_TxFifoAvailable = nullptr;
	}

	if (m_Stream) {
		sc_can_stream_uninit(m_Stream);
		m_Stream = nullptr;
	}

	ZeroMemory(&m_TimeTracker, sizeof(m_TimeTracker));

	ResetTxrMap();

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
		InterlockedAnd((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->flags, ~SC_MM_FLAG_BUS_ON);
		InterlockedAnd((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->flags, ~SC_MM_FLAG_BUS_ON);

		SetEvent(m_ComDeviceDataPrivate[i].rx.ev);
	}

	m_RxThreadLiveComDevCount = 0;

	m_OnBus = false;
}

void ScDev::SetBusOff()
{
	assert(m_Initialized);
	assert(m_Opened);

	if (m_OnBus) {
		LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: go off bus\n", m_DeviceName.c_str());

		sc_msg_config* bus = reinterpret_cast<sc_msg_config*>(m_CmdCtx.tx_buffer);
		bus->id = SC_MSG_BUS;
		bus->len = sizeof(*bus);
		bus->arg = 0;

		uint16_t bytes_out;
		sc_cmd_ctx_run(&m_CmdCtx, bus->len, &bytes_out, CMD_TIMEOUT_MS);
	}

	BusCleanup();
}

int ScDev::SetBusOn()
{
	int error = SC_DLL_ERROR_NONE;
	DWORD thread_id = 0;
	decltype(m_TxThreadNotificationValue)::value_type bitmask = 0;

	assert(m_Initialized);
	assert(m_Opened);

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: set feature flags\n", m_DeviceName.c_str());
	
	error = SetFeatureFlags();

	if (error) {
		goto error_exit;
	}

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: set nominal bit timing\n", m_DeviceName.c_str());

	error = SetNominalBitTiming();

	if (error) {
		goto error_exit;
	}

	if ((m_FeatureFlags & SC_FEATURE_FLAG_FDF) || (dev_info.feat_perm & SC_FEATURE_FLAG_FDF)) {
		LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: set data bit timing\n", m_DeviceName.c_str());

		error = SetDataBitTiming();

		if (error) {
			goto error_exit;
		}
	}

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: clear MM error\n", m_DeviceName.c_str());

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
		m_ComDeviceDataPrivate[i].rx.hdr->error = 0;
		m_ComDeviceDataPrivate[i].tx.hdr->error = 0;

		InterlockedAnd((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->flags, ~SC_MM_FLAG_ERROR);
		InterlockedAnd((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->flags, ~SC_MM_FLAG_ERROR);

		SetEvent(m_ComDeviceDataPrivate[i].rx.ev);

		if (m_ComDeviceDataPrivate[i].com_device) {
			bitmask |= static_cast<decltype(m_TxThreadNotificationValue)::value_type>(1) << i;
		}
	}

	sc_tt_init(&m_TimeTracker);

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: init CAN stream\n", m_DeviceName.c_str());

	assert(!m_Stream);
	error = sc_can_stream_init(
		m_Device,
		can_info.msg_buffer_size,
		this,
		&ScDev::OnRx,
		-1,
		&m_Stream);

	if (error) {
		goto error_exit;
	}

	assert(!m_Stream->user_handle);

	m_RxThreadNotificationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!m_RxThreadNotificationEvent) {
		error = SC_DLL_ERROR_OUT_OF_MEM;
		goto error_exit;
	}

	m_TxThreadNotificationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!m_TxThreadNotificationEvent) {
		error = SC_DLL_ERROR_OUT_OF_MEM;
		goto error_exit;
	}

	// CreateSemaphoreW fails if max count <= 0, https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createsemaphorew
	m_TxFifoAvailable = CreateSemaphoreW(nullptr, can_info.tx_fifo_size, can_info.tx_fifo_size, nullptr);
	if (!m_TxFifoAvailable) {
		error = SC_DLL_ERROR_OUT_OF_MEM;
		goto error_exit;
	}

	m_ThreadNotificationAcknowledgeCount = CreateSemaphoreW(nullptr, 0, 2, nullptr);
	if (!m_ThreadNotificationAcknowledgeCount) {
		error = SC_DLL_ERROR_OUT_OF_MEM;
		goto error_exit;
	}

	m_RxThread = CreateThread(NULL, 0, &ScDev::RxMain, this, 0, &thread_id);
	if (!m_RxThread) {
		error = SC_DLL_ERROR_OUT_OF_MEM;
		goto error_exit;
	}

	m_TxThread = CreateThread(NULL, 0, &ScDev::TxMain, this, 0, &thread_id);
	if (!m_TxThread) {
		error = SC_DLL_ERROR_OUT_OF_MEM;
		goto error_exit;
	}

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: go on bus\n", m_DeviceName.c_str());

	sc_msg_config* bus = reinterpret_cast<sc_msg_config*>(m_CmdCtx.tx_buffer);
	bus->id = SC_MSG_BUS;
	bus->len = sizeof(*bus);
	bus->arg = m_Device->dev_to_host16(1);

	uint16_t bytes_out = 0;
	error = sc_cmd_ctx_run(&m_CmdCtx, bus->len, &bytes_out, CMD_TIMEOUT_MS);
	if (error) {
		goto error_exit;
	}

	auto *e = reinterpret_cast<sc_msg_error*>(m_CmdCtx.rx_buffer);
	if (bytes_out < sizeof(*e)) {
		error = SC_DLL_ERROR_PROTO_VIOLATION;
		goto error_exit;
	}

	if (e->id != SC_MSG_ERROR || e->len < sizeof(*e)) {
		error = SC_DLL_ERROR_PROTO_VIOLATION;
		goto error_exit;
	}

	if (e->error) {
		error = map_device_error(e->error);
		goto error_exit;
	}

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
		InterlockedOr((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->flags, SC_MM_FLAG_BUS_ON);
		InterlockedOr((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->flags, SC_MM_FLAG_BUS_ON);

		SetEvent(m_ComDeviceDataPrivate[i].rx.ev);
	}

	Notify(NOTIFICATION_SET, bitmask);

	m_OnBus = true;

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "%s: we are on bus\n", m_DeviceName.c_str());

success_exit:
	return error;

error_exit:
	SetBusOff();
	goto success_exit;
}

int ScDev::SetBus(sc_com_dev_index_t index, bool on)
{
	assert(m_Initialized);

	Guard g(m_Lock);

	if (!VerifyConfigurationAccess(index)) {
		return SC_DLL_ERROR_ACCESS_DENIED;
	}

	if (m_Gone) {
		return SC_DLL_ERROR_DEV_GONE;
	}

	if (!m_Opened) {
		return SC_DLL_ERROR_INVALID_OPERATION;
	}

	SetBusOff();

	if (on) {
		return SetBusOn();
	}

	return SC_DLL_ERROR_NONE;
}

void ScDev::Notify(uint8_t code, uint8_t value)
{
	m_RxThreadNotificationValue.store(value, std::memory_order_relaxed);
	m_TxThreadNotificationValue.store(value, std::memory_order_relaxed);
	m_RxThreadNotificationCode.store(code, std::memory_order_release);
	m_TxThreadNotificationCode.store(code, std::memory_order_release);

	// rx / tx
	SetEvent(m_RxThreadNotificationEvent);
	SetEvent(m_TxThreadNotificationEvent);

	// rx / tx
	WaitForSingleObject(m_ThreadNotificationAcknowledgeCount, INFINITE);
	WaitForSingleObject(m_ThreadNotificationAcknowledgeCount, INFINITE);

	ResetEvent(m_RxThreadNotificationEvent);
	ResetEvent(m_TxThreadNotificationEvent);
}

int ScDev::AddComDevice(XSuperCANDevice* device)
{
	assert(device);
	assert(m_Initialized);

	Guard g(m_Lock);

	sc_com_dev_index_t index = 0;

	for (index = 0; index < _countof(m_ComDeviceData); ++index) {
		if (!m_ComDeviceDataPrivate[index].com_device) {
			break;
		}
	}

	if (index == _countof(m_ComDeviceData)) {
		return SC_DLL_ERROR_OUT_OF_MEM;
	}

	device->Init(
		shared_from_this(), 
		index,
		&m_ComDeviceData[index]);

	m_ComDeviceDataPrivate[index].com_device = device;

	if (m_OnBus) { // on bus
		Notify(NOTIFICATION_ADD, index);
	}
	else {
		// nothing to do
	}

	return SC_DLL_ERROR_NONE;
}

void ScDev::RemoveComDevice(sc_com_dev_index_t index)
{
	assert(index < _countof(m_ComDeviceData));
	assert(m_Initialized);

	Guard g(m_Lock);

	if (m_ConfigurationAccessIndex == index) {
		m_ConfigurationAccessIndex = MAX_COM_DEVICES_PER_SC_DEVICE;
	}

	m_ComDeviceDataPrivate[index].com_device = nullptr;

	if (m_OnBus) { // on bus
		Notify(NOTIFICATION_REMOVE, index);
	}
	else {
		ComDeviceRemovedRx(index);
		ComDeviceRemovedTx(index);
	}
}

void ScDev::ComDeviceAddedTx(sc_com_dev_index_t index)
{
	auto* priv = &m_ComDeviceDataPrivate[index];

	priv->tx.index = priv->tx.hdr->get_index;
}

void ScDev::ComDeviceRemovedTx(sc_com_dev_index_t index)
{
	auto* priv = &m_ComDeviceDataPrivate[index];

	priv->tx.hdr->get_index = priv->tx.hdr->put_index;
	priv->tx.index = priv->tx.hdr->put_index;

	ResetEvent(priv->tx.ev);
}

void ScDev::ComDeviceRemovedRx(sc_com_dev_index_t index)
{
	auto* priv = &m_ComDeviceDataPrivate[index];

	InterlockedExchange(&priv->rx.hdr->can_lost_rx, 0);
	InterlockedExchange(&priv->rx.hdr->can_lost_tx, 0);
	InterlockedExchange(&priv->rx.hdr->can_lost_status, 0);
	InterlockedExchange(&priv->rx.hdr->can_lost_error, 0);
	InterlockedExchange(&priv->rx.hdr->log_lost, 0);

	priv->rx.hdr->put_index = priv->rx.index;
	priv->rx.hdr->get_index = priv->rx.index;
	
	ResetEvent(priv->rx.ev);
}


DWORD ScDev::RxMain(void* self)
{
	static_cast<ScDev*>(self)->RxMain();
	return 0;
}


void ScDev::RxMain()
{
	auto stream_error = false;
	HANDLE handles[] = {
		m_RxThreadNotificationEvent,
		m_LogEvent,
		nullptr,
	};

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	for (bool done = false; !done; ) {
		if (stream_error) {
			auto rx_notification_had_work = false;

			// Do continue to service requests, especially the ones that require acknowledge.
			ProcessRxNotification(&done, &rx_notification_had_work);


			if (!rx_notification_had_work) {
				Sleep(0); // manual reset events could be set for a while
			}
		}
		else {
			auto error = sc_can_stream_rx_next_wait_handle(m_Stream, &handles[2]);

			if (error) {
				SetDeviceError(error);
				LogFormatDirect(SC_DLL_LOG_LEVEL_ERROR, "sc_can_stream_rx_next_wait_handle failed: %s (%d)\n", sc_strerror(error), error);
				stream_error = true;
			}
			else {
				DWORD r = WaitForMultipleObjects(static_cast<DWORD>(_countof(handles)), handles, FALSE, INFINITE);

				if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + _countof(handles)) {
					auto rx_notification_had_work = false;
					auto log_had_work = false;
					auto rx_had_work = false;

					/* WaitForMultipleObjects returns first signaled handle,
					 * but more _could_ be signaled.
					 *
					 * https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects
					 */
					ProcessRxNotification(&done, &rx_notification_had_work);
					ProcessLog(&log_had_work);
					ProcessRxStream(&stream_error, &rx_had_work);

					if (!rx_notification_had_work &&
						!log_had_work &&
						!rx_had_work) {
						Sleep(0); // manual reset events could be set for a while
					}
				}
				else if (WAIT_TIMEOUT == r) {
					// ?
				}
				else {
					auto e = GetLastError();

					SetDeviceError(sc_map_win_error(m_Device, e));
					LogFormatDirect(SC_DLL_LOG_LEVEL_ERROR, "WaitForMultipleObjects failed: %lu (error=%lu)\n", r, e);
					stream_error = true;
				}
			}
		}
	}
}

void ScDev::ProcessLog(bool* performed_work)
{
	*performed_work = false;

	Guard g(m_LogLock);

	ResetEvent(m_LogEvent);

	if (m_LogLost) {
		for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
			auto com_dev_index = m_RxThreadLiveComDevBuffer[i];
			auto* priv = &m_ComDeviceDataPrivate[com_dev_index];

			InterlockedAdd((volatile LONG*)&priv->rx.hdr->log_lost, (LONG)m_LogLost);
			SetEvent(priv->rx.ev);
		}

		m_LogLost = 0;

		*performed_work = true;
	}

	if (m_LogRingGetIndex != m_LogRingPutIndex) {
		do {
			const unsigned index = m_LogRingGetIndex++ % _countof(m_LogRingBuffer);
			log_entry const* const e = &m_LogRingBuffer[index];

			StoreLogMessage(e);
		} while (m_LogRingGetIndex != m_LogRingPutIndex);

		*performed_work = true;
	}
}

void ScDev::ProcessRxStream(bool* stream_error, bool* performed_work)
{
	*performed_work = false;

	auto error = sc_can_stream_rx(m_Stream, 0);

	if (error) {
		if (SC_DLL_ERROR_TIMEOUT == error) {

		}
		else {
			SetDeviceError(error);
			LogFormatDirect(SC_DLL_LOG_LEVEL_ERROR, "sc_can_stream_rx failed: %s (%d)\n", sc_strerror(error), error);
			*stream_error = true;
			*performed_work = true;
		}
	}
	else {
		*performed_work = true;
	}
}

void ScDev::ProcessRxNotification(bool *done, bool* performed_work)
{
	uint8_t code = 0;
	uint8_t value = 0;

	assert(done);
	assert(performed_work);

	code = m_RxThreadNotificationCode.exchange(NOTIFICATION_NONE);
	value = m_RxThreadNotificationValue.load(std::memory_order_consume);

	switch (code) {
	case NOTIFICATION_SHUTDOWN:
		*done = true;
		break;

	case NOTIFICATION_SET:
		for (sc_com_dev_index_t i = 0; i < MAX_COM_DEVICES_PER_SC_DEVICE; ++i) {
			if (value & (sc_com_dev_index_t(1) << i)) {
				m_RxThreadLiveComDevBuffer[m_RxThreadLiveComDevCount++] = i;
			}
		}

		ReleaseSemaphore(m_ThreadNotificationAcknowledgeCount, 1, NULL);
		break;

	case NOTIFICATION_ADD:
		m_RxThreadLiveComDevBuffer[m_RxThreadLiveComDevCount++] = static_cast<sc_com_dev_index_t>(value);
		ReleaseSemaphore(m_ThreadNotificationAcknowledgeCount, 1, nullptr);
		break;

	case NOTIFICATION_REMOVE: {
		ComDeviceRemovedRx(static_cast<sc_com_dev_index_t>(value));

		for (sc_com_dev_index_t i = 0; i < m_RxThreadLiveComDevCount; ++i) {
			if (value == m_RxThreadLiveComDevBuffer[i]) {
				m_RxThreadLiveComDevBuffer[i] = m_RxThreadLiveComDevBuffer[--m_RxThreadLiveComDevCount];
				break;
			}
		}

		// free any outstanding track ids occupied by the past COM dev
		for (size_t i = 0; i < _countof(m_TxrMap); ++i) {
			auto const com_dev_index = m_TxrMap[i].index.load(std::memory_order_acquire);

			if (value == com_dev_index) {
				m_TxrMap[i].index.store(MAX_COM_DEVICES_PER_SC_DEVICE, std::memory_order_release);
				ReleaseSemaphore(m_TxFifoAvailable, 1, nullptr);
			}
		}

		ReleaseSemaphore(m_ThreadNotificationAcknowledgeCount, 1, nullptr);
	} break;
	}

	*performed_work = NOTIFICATION_NONE != code;
}

DWORD ScDev::TxMain(void* self)
{
	static_cast<ScDev*>(self)->TxMain();
	return 0;
}

void ScDev::TxMain()
{
	const unsigned TX_HANDLE_OFFSET = 1;
	HANDLE handles[TX_HANDLE_OFFSET + MAX_COM_DEVICES_PER_SC_DEVICE];
	uint32_t aligned_sc_msg_can_tx_buffer[24];
	sc_msg_can_tx* tx = reinterpret_cast<sc_msg_can_tx*>(aligned_sc_msg_can_tx_buffer);
	sc_com_dev_index_t live_com_dev_buffer[MAX_COM_DEVICES_PER_SC_DEVICE];
	sc_com_dev_index_t live_com_dev_count = 0;
	auto stream_error = false;

	tx->id = SC_MSG_CAN_TX;

	assert(m_TxThreadNotificationEvent);
	handles[0] = m_TxThreadNotificationEvent;

	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceData); ++i) {
		assert(m_ComDeviceDataPrivate[i].tx.ev);
		handles[TX_HANDLE_OFFSET + i] = m_ComDeviceDataPrivate[i].tx.ev;
	}

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	auto batch_started = false;

	for (;;) {
		auto r = WaitForMultipleObjects(static_cast<DWORD>(_countof(handles)), handles, FALSE, INFINITE);
		auto finish_batch = true;

		if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + _countof(handles)) {
			const auto handle_index = r - WAIT_OBJECT_0;
			const auto handle = handles[handle_index];

			/* WaitForMultipleObjects returns first signaled handle,
			 * but more _could_ be signaled.
			 *
			 * https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects
			 */
			if (m_TxThreadNotificationEvent == handle) {
				const auto code = m_TxThreadNotificationCode.exchange(NOTIFICATION_NONE);
				const auto value = m_TxThreadNotificationValue.load(std::memory_order_consume);

				if (NOTIFICATION_SHUTDOWN == code) {
					break;
				}


				switch (code) {
				case NOTIFICATION_SET: {
					for (sc_com_dev_index_t i = 0; i < MAX_COM_DEVICES_PER_SC_DEVICE; ++i) {
						if (value & (sc_com_dev_index_t(1) << i)) {
							live_com_dev_buffer[live_com_dev_count++] = i;
							ComDeviceAddedTx(i);
						}
					}

					ReleaseSemaphore(m_ThreadNotificationAcknowledgeCount, 1, NULL);
				} break;
				case NOTIFICATION_ADD: {
					live_com_dev_buffer[live_com_dev_count++] = static_cast<sc_com_dev_index_t>(value);
					ComDeviceAddedTx(static_cast<sc_com_dev_index_t>(value));
					ReleaseSemaphore(m_ThreadNotificationAcknowledgeCount, 1, NULL);
				} break;
				case NOTIFICATION_REMOVE: {
					ComDeviceRemovedTx(static_cast<sc_com_dev_index_t>(value));

					for (sc_com_dev_index_t i = 0; i < live_com_dev_count; ++i) {
						if (value == live_com_dev_buffer[i]) {
							live_com_dev_buffer[i] = live_com_dev_buffer[--live_com_dev_count];
							break;
						}
					}
					ReleaseSemaphore(m_ThreadNotificationAcknowledgeCount, 1, NULL);
				} break;
				case NOTIFICATION_NONE:
					// for manual reset event this could be active a bit
					Sleep(0); // yield
					break;
				}
			}
		}
		else if (WAIT_TIMEOUT == r) {
			// ?
		}
		else {
			auto e = GetLastError();

			SetDeviceError(sc_map_win_error(m_Device, e));
			LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "WaitForMultipleObjects failed: %lu (error=%lu)\n", r, e);
			stream_error = true;
		}

		if (stream_error) {
			// Do continue to serice requests that require acknowledge.
			goto service_end;
		}

		if (!batch_started) {
			auto error = sc_can_stream_tx_batch_begin(m_Stream);

			if (error) {
				SetDeviceError(error);
				stream_error = true;
				goto service_end;
			}

			batch_started = true;
		}

		for (bool done = false; !done; ) {
			done = true;

			for (sc_com_dev_index_t i = 0; i < live_com_dev_count; ++i) {
				sc_com_dev_index_t const com_dev_index = live_com_dev_buffer[i];
				auto* data = &m_ComDeviceData[com_dev_index];
				auto* priv = &m_ComDeviceDataPrivate[com_dev_index];
				auto gi = priv->tx.hdr->get_index;
				auto pi = priv->tx.hdr->put_index;
				auto used = pi - gi;

				if (gi != priv->tx.index || used > data->tx.elements) {
					// rogue client
					LogFormatQueue(SC_DLL_LOG_LEVEL_WARNING, "TX ring index=%u gi=%lu pi=%lu used=%lu elements=%lu is inconsistent, resetting ring\n",
						com_dev_index,
						static_cast<unsigned long>(gi),
						static_cast<unsigned long>(pi),
						static_cast<unsigned long>(used),
						static_cast<unsigned long>(data->tx.elements));

					priv->tx.index = pi;
					priv->tx.hdr->get_index = pi;
				}
				else if (used) {
					auto const slot_index = priv->tx.index % data->tx.elements;
					sc_can_mm_slot_t* slot = &priv->tx.hdr->elements[slot_index];

					if (SC_MM_DATA_TYPE_CAN_TX == slot->tx.type) {
						// wait with a timeout to prevent spinning
						r = WaitForSingleObject(m_TxFifoAvailable, 1);
						if (WAIT_OBJECT_0 == r) {
							uint16_t len = sizeof(*tx);
							uint8_t const dlc = slot->tx.dlc & 0xf;
							uint8_t const data_len = dlc_to_len(dlc);

							if (!(slot->tx.flags & SC_CAN_FRAME_FLAG_RTR)) {
								len += data_len;
								memcpy(tx->data, slot->tx.data, data_len);
							}

							if (len & (SC_MSG_CAN_LEN_MULTIPLE - 1)) {
								len += SC_MSG_CAN_LEN_MULTIPLE - (len & (SC_MSG_CAN_LEN_MULTIPLE - 1));
							}

							tx->can_id = m_Device->dev_to_host32(slot->tx.can_id);
							tx->dlc = dlc;
							tx->len = static_cast<uint8_t>(len);
							tx->flags = slot->tx.flags;

							size_t txr_slot = _countof(m_TxrMap);

							// find free txr slot
							for (size_t j = 0; j < _countof(m_TxrMap); ++j) {
								if (MAX_COM_DEVICES_PER_SC_DEVICE == m_TxrMap[j].index.load(std::memory_order_acquire)) {
									txr_slot = j;
									break;
								}
							}

							assert(txr_slot != _countof(m_TxrMap));
							auto* echo = &m_TxEchoMap[txr_slot];

							echo->dlc = slot->tx.dlc;
							echo->can_id = slot->tx.can_id;
							echo->track_id = slot->tx.track_id;
							memcpy(echo->data, tx->data, data_len);

							m_TxrMap[txr_slot].index.store(static_cast<uint32_t>(com_dev_index), std::memory_order_release);

							tx->track_id = static_cast<uint8_t>(txr_slot);

							for (;;) {
								uint8_t const* buffer = reinterpret_cast<uint8_t*>(tx);
								size_t added = 0;
								auto error = sc_can_stream_tx_batch_add(
									m_Stream,
									&buffer,
									&len,
									1,
									&added);

								if (error) {
									LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "sc_can_stream_tx_batch_add failed: %s (%d)\n", sc_strerror(error), error);
									SetDeviceError(error);
									stream_error = true;
									goto service_end;
								}

								if (added) {
									break;
								}

								error = sc_can_stream_tx_batch_end(m_Stream);
								if (error) {
									LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "sc_can_stream_tx_batch_end failed: %s (%d)\n", sc_strerror(error), error);
									SetDeviceError(error);
									stream_error = true;
									goto service_end;
								}

								error = sc_can_stream_tx_batch_begin(m_Stream);
								if (error) {
									LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "sc_can_stream_tx_batch_begin failed: %s (%d)\n", sc_strerror(error), error);
									stream_error = true;
									goto service_end;
								}
							}

							++priv->tx.index;

							priv->tx.hdr->get_index = priv->tx.index;

							if (used > 1) {
								SetEvent(priv->tx.ev);
								finish_batch = false;
							}

							done = false;
						}
						else if (WAIT_TIMEOUT == r) {
							SetEvent(priv->tx.ev);
						}
						else {
							SetDeviceError(sc_map_win_error(m_Device, GetLastError()));
							stream_error = true;
							goto service_end;
						}
					}
					else {
						// drop
						//++data->tx.hdr->txr_lost;
						LogFormatQueue(SC_DLL_LOG_LEVEL_WARNING, "TX ring index=%u unhandled entry type=%d will be ignored\n", slot_index, slot->tx.type);

						++priv->tx.index;

						priv->tx.hdr->get_index = priv->tx.index;

						if (used > 1) {
							SetEvent(priv->tx.ev);
							finish_batch = false;
						}

						done = false;
					}
				}
			}
		}

		if (finish_batch) {
			auto error = sc_can_stream_tx_batch_end(m_Stream);
			if (error) {
				LogFormatQueue(SC_DLL_LOG_LEVEL_ERROR, "sc_can_stream_tx_batch_end failed: %s (%d)\n", sc_strerror(error), error);
				SetDeviceError(error);
				stream_error = true;
				goto service_end;
			}

			batch_started = false;
		}

service_end:
		;
	}
}

void ScDev::SetDeviceError(int error) 
{
	for (sc_com_dev_index_t i = 0; i < _countof(m_ComDeviceDataPrivate); ++i) {
		m_ComDeviceDataPrivate[i].rx.hdr->error = error;
		m_ComDeviceDataPrivate[i].tx.hdr->error = error;
		InterlockedOr((volatile LONG*)&m_ComDeviceDataPrivate[i].rx.hdr->flags, SC_MM_FLAG_ERROR);
		InterlockedOr((volatile LONG*)&m_ComDeviceDataPrivate[i].tx.hdr->flags, SC_MM_FLAG_ERROR);

		SetEvent(m_ComDeviceDataPrivate[i].rx.ev);
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


XSuperCANDevice::~XSuperCANDevice()
{
	if (m_SharedDevice) {
		m_SharedDevice->RemoveComDevice(m_Index);
	}

	if (m_Sc) {
		m_Sc->Release();
	}
}

XSuperCANDevice::XSuperCANDevice()
{
	m_Sc = nullptr;
	m_Index = 0;
	m_Mm = nullptr;
}


STDMETHODIMP XSuperCANDevice::GetRingBufferMappings(SuperCANRingBufferMapping* rx, SuperCANRingBufferMapping* tx)
{
	ATLASSERT(rx);
	ATLASSERT(tx);

	ObjectLock g(this);

	rx->Bytes = m_Mm->rx.elements * sizeof(sc_can_mm_slot_t) + sizeof(sc_can_mm_header);
	rx->Elements = m_Mm->rx.elements;
	rx->MemoryName = SysAllocString(m_Mm->rx.mem_name);
	rx->EventName = SysAllocString(m_Mm->rx.ev_name);

	tx->Bytes = m_Mm->tx.elements * sizeof(sc_can_mm_slot_t) + sizeof(sc_can_mm_header);
	tx->Elements = m_Mm->tx.elements;
	tx->MemoryName = SysAllocString(m_Mm->tx.mem_name);
	tx->EventName = SysAllocString(m_Mm->tx.ev_name);

	return S_OK;
}

void XSuperCANDevice::Init(
	const ScDevPtr& dev,
	sc_com_dev_index_t index,
	com_device_data* mm)
{
	m_SharedDevice = dev;
	m_Index = index;
	m_Mm = mm;
}


STDMETHODIMP XSuperCANDevice::AcquireConfigurationAccess(
	boolean* result,
	unsigned long* timeout_ms)
{
	ObjectLock g(this); 

	ATLASSERT(result);
	ATLASSERT(timeout_ms);

	*result = m_SharedDevice->AcquireConfigurationAccess(m_Index, timeout_ms);

	return S_OK;
}

STDMETHODIMP XSuperCANDevice::ReleaseConfigurationAccess()
{
	ObjectLock g(this);

	m_SharedDevice->ReleaseConfigurationAccess(m_Index);

	return S_OK;
}

STDMETHODIMP XSuperCANDevice::SetBus(boolean on)
{
	ObjectLock g(this);

	auto error = m_SharedDevice->SetBus(m_Index, on != 0);

	if (error) {
		return SC_HRESULT_FROM_ERROR(error);
	}

	return S_OK;
}

STDMETHODIMP XSuperCANDevice::SetFeatureFlags(unsigned long flags)
{
	ObjectLock g(this);

	auto error = m_SharedDevice->SetFeatureFlags(m_Index, static_cast<uint32_t>(flags));

	if (error) {
		return SC_HRESULT_FROM_ERROR(error);
	}

	return S_OK;
}

STDMETHODIMP XSuperCANDevice::GetDeviceData(SuperCANDeviceData* data)
{
	ATLASSERT(data);

	wchar_t wstr[_countof(m_SharedDevice->dev_info.name_bytes)+1];

	auto const& ci = m_SharedDevice->can_info;
	auto const& di = m_SharedDevice->dev_info;

	auto chars = MultiByteToWideChar(CP_UTF8, 0 /*flags*/, reinterpret_cast<LPCCH>(di.name_bytes), di.name_len, wstr, _countof(wstr)-1);

	assert(chars >= 0 && chars < _countof(wstr));
	wstr[chars] = 0;

	data->name = SysAllocString(wstr);

	data->dt_max.brp = ci.dtbt_brp_max;
	data->dt_max.tseg1 = ci.dtbt_tseg1_max;
	data->dt_max.tseg2 = ci.dtbt_tseg2_max;
	data->dt_max.sjw = ci.dtbt_sjw_max;
	data->dt_min.brp = ci.dtbt_brp_min;
	data->dt_min.tseg1 = ci.dtbt_tseg1_min;
	data->dt_min.tseg2 = ci.dtbt_tseg2_min;
	data->dt_min.sjw = 1;

	data->nm_max.brp = ci.nmbt_brp_max;
	data->nm_max.tseg1 = ci.nmbt_tseg1_max;
	data->nm_max.tseg2 = ci.nmbt_tseg2_max;
	data->nm_max.sjw = ci.nmbt_sjw_max;
	data->nm_min.brp = ci.nmbt_brp_min;
	data->nm_min.tseg1 = ci.nmbt_tseg1_min;
	data->nm_min.tseg2 = ci.nmbt_tseg2_min;
	data->nm_min.sjw = 1;

	data->can_clock_hz = ci.can_clk_hz;
	data->feat_perm = di.feat_perm;
	data->feat_conf = di.feat_conf;
	data->fw_ver_major = di.fw_ver_major;
	data->fw_ver_minor = di.fw_ver_minor;
	data->fw_ver_patch = di.fw_ver_patch;

	static_assert(sizeof(data->sn_bytes) == sizeof(di.sn_bytes));
	memcpy(data->sn_bytes, di.sn_bytes, di.sn_len);
	data->sn_length = di.sn_len;


	return S_OK;
}

STDMETHODIMP XSuperCANDevice::SetNominalBitTiming(SuperCANBitTimingParams params)
{
	ObjectLock g(this);

	auto error = m_SharedDevice->SetNominalBitTiming(m_Index, params);

	if (error) {
		return SC_HRESULT_FROM_ERROR(error);
	}

	return S_OK;
}

STDMETHODIMP XSuperCANDevice::SetDataBitTiming(SuperCANBitTimingParams params)
{
	ObjectLock g(this);

	auto error = m_SharedDevice->SetDataBitTiming(m_Index, params);

	if (error) {
		return SC_HRESULT_FROM_ERROR(error);
	}

	return S_OK;
}

void XSuperCANDevice::SetSuperCAN(ISuperCAN2* sc)
{
	assert(sc);
	assert(!m_Sc);

	sc->AddRef();
	m_Sc = sc;
}

STDMETHODIMP XSuperCANDevice::SetLogLevel(int level)
{
	ObjectLock g(this);

	auto error = m_SharedDevice->SetLogLevel(m_Index, level);
	if (error) {
		SC_HRESULT_FROM_ERROR(error);
	}

	return S_OK;
}



///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


CRITICAL_SECTION XSuperCAN::ms_Lock;
ATL::CComObject<XSuperCAN>* XSuperCAN::ms_Instance = XSuperCAN::CreateInstanceLock();

ATL::CComObject<XSuperCAN>* XSuperCAN::CreateInstanceLock()
{
	InitializeCriticalSection(&ms_Lock);

	std::atexit(DestroyInstanceLock);

	return nullptr;
}

void XSuperCAN::DestroyInstanceLock()
{
	DeleteCriticalSection(&ms_Lock);
}

ATL::CComObject<XSuperCAN>* XSuperCAN::GetOrCreate()
{
	Guard g(ms_Lock);

	if (!ms_Instance) {
		auto hr = ATL::CComObject<XSuperCAN>::CreateInstance(&ms_Instance);
		if (SUCCEEDED(hr)) {
			LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "Singleton instance set\n");
		}
		else {
			ms_Instance = nullptr;
		}
	}

	auto ptr = ms_Instance;

	if (ptr) {
		ptr->AddRef();
	}

	return ptr;
}


XSuperCAN::~XSuperCAN()
{
	Guard g(ms_Lock);

	m_Devices.clear();

	sc_log_set_callback(nullptr, nullptr);

	sc_uninit();

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "DLL uninitialized\n");

	assert(this == ms_Instance);

	ms_Instance = nullptr;

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "Singleton instance cleared\n");
}

XSuperCAN::XSuperCAN()
{
	sc_init();

	sc_log_set_callback(this, &XSuperCAN::Log);

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "DLL initialized\n");
}


STDMETHODIMP XSuperCAN::DeviceScan(unsigned long* count)
{
	ATLASSERT(count);

	ObjectLock g(this);

	std::vector<wchar_t> str;
	str.resize(128);
	
	std::vector<ScDevPtr> new_devices;
	new_devices.reserve(m_Devices.size());

	*count = 0;
	
	for (bool done = false; !done; ) {
		done = true;

		auto error = sc_dev_scan();
		if (error) {
			if (SC_DLL_ERROR_DEV_COUNT_CHANGED == error) {
				done = false;
				continue;
			}

			return SC_HRESULT_FROM_ERROR(error);
		}

		uint32_t c = 0;
		error = sc_dev_count(&c);
		if (error) {
			if (SC_DLL_ERROR_DEV_COUNT_CHANGED == error) {
				done = false;
				continue;
			}

			return SC_HRESULT_FROM_ERROR(error);
		}

		*count = c;

		new_devices.clear();

		for (uint32_t i = 0; i < c; ++i) {
			size_t len = str.capacity();
			error = sc_dev_id_unicode(i, str.data(), &len);
			if (error) {
				if (SC_DLL_ERROR_BUFFER_TOO_SMALL == error) {
					str.resize(len + 1);
					--i;
				}
				else if (SC_DLL_ERROR_DEV_COUNT_CHANGED == error) {
					done = false;
					break;
				}
				else {
					return SC_HRESULT_FROM_ERROR(error);
				}
			}
			else {
				bool found = false;
				ScDevPtr ptr;

				for (auto const& dev : m_Devices) {
					auto const& dev_name = dev->name();
					if (dev_name.compare(0, dev_name.size(), str.data(), len) == 0) {
						found = true;
						ptr = dev;
						break;
					}
				}

				if (found) {
					new_devices.emplace_back(std::move(ptr));
				} else {
					ptr = std::make_shared<ScDev>();
					error = ptr->Init(std::wstring(str.data(), str.data() + len));

					if (error) {
						LOG_SRV(SC_DLL_LOG_LEVEL_ERROR, "failed to initialize SuperCAN device: %s (%d)\n", sc_strerror(error), error);
					}
					else {
						new_devices.emplace_back(std::move(ptr));
					}
				}
			}
		}
	}

	m_Devices.swap(new_devices);

	LOG_SRV(SC_DLL_LOG_LEVEL_DEBUG, "DeviceScan: %zu devices found\n", m_Devices.size());

	return S_OK;
}

STDMETHODIMP XSuperCAN::DeviceGetCount(unsigned long* count)
{
	ObjectLock g(this);

	ATLASSERT(count);
	
	*count = static_cast<unsigned long>(m_Devices.size());

	return S_OK;
}


STDMETHODIMP XSuperCAN::DeviceOpen(
	unsigned long index, 
	ISuperCANDevice** dev)
{
	ObjectLock g(this);

	ATLASSERT(dev);

	if (index >= m_Devices.size()) {
		return E_INVALIDARG;
	}

	auto& sc_device = m_Devices[index];

	ATL::CComObject<XSuperCANDevice>* com_device = nullptr;
	try {
		HRESULT hr = ATL::CComObject<XSuperCANDevice>::CreateInstance(&com_device);
		if (FAILED(hr)) {
			return hr;
		}

		com_device->AddRef();

		auto error = sc_device->AddComDevice(com_device);
		if (error) {
			com_device->Release();
			return SC_HRESULT_FROM_ERROR(error);
		}

		com_device->SetSuperCAN(this);
		
		*dev = static_cast<ISuperCANDevice*>(com_device);
	}
	catch (const std::bad_alloc&) {
		if (com_device) {
			com_device->Release();
		}
		
		return E_OUTOFMEMORY;
	}

	return S_OK;
}

STDMETHODIMP XSuperCAN::GetVersion(SuperCANVersion* version)
{
	ATLASSERT(version);

	version->major = SC_SRV_VERSION_MAJOR;
	version->minor = SC_SRV_VERSION_MINOR;
	version->patch = SC_SRV_VERSION_PATCH;
	version->build = 0;
	version->commit = SysAllocString(_T(SC_COMMIT));

	return S_OK;
}

STDMETHODIMP XSuperCAN::SetLogLevel(int level)
{
	sc_log_set_level(level);

	return S_OK;
}

void XSuperCAN::Log(void* ctx, int level, char const* msg, size_t bytes)
{
	static_cast<XSuperCAN*>(ctx)->Log(level, msg, bytes);
}

void XSuperCAN::Log(int level, char const* msg, size_t bytes)
{
	(void)bytes;

	LOG_DLL(level, "%s", msg);
}

} // anon


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


CSuperCAN::~CSuperCAN()
{
	if (m_Instance) {
		m_Instance->Release();
		m_Instance = nullptr;
	}
}

CSuperCAN::CSuperCAN()
	: m_Instance(XSuperCAN::GetOrCreate())
{
}

STDMETHODIMP CSuperCAN::DeviceScan(unsigned long* count)
{
	if (!m_Instance) {
		return E_OUTOFMEMORY;
	}

	return m_Instance->DeviceScan(count);
}

STDMETHODIMP CSuperCAN::DeviceGetCount(unsigned long* count)
{
	if (!m_Instance) {
		return E_OUTOFMEMORY;
	}

	return m_Instance->DeviceGetCount(count);
}

STDMETHODIMP CSuperCAN::DeviceOpen(
	unsigned long index, 
	ISuperCANDevice** dev)
{
	if (!m_Instance) {
		return E_OUTOFMEMORY;
	}

	return m_Instance->DeviceOpen(index, dev);
}

STDMETHODIMP CSuperCAN::GetVersion(SuperCANVersion* version)
{
	if (!m_Instance) {
		return E_OUTOFMEMORY;
	}

	return m_Instance->GetVersion(version);
}

STDMETHODIMP CSuperCAN::SetLogLevel(int level)
{
	if (!m_Instance) {
		return E_OUTOFMEMORY;
	}

	return m_Instance->SetLogLevel(level);
}
