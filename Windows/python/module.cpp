/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Jean Gressmann <jean@0x42.de>
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

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ObjBase.h>

#ifdef max
#   undef max
#endif
#ifdef min
#   undef min
#endif

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <memory>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <vector>
#include <string>


#include "supercan_winapi.h"
#include "supercan_dll.h"
#include "can_bit_timing.h"
#include "supercan_misc.h"
#include "supercan_srv.h"


#if defined(_MSC_VER) || (defined(__clang__) && !defined(__GNUC__))
    #define SC_ALIGN(x) __declspec(align(x))
    #define SC_UUID_INLINE(id) __declspec(uuid(id))
    #define SC_UUID_EXTERN(type, id)
#elif defined(__GNUC__)
    #define SC_ALIGN(x) __attribute__((aligned(x)))
    #define SC_UUID_INLINE(id)
    #define SC_UUID_EXTERN(type, id) \
        template<> inline GUID const& __mingw_uuidof<type>() { \
            static GUID guid; \
            static const HRESULT initialized = CLSIDFromString(L"{" id "}", &guid); \
            (void)initialized; \
            return guid; \
        }    
#endif


#include "supercan_srv.tlh.h"

#define CMD_TIMEOUT_MS 1000

namespace
{
uint64_t s_perf_counter_freq = 1;

inline uint64_t mono_ticks()
{
    uint64_t now;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);
    return now;
}

inline uint64_t mono_millis()
{
    return (mono_ticks() * 1000U) / s_perf_counter_freq;
}

struct PyObjectDeleter {
    inline void operator()(PyObject *obj) const noexcept {
        Py_XDECREF(obj);
    }
};

using PyPtr = std::unique_ptr<PyObject, PyObjectDeleter>;

PyPtr can_bit_timing_type; // class can.BitTiming(f_clock, brp, tseg1, tseg2, sjw, nof_samples=1, strict=False)
PyPtr can_bit_timing_fd_type; // class can.BitTimingFd(f_clock, nom_brp, nom_tseg1, nom_tseg2, nom_sjw, data_brp, data_tseg1, data_tseg2, data_sjw, strict=False)
PyPtr can_message_type;
PyPtr can_can_operation_error;
PyPtr sc_exclusive_type;
PyPtr sc_shared_type;
PyPtr sc_bus_type;
PyPtr can_bus_state_type;
PyPtr can_bus_can_protocol_type;
size_t can_bus_abc_size;
PyPtr can_exceptions_can_initialization_error_type;
PyPtr can_exceptions_can_operation_error_type;
PyPtr copy_deepcopy;
PyPtr can_bus_state_active;
PyPtr can_bus_state_error;
PyPtr can_bus_state_passive;
PyPtr rx_no_msg_result;



std::atomic_int s_exclusive_object_count;
uint64_t system_time_to_epoch_offset_100ns;

#define SetCanInitializationError(...) \
    do { \
        PyPtr msg(PyUnicode_FromFormat(__VA_ARGS__)); \
        PyPtr args(Py_BuildValue("OO", msg.get(), Py_None)); \
        /* create exception object */ \
        PyPtr exc(PyObject_Call(can_exceptions_can_initialization_error_type.get(), args.get(), nullptr)); \
        PyErr_SetObject(exc.get(), msg.get()); \
    } while (0)

#define SetCanOperationError(...) \
    do { \
        PyPtr msg(PyUnicode_FromFormat(__VA_ARGS__)); \
        PyPtr args(Py_BuildValue("OO", msg.get(), Py_None)); \
        /* create exception object */ \
        PyPtr exc(PyObject_Call(can_exceptions_can_operation_error_type.get(), args.get(), nullptr)); \
        PyErr_SetObject(exc.get(), msg.get()); \
    } while (0)

inline uint8_t dlc_to_len(uint8_t dlc)
{
	static const uint8_t map[16] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
	};
	return map[dlc & 0xf];
}

PyObject* sc_create_can_message(bool is_rx, double timestamp, uint32_t can_id, uint32_t flags, uint8_t dlc, uint8_t const* data_)
{
    PyPtr data;
    int rtr = 0;
    int fdf = 0;
    //int is_error_frame = 0;
    int brs = 0;
    int esi = 0;
    int ext = (flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT;
    unsigned data_len = dlc_to_len(dlc);

    if (flags & SC_CAN_FRAME_FLAG_FDF) {
        data.reset(PyByteArray_FromStringAndSize((char const*)data_, data_len));
        fdf = 1;
        esi = (flags & SC_CAN_FRAME_FLAG_BRS) == SC_CAN_FRAME_FLAG_BRS;
        brs = (flags & SC_CAN_FRAME_FLAG_ESI) == SC_CAN_FRAME_FLAG_ESI;
    } else {
        if (flags & SC_CAN_FRAME_FLAG_RTR) {
            rtr = 1;
        } else {
            data.reset(PyByteArray_FromStringAndSize((char const*)data_, data_len));
        }
    }

    PyPtr kwargs(
        Py_BuildValue(
            "{s:d,s:k,s:i,s:i,s:i,s:O,s:i,s:O,s:i,s:i,s:i,s:i}",
            "timestamp",
            timestamp,
            "arbitration_id",
            can_id,
            "is_extended_id",
            ext,
            "is_remote_frame",
            rtr,
            "is_error_frame",
            0,
            "channel",
            Py_None,
            "dlc",
            (int)dlc,
            "data",
            data.get(),
            "is_fd",
            fdf,
            "is_rx",
            (int)is_rx,
            "bitrate_switch",
            brs,
            "error_state_indicator",
            esi
        )
    );

    data.release();

    // create can.Message
    PyPtr args(PyTuple_New(0)); // immortal, Python has already optimized this
    PyPtr msg(PyObject_Call(can_message_type.get(), args.get(), kwargs.get()));
    
    // create tuple [msg, Filtered=False]
    PyObject* ret = PyTuple_New(2);

    PyTuple_SET_ITEM(ret, 0, msg.release()); // steals referenc
    PyTuple_SET_ITEM(ret, 1, Py_NewRef(Py_False));
    
    return ret;
}


struct sc_config
{
    can_bit_timing_constraints_real nominal_user_constraints, data_user_constraints;
    std::string serial;
    PyObject* filters;
    int channel_index;
    bool fdf;
    bool receive_own_messages;
};


bool get_bool_arg(PyObject* arg, char const* name, bool* in_out_arg)
{
    if (!arg || Py_None == arg) {

    }
    else if (Py_True == arg) {
        *in_out_arg = true;
    }
    else if (Py_False == arg) {
        *in_out_arg = false;
    }
    else if (PyLong_Check(arg)) {
        *in_out_arg = PyLong_AsLong(arg) != 0;
    }
    else {
        PyErr_Format(PyExc_ValueError, "%s must be a boolean value [True, False]", name);
        return false;
    }

    return true;
}

bool get_int_arg(PyObject* arg, char const* name, int* in_out_arg)
{
    if (!arg || Py_None == arg) {

    }
    else if (PyLong_Check(arg)) {
        *in_out_arg = (int)PyLong_AsLong(arg);
    }
    else {
        PyErr_Format(PyExc_ValueError, "%s must be an integer value", name);
        return false;
    }

    return true;
}

bool get_sample_point_arg(PyObject* arg, char const* name, float* in_out_arg)
{
    if (!arg || Py_None == arg) {

    }
    else if (PyFloat_Check(arg)) {
        float sample_point = (float)PyFloat_AsDouble(arg);

        if (sample_point != sample_point || sample_point <= 0 || sample_point >= 1) {
            goto error;
        }

        *in_out_arg = sample_point;
    }
    else {
error:
        PyErr_Format(PyExc_ValueError, "%s must be in range (0, 1)", name);
        return false;
    }

    return true;
}

bool sc_parse_args(PyObject* args, PyObject* kwargs, sc_config& config)
{
    PyObject* py_channel = nullptr;
    PyObject* py_serial = nullptr;
    PyObject* py_fd = nullptr;
    PyObject* py_nbitrate = nullptr;
    PyObject* py_dbitrate = nullptr;
    PyObject* py_nsample_point = nullptr;
    PyObject* py_dsample_point = nullptr;
    PyObject* py_nsjw = nullptr;
    PyObject* py_dsjw = nullptr;
    PyObject* py_receive_own_messages = nullptr;

    ZeroMemory(&config.nominal_user_constraints, sizeof(config.nominal_user_constraints));
    ZeroMemory(&config.data_user_constraints, sizeof(config.data_user_constraints));

    config.nominal_user_constraints.bitrate = 500000;
    config.nominal_user_constraints.sample_point = 0.8f;
    config.nominal_user_constraints.sjw = CAN_SJW_TSEG2;
    config.data_user_constraints.bitrate = 500000;
    config.data_user_constraints.sample_point = 0.7f;
    config.data_user_constraints.sjw = CAN_SJW_TSEG2;
    config.channel_index = -1;
    config.receive_own_messages = false;
    config.fdf = false;
    config.filters = nullptr;
    
    const char* serial_ptr = nullptr;
    Py_ssize_t serial_len = 0;
    char python_converts_strings_to_numbers[16];

    char const * const kwlist[] = {
        "channel",
        "filters",
        "serial",
        "bitrate",
        "data_bitrate",
        "fd",
        "sample_point",
        "data_sample_point",
        "sjw_abr",
        "sjw_dbr",
        "receive_own_messages",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "OO|OOOOOOOOOO",
        (char**)kwlist,
        &py_channel,
        &config.filters,
        &py_serial,
        &py_nbitrate,
        &py_dbitrate,
        &py_fd,
        &py_nsample_point,
        &py_dsample_point,
        &py_nsjw,
        &py_dsjw,
        &py_receive_own_messages
    )) {
        return false;
    }

    if (PyLong_Check(py_channel)) {
        config.channel_index = (int)PyLong_AsLong(py_channel);
    } else if (PyUnicode_Check(py_channel)) {
        Py_ssize_t wlen = 0;
        wchar_t* wstr = PyUnicode_AsWideCharString(py_channel, &wlen);

        if (wstr) {
            config.channel_index = (int)wcstol(wstr, nullptr, 10);
            PyMem_Free(wstr);
        } else {
            return false;
        }
    } else if (Py_None == py_channel) {

    }
    else {
        PyErr_SetString(PyExc_ValueError, "channel must be an integer argument or None");
        return false;
    }

    if (!py_serial || Py_None == py_serial) {

    }
    else if (PyLong_Check(py_serial)) {
        // if we pass serial="123456" we end up here, sigh
        serial_len = snprintf(python_converts_strings_to_numbers, sizeof(python_converts_strings_to_numbers), "%llu", PyLong_AsUnsignedLongLong(py_serial));
        serial_ptr = python_converts_strings_to_numbers;
    }
    else if (PyUnicode_Check(py_serial)) {
        serial_ptr = PyUnicode_AsUTF8AndSize(py_serial, &serial_len);
    }
    else {
        PyErr_SetString(PyExc_ValueError, "serial must be a hex string like '92fe20c6'");
        return false;
    }

    config.serial.assign(serial_ptr, serial_ptr + serial_len);

    if (!py_nbitrate || Py_None == py_nbitrate) {

    }
    else if (PyLong_Check(py_nbitrate)) {
        config.nominal_user_constraints.bitrate = PyLong_AsUnsignedLong(py_nbitrate);
    }
    else {
        PyErr_SetString(PyExc_ValueError, "bitrate must be a positive integer <= 1000000");
        return false;
    }

    if (!py_dbitrate || Py_None == py_dbitrate) {

    }
    else if (PyLong_Check(py_dbitrate)) {
        config.data_user_constraints.bitrate = PyLong_AsUnsignedLong(py_dbitrate);
    }
    else {
        PyErr_SetString(PyExc_ValueError, "data_bitrate must be a positive integer");
        return false;
    }
    
    if (!get_int_arg(py_nsjw, "sjw_abr", &config.nominal_user_constraints.sjw)) {
        return false;
    }

    if (!get_int_arg(py_dsjw, "sjw_dbr", &config.data_user_constraints.sjw)) {
        return false;
    }

    if (!get_bool_arg(py_fd, "fd", &config.fdf)) {
        return false;
    }

    if (!get_bool_arg(py_receive_own_messages, "receive_own_messages", &config.receive_own_messages)) {
        return false;
    }

    if (!get_sample_point_arg(py_nsample_point, "sample_point", &config.nominal_user_constraints.sample_point)) {
        return false;
    }

    if (config.fdf && !get_sample_point_arg(py_dsample_point, "data_sample_point", &config.data_user_constraints.sample_point)) {
        return false;
    }

    if (config.nominal_user_constraints.bitrate > 1000000) {
        PyErr_SetString(PyExc_ValueError, "bitrate must be in range (0-1000000]");
        return false;
    }

    if (config.fdf && config.data_user_constraints.bitrate < config.nominal_user_constraints.bitrate) {
        PyErr_SetString(PyExc_ValueError, "data_bitrate must not be less than bitrate");
        return false;
    }

    return true;
}


bool sc_to_timeout(PyObject* timeout, DWORD* out_timeout_ms)
{
    if (Py_None == timeout) {
        *out_timeout_ms = INFINITE;
    } else if (PyLong_Check(timeout)) {
        long timeout_long = PyLong_AsLong(timeout) * 1000;

        if (timeout_long < 0 || ((DWORD)timeout_long) >= std::numeric_limits<DWORD>::max()) {
            *out_timeout_ms = INFINITE;
        } else {
            *out_timeout_ms = (DWORD)timeout_long;
        }
    } else if (PyFloat_Check(timeout)) {
        double timeout_double = PyFloat_AsDouble(timeout) * 1000;

        if (timeout_double < 0 || timeout_double >= std::numeric_limits<DWORD>::max()) {
            *out_timeout_ms = INFINITE;
        } else {
            *out_timeout_ms = (DWORD)timeout_double;
        }
    } else {
        PyErr_Format(PyExc_ValueError, "timeout must be float or None");
        return false;
    }

    return true;
}

class sc_base 
{
public:
    virtual ~sc_base() = default;    
    virtual bool init(PyObject* kwargs, sc_config& config) = 0;
    virtual void stop() = 0;
    virtual PyObject* send(PyObject *msg, DWORD timeout_winapi) = 0;
    virtual PyObject* recv(DWORD timeout_winapi) = 0;
    virtual PyObject* get_state() const = 0;
    virtual PyObject* get_channel_info() const = 0;
protected:
    sc_base() = default;    
};


struct sc_exclusive : public sc_base
{
    sc_cmd_ctx_t cmd_ctx;
    sc_dev_t* dev;
    sc_can_stream_t* stream;
    struct sc_dev_time_tracker tt;
    uint8_t available_track_id_buffer[256];
    size_t available_track_id_count;
    PyObject* echos[256];
    PyPtr channel_info;
    HANDLE rx_event;
    unsigned event_counter;
    bool receive_own_messages;
    bool fdf;
    bool fw_ge_060;
    uint8_t bus_status;
    uint8_t rx_errors;
    uint8_t tx_errors;
    uint64_t rx_lost;
    uint64_t tx_dropped;
    uint64_t initial_device_time_us;
    uint64_t initial_system_time_100ns;
    std::deque<PyObject*> rx_queue;

    ~sc_exclusive()
    {
        stop_();

        if (dev) {
            sc_dev_close(dev);
            dev = nullptr;
        }

        // clean up library
        if (0 == --s_exclusive_object_count) {
            sc_uninit();
        }

        CloseHandle(rx_event);
    }

    sc_exclusive()
    {
        memset(&cmd_ctx, 0, sizeof(cmd_ctx));
        memset(&tt, 0, sizeof(tt));
        memset(&echos, 0, sizeof(echos));

        dev = nullptr;
        stream = nullptr;
        event_counter = 0;
        receive_own_messages = false;
        fdf = false;
        fw_ge_060 = false;
        initial_device_time_us = 0;
        initial_device_time_us = 0;
        bus_status = SC_CAN_STATUS_ERROR_ACTIVE;
        rx_errors = 0;
        tx_errors = 0;
        rx_lost = 0;
        tx_dropped = 0;

        for (size_t i = 0; i < _countof(available_track_id_buffer); ++i) {
            available_track_id_buffer[i] = (uint8_t)i;
        }

        available_track_id_count = _countof(available_track_id_buffer);

        sc_tt_init(&tt);

        if (0 == s_exclusive_object_count++) {
            sc_init();
        }

        rx_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    }

    bool init(PyObject* kwargs, sc_config& config)
    {
        // ok
        int error;
        uint32_t count;
        uint32_t best_index;
        // sc_version_t version;
        struct can_bit_timing_settings nominal_settings, data_settings;
        struct can_bit_timing_hw_contraints nominal_hw_constraints, data_hw_constraints;
        struct sc_msg_dev_info dev_info;
        struct sc_msg_can_info can_info;
        char serial_str[1 + sizeof(dev_info.sn_bytes) * 2];
        char name_str[1 + sizeof(dev_info.name_bytes)];

        error = SC_DLL_ERROR_NONE;
        count = 0;
        best_index = 0;
        serial_str[0] = 0;
        name_str[0] = 0;

        // memset(&version, 0, sizeof(version));

        error = sc_dev_scan();
        if (error) {
            SetCanInitializationError("sc_dev_scan failed: %s (%d)\n", sc_strerror(error), error);
            goto cleanup;
        }

        error = sc_dev_count(&count);
        if (error) {
            SetCanInitializationError("sc_dev_count failed: %s (%d)\n", sc_strerror(error), error);
            goto cleanup;
        }

        if (!count) {
            PyErr_SetString(PyExc_ValueError, "no SuperCAN devices found");

            goto cleanup;
        }

        best_index = count;

        for (uint32_t i = 0; i < count; ++i) {
            bool close_device = false;
            bool close_cmd_ctx = false;

            error = sc_dev_open_by_index(i, &dev);
            if (SC_DLL_ERROR_NONE == error) {
                close_device = true;
                error = sc_cmd_ctx_init(&cmd_ctx, dev);
                if (SC_DLL_ERROR_NONE == error) {
                    close_cmd_ctx = true;
                    if (!config.serial.empty()) {
                        if (sc_exclusive_get_device_info(&dev_info)) {
                            //fprintf(stdout, "device features perm=%#04x conf=%#04x\n", dev_info.feat_perm, dev_info.feat_conf);

                            for (size_t i = 0; i < std::min((size_t)dev_info.sn_len, _countof(serial_str) - 1); ++i) {
                                snprintf(&serial_str[i * 2], 3, "%02x", dev_info.sn_bytes[i]);
                            }

                            dev_info.name_len = std::min<uint8_t>(dev_info.name_len, sizeof(name_str) - 1);
                            memcpy(name_str, dev_info.name_bytes, dev_info.name_len);
                            name_str[dev_info.name_len] = 0;

                            //fprintf(stdout, "device identifies as %s, serial no %s, firmware version %u.%u.%u\n",
                            //   name_str, serial_str, dev_info.fw_ver_major, dev_info.fw_ver_minor, dev_info.fw_ver_patch);

                            if (_stricmp(serial_str, config.serial.c_str()) == 0) {
                                if (config.channel_index < 0 || (unsigned)config.channel_index == dev_info.ch_index) {
                                    best_index = i;
                                    i = count;
                                }
                            }
                        }
                    } else {
                        best_index = i;
                        i = count;
                    }
                }
            }

            if (close_cmd_ctx) {
                sc_cmd_ctx_uninit(&cmd_ctx);
                cmd_ctx.dev = nullptr;
            }

            if (close_device) {
                sc_dev_close(dev);
                dev = nullptr;
            }
        }

        if (count == best_index) {
            PyPtr msg(PyUnicode_FromFormat("failed to find device matching serial '%s', channel index %d", config.serial.c_str(), config.channel_index));
            PyErr_SetObject(PyExc_ValueError, msg.get());

            goto cleanup;
        }

        error = sc_dev_open_by_index(best_index, &dev);
        if (error) {
            SetCanInitializationError("failed to open device: %s (%d)\n", sc_strerror(error), error);
            goto cleanup;
        }

        error = sc_cmd_ctx_init(&cmd_ctx, dev);
        if (error) {
            SetCanInitializationError("failed to create device command context: %s (%d)\n", sc_strerror(error), error);
            goto cleanup;
        }
        // fetch device info (for CAN-FD support)
        if (config.fdf)
        {
            if (!sc_exclusive_get_device_info(&dev_info)) {
                SetCanInitializationError("failed to get device info: %s (%d)\n", sc_strerror(error), error);
                goto cleanup;
            }

            uint16_t feat_flags = dev_info.feat_perm | dev_info.feat_conf;

            if (!(feat_flags & SC_FEATURE_FLAG_FDF)) {
                SetCanInitializationError("device doesn't support CAN-FD mode");
                goto cleanup;
            }

            for (size_t i = 0; i < std::min((size_t)dev_info.sn_len, _countof(serial_str) - 1); ++i) {
                snprintf(&serial_str[i * 2], 3, "%02x", dev_info.sn_bytes[i]);
            }

            dev_info.name_len = std::min<uint8_t>(dev_info.name_len, sizeof(name_str) - 1);
            memcpy(name_str, dev_info.name_bytes, dev_info.name_len);
            name_str[dev_info.name_len] = 0;
        }

        // fetch can info
        {
            struct sc_msg_req* req = (struct sc_msg_req*)cmd_ctx.tx_buffer;
            memset(req, 0, sizeof(*req));
            req->id = SC_MSG_CAN_INFO;
            req->len = sizeof(*req);
            uint16_t rep_len;
            error = sc_cmd_ctx_run(&cmd_ctx, req->len, &rep_len, CMD_TIMEOUT_MS);
            if (error) {
                SetCanInitializationError("failed to get CAN info: %s (%d)\n", sc_strerror(error), error);
                goto cleanup;
            }

            if (rep_len < sizeof(can_info)) {
                SetCanInitializationError("failed to get CAN info (short reponse)\n");
                goto cleanup;
            }

            memcpy(&can_info, cmd_ctx.rx_buffer, sizeof(can_info));

            can_info.can_clk_hz = dev->dev_to_host32(can_info.can_clk_hz);
            can_info.msg_buffer_size = dev->dev_to_host16(can_info.msg_buffer_size);
            can_info.nmbt_brp_max = dev->dev_to_host16(can_info.nmbt_brp_max);
            can_info.nmbt_tseg1_max =dev->dev_to_host16(can_info.nmbt_tseg1_max);

            // limit track ids to device tx fifo range
            available_track_id_count = std::min(available_track_id_count, (size_t)can_info.tx_fifo_size);
        }

        // compute hw settings
        {
            nominal_hw_constraints.brp_min = can_info.nmbt_brp_min;
            nominal_hw_constraints.brp_max = can_info.nmbt_brp_max;
            nominal_hw_constraints.brp_step = 1;
            nominal_hw_constraints.clock_hz = can_info.can_clk_hz;
            nominal_hw_constraints.sjw_max = can_info.nmbt_sjw_max;
            nominal_hw_constraints.tseg1_min = can_info.nmbt_tseg1_min;
            nominal_hw_constraints.tseg1_max = can_info.nmbt_tseg1_max;
            nominal_hw_constraints.tseg2_min = can_info.nmbt_tseg2_min;
            nominal_hw_constraints.tseg2_max = can_info.nmbt_tseg2_max;

            data_hw_constraints.brp_min = can_info.dtbt_brp_min;
            data_hw_constraints.brp_max = can_info.dtbt_brp_max;
            data_hw_constraints.brp_step = 1;
            data_hw_constraints.clock_hz = can_info.can_clk_hz;
            data_hw_constraints.sjw_max = can_info.dtbt_sjw_max;
            data_hw_constraints.tseg1_min = can_info.dtbt_tseg1_min;
            data_hw_constraints.tseg1_max = can_info.dtbt_tseg1_max;
            data_hw_constraints.tseg2_min = can_info.dtbt_tseg2_min;
            data_hw_constraints.tseg2_max = can_info.dtbt_tseg2_max;

            error = cia_fd_cbt_real(
                &nominal_hw_constraints,
                &data_hw_constraints,
                &config.nominal_user_constraints,
                &config.data_user_constraints,
                &nominal_settings,
                &data_settings);
            switch (error) {
            case CAN_BTRE_NO_SOLUTION:
                SetCanInitializationError("The chosen nominal/data bitrate/sjw cannot be configured on the device.\n");
                goto cleanup;
            case CAN_BTRE_NONE:
                break;
            default:
                SetCanInitializationError("Ooops. Failed to compute bitrate timing");
                goto cleanup;
            }
        }


        // setup device
        {
            unsigned cmd_count = 0;
            PUCHAR cmd_tx_ptr = cmd_ctx.tx_buffer;

            // clear features
            struct sc_msg_features* feat = (struct sc_msg_features*)cmd_tx_ptr;
            memset(feat, 0, sizeof(*feat));
            feat->id = SC_MSG_FEATURES;
            feat->len = sizeof(*feat);
            feat->op = SC_FEAT_OP_CLEAR;
            cmd_tx_ptr += feat->len;
            ++cmd_count;

            feat = (struct sc_msg_features*)cmd_tx_ptr;
            memset(feat, 0, sizeof(*feat));
            feat->id = SC_MSG_FEATURES;
            feat->len = sizeof(*feat);
            feat->op = SC_FEAT_OP_OR;
            // try to enable CAN-FD and TXR
            feat->arg = (dev_info.feat_perm | dev_info.feat_conf) &
                ((config.fdf ? SC_FEATURE_FLAG_FDF : 0) | SC_FEATURE_FLAG_TXR);
            cmd_tx_ptr += feat->len;
            ++cmd_count;


            // set nominal bittiming
            struct sc_msg_bittiming* bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
            memset(bt, 0, sizeof(*bt));
            bt->id = SC_MSG_NM_BITTIMING;
            bt->len = sizeof(*bt);
            bt->brp = dev->dev_to_host16((uint16_t)nominal_settings.brp);
            bt->sjw = (uint8_t)nominal_settings.sjw;
            bt->tseg1 = dev->dev_to_host16((uint16_t)nominal_settings.tseg1);
            bt->tseg2 = (uint8_t)nominal_settings.tseg2;
            cmd_tx_ptr += bt->len;
            ++cmd_count;

            if (config.fdf) {
                // CAN-FD capable & configured -> set data bitrate

                bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
                memset(bt, 0, sizeof(*bt));
                bt->id = SC_MSG_DT_BITTIMING;
                bt->len = sizeof(*bt);
                bt->brp = dev->dev_to_host16((uint16_t)data_settings.brp);
                bt->sjw = (uint8_t)data_settings.sjw;
                bt->tseg1 = dev->dev_to_host16((uint16_t)data_settings.tseg1);
                bt->tseg2 = (uint8_t)data_settings.tseg2;
                cmd_tx_ptr += bt->len;
                ++cmd_count;
            }

            struct sc_msg_config* bus_on = (struct sc_msg_config*)cmd_tx_ptr;
            memset(bus_on, 0, sizeof(*bus_on));
            bus_on->id = SC_MSG_BUS;
            bus_on->len = sizeof(*bus_on);
            bus_on->arg = dev->dev_to_host16(1);
            cmd_tx_ptr += bus_on->len;
            ++cmd_count;

            uint16_t rep_len;
            error = sc_cmd_ctx_run(&cmd_ctx, (uint16_t)(cmd_tx_ptr - cmd_ctx.tx_buffer), &rep_len, CMD_TIMEOUT_MS);
            if (error) {
                SetCanInitializationError("failed to configure device: %s (%d)\n", sc_strerror(error), error);
                goto cleanup;
            }


            if (rep_len < cmd_count * sizeof(struct sc_msg_error)) {
                SetCanInitializationError("failed to setup device (short reponse)");
                goto cleanup;
            }

            struct sc_msg_error* error_msgs = (struct sc_msg_error*)cmd_ctx.rx_buffer;
            for (unsigned i = 0; i < cmd_count; ++i) {
                struct sc_msg_error* error_msg = &error_msgs[i];
                if (SC_ERROR_NONE != error_msg->error) {
                    SetCanInitializationError("setup cmd index %u failed: %d", i, error_msg->error);
                    goto cleanup;
                }
            }
        }

        error = sc_can_stream_init(
            dev,
            can_info.msg_buffer_size,
            this,
            &sc_exclusive::process_can,
            -1, /* no read requests */
            &stream);
        if (error) {
            SetCanInitializationError("failed to initialize CAN stream: %s (%d)\n", sc_strerror(error), error);
            goto cleanup;
        }

        stream->user_handle = rx_event;
        receive_own_messages = config.receive_own_messages;
        fdf = config.fdf;
        fw_ge_060 = (
            dev_info.fw_ver_major > 0 ||
            dev_info.fw_ver_minor >= 6);

        channel_info.reset(PyUnicode_FromFormat("%s (%s) CH%u", name_str, serial_str, dev_info.ch_index));

        return true;

    cleanup:
        return false;
    }

    
    bool sc_exclusive_get_device_info(struct sc_msg_dev_info* dev_info)
    {
        int error = 0;
        struct sc_msg_req* req = (struct sc_msg_req*)cmd_ctx.tx_buffer;
        memset(req, 0, sizeof(*req));
        req->id = SC_MSG_DEVICE_INFO;
        req->len = sizeof(*req);
        uint16_t rep_len;
        error = sc_cmd_ctx_run(&cmd_ctx, req->len, &rep_len, CMD_TIMEOUT_MS);
        if (error) {
            fprintf(stderr, "failed to get device info: %s (%d)\n", sc_strerror(error), error);
            return false;
        }

        if (rep_len < sizeof(dev_info)) {
            fprintf(stderr, "failed to get device info (short reponse)\n");
            return false;
        }

        memcpy(dev_info, cmd_ctx.rx_buffer, sizeof(*dev_info));

        dev_info->feat_perm = dev->dev_to_host16(dev_info->feat_perm);
        dev_info->feat_conf = dev->dev_to_host16(dev_info->feat_conf);

        return true;
    }


    double track_device_time(uint32_t timestamp_us) {
        uint64_t now_100ns;
        uint64_t device_time_us;

        sc_tt_track(&tt, timestamp_us);

        device_time_us = tt.ts_us_hi;
        device_time_us <<= 32;
        device_time_us |= tt.ts_us_lo;

        if (initial_device_time_us == 0) {
            FILETIME now;

            GetSystemTimeAsFileTime(&now);

            initial_system_time_100ns = now.dwHighDateTime;
            initial_system_time_100ns <<= 32;
            initial_system_time_100ns |= now.dwLowDateTime;

            initial_device_time_us = device_time_us;
        }

        now_100ns = initial_system_time_100ns;
        now_100ns += (device_time_us - initial_device_time_us) * UINT64_C(10);
        now_100ns -= system_time_to_epoch_offset_100ns;

        return now_100ns * 1e-7;
    }

    void stop_() 
    {
        if (stream) {
            sc_can_stream_uninit(stream);
            stream = nullptr;
        }

        if (cmd_ctx.dev) {
            struct sc_msg_config* bus_on = (struct sc_msg_config*)cmd_ctx.tx_buffer;
            memset(bus_on, 0, sizeof(*bus_on));
            bus_on->id = SC_MSG_BUS;
            bus_on->len = sizeof(*bus_on);

            uint16_t rep_len;
            int error = sc_cmd_ctx_run(&cmd_ctx, bus_on->len, &rep_len, CMD_TIMEOUT_MS);
            if (error) {
                //SetCanInitializationError("failed to configure device: %s (%d)\n", sc_strerror(error), error);
                //goto cleanup_device_ctx;
            }

            sc_cmd_ctx_uninit(&cmd_ctx);
            cmd_ctx.dev = nullptr;
        }

        for (auto item : rx_queue) {
            Py_DECREF(item);
        }

        rx_queue.clear();

        // release echos
        for (size_t i = 0; i < _countof(echos); ++i) {
            Py_XDECREF(echos[i]);
        }
    }
    void stop() {
        stop_();
    }

    static int process_can(void* ctx, void const* ptr, uint16_t size)
    {
        uint16_t left = 0;
        PyObject* self = (PyObject*)ctx;
        sc_exclusive* sc = (sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size);

        if (!sc->process_buffer(self, (uint8_t*)ptr, size, &left)) {
            return -1;
        }

        if (left) {
            return -1;
        }

        return 0;
    }

    bool process_buffer(PyObject* self, uint8_t* ptr, uint16_t size, uint16_t* left)
    {
        // process buffer
        PUCHAR in_beg = ptr;
        PUCHAR in_end = in_beg + size;
        PUCHAR in_ptr = in_beg;

        *left = 0;

        ++event_counter;

        while (in_ptr + SC_MSG_HEADER_LEN <= in_end) {
            struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;
            if (in_ptr + msg->len > in_end) {
                *left = (uint16_t)(in_end - in_ptr);
                break;
            }

            assert(msg->len);

            if (msg->len < SC_MSG_HEADER_LEN) {
                fprintf(stderr, "malformed msg, len < SC_MSG_HEADER_LEN\n");
                return false;
            }

            in_ptr += msg->len;

            switch (msg->id) {
            case SC_MSG_EOF: {
                in_ptr = in_end;
            } break;
            case SC_MSG_CAN_STATUS: {
                struct sc_msg_can_status const* status = (struct sc_msg_can_status const*)msg;
                if (msg->len < sizeof(*status)) {
                    fprintf(stderr, "malformed sc_msg_status\n");
                    return false;
                }

                uint32_t timestamp_us = dev->dev_to_host32(status->timestamp_us);

                track_device_time(timestamp_us);

                bus_status = status->bus_status;
                rx_errors = status->rx_errors;
                tx_errors = status->tx_errors;
                rx_lost += dev->dev_to_host16(status->rx_lost);
                tx_dropped += dev->dev_to_host16(status->tx_dropped);

                //PyObject_SetAttrString(self, "rx_errors", can_bus_state_passive.get());

                // if (!ac->candump && (ac->log_flags & LOG_FLAG_CAN_STATE)) {
                //     bool log = false;
                //     if (ac->log_on_change) {
                //         log = ac->can_rx_errors_last != status->rx_errors ||
                //             ac->can_tx_errors_last != status->tx_errors ||
                //             ac->can_bus_state_last != status->bus_status;
                //     }
                //     else {
                //         log = true;
                //     }

                //     ac->can_rx_errors_last = status->rx_errors;
                //     ac->can_tx_errors_last = status->tx_errors;
                //     ac->can_bus_state_last = status->bus_status;

                //     if (log) {
                //         fprintf(stdout, "CAN rx errors=%u tx errors=%u bus=", status->rx_errors, status->tx_errors);
                //         switch (status->bus_status) {
                //         case SC_CAN_STATUS_ERROR_ACTIVE:
                //             fprintf(stdout, "error_active");
                //             break;
                //         case SC_CAN_STATUS_ERROR_WARNING:
                //             fprintf(stdout, "error_warning");
                //             break;
                //         case SC_CAN_STATUS_ERROR_PASSIVE:
                //             fprintf(stdout, "error_passive");
                //             break;
                //         case SC_CAN_STATUS_BUS_OFF:
                //             fprintf(stdout, "off");
                //             break;
                //         default:
                //             fprintf(stdout, "unknown");
                //             break;
                //         }
                //         fprintf(stdout, "\n");
                //     }
                // }

                // if (!ac->candump && (ac->log_flags & LOG_FLAG_USB_STATE)) {
                //     bool log = false;
                //     bool irq_queue_full = status->flags & SC_CAN_STATUS_FLAG_IRQ_QUEUE_FULL;
                //     bool desync = status->flags & SC_CAN_STATUS_FLAG_TXR_DESYNC;
                //     if (ac->log_on_change) {
                //         log = ac->usb_rx_lost != rx_lost ||
                //             ac->usb_tx_dropped != tx_dropped ||
                //             irq_queue_full || desync;
                //     }
                //     else {
                //         log = true;
                //     }

                //     ac->usb_rx_lost = rx_lost;
                //     ac->usb_tx_dropped = tx_dropped;

                //     if (log) {
                //         fprintf(stdout, "CAN->USB rx lost=%u USB->CAN tx dropped=%u irqf=%u desync=%u\n", rx_lost, tx_dropped, irq_queue_full, desync);
                //     }
                // }
            } break;
            case SC_MSG_CAN_ERROR: {
                struct sc_msg_can_error const* error_msg = (struct sc_msg_can_error const*)msg;
                if (msg->len < sizeof(*error_msg)) {
                    fprintf(stderr, "malformed sc_msg_can_error\n");
                    return false;
                }

                uint32_t timestamp_us = dev->dev_to_host32(error_msg->timestamp_us);

                track_device_time(timestamp_us);

                // if (SC_CAN_ERROR_NONE != error_msg->error) {
                //     fprintf(
                //         stdout, "%s %s ",
                //         (error_msg->flags & SC_CAN_ERROR_FLAG_RXTX_TX) ? "tx" : "rx",
                //         (error_msg->flags & SC_CAN_ERROR_FLAG_NMDT_DT) ? "data" : "arbitration");
                //     switch (error_msg->error) {
                //     case SC_CAN_ERROR_STUFF:
                //         fprintf(stdout, "stuff ");
                //         break;
                //     case SC_CAN_ERROR_FORM:
                //         fprintf(stdout, "form ");
                //         break;
                //     case SC_CAN_ERROR_ACK:
                //         fprintf(stdout, "ack ");
                //         break;
                //     case SC_CAN_ERROR_BIT1:
                //         fprintf(stdout, "bit1 ");
                //         break;
                //     case SC_CAN_ERROR_BIT0:
                //         fprintf(stdout, "bit0 ");
                //         break;
                //     case SC_CAN_ERROR_CRC:
                //         fprintf(stdout, "crc ");
                //         break;
                //     default:
                //         fprintf(stdout, "<unknown> ");
                //         break;
                //     }
                //     fprintf(stdout, "error\n");
                // }
            } break;
            case SC_MSG_CAN_RX: {
                struct sc_msg_can_rx const* rx = (struct sc_msg_can_rx const*)msg;
                uint32_t can_id = dev->dev_to_host32(rx->can_id);
                uint32_t timestamp_us = dev->dev_to_host32(rx->timestamp_us);
                uint8_t len = dlc_to_len(rx->dlc);
                uint8_t bytes = sizeof(*rx);
                double timestamp = track_device_time(timestamp_us);
                PyPtr data;
                int rtr = 0;
                int fdf = 0;
                int ext = 0;
                //int is_error_frame = 0;
                int dlc = 0;
                int brs = 0;
                int esi = 0;

                if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR)) {
                    bytes += len;
                }

                if (msg->len < len) {
                    fprintf(stderr, "malformed sc_msg_can_rx\n");
                    return false;
                }

                ext = (rx->flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT;
                dlc = rx->dlc;

                if (rx->flags & SC_CAN_FRAME_FLAG_FDF) {
                    data.reset(PyByteArray_FromStringAndSize((char const*)rx->data, len));
                    fdf = 1;
                    esi = (rx->flags & SC_CAN_FRAME_FLAG_BRS) == SC_CAN_FRAME_FLAG_BRS;
                    brs = (rx->flags & SC_CAN_FRAME_FLAG_ESI) == SC_CAN_FRAME_FLAG_ESI;
                } else {
                    if (rx->flags & SC_CAN_FRAME_FLAG_RTR) {
                        rtr = 1;
                    } else {
                        data.reset(PyByteArray_FromStringAndSize((char const*)rx->data, len));
                    }
                }

                PyPtr kwargs(
                    Py_BuildValue(
                        "{s:d,s:k,s:i,s:i,s:i,s:O,s:i,s:O,s:i,s:i,s:i,s:i}",
                        "timestamp",
                        timestamp,
                        "arbitration_id",
                        can_id,
                        "is_extended_id",
                        ext,
                        "is_remote_frame",
                        rtr,
                        "is_error_frame",
                        0,
                        "channel",
                        Py_None,
                        "dlc",
                        dlc,
                        "data",
                        data.get(),
                        "is_fd",
                        fdf,
                        "is_rx",
                        1,
                        "bitrate_switch",
                        brs,
                        "error_state_indicator",
                        esi
                    )
                );

                data.release();

                // create can.Message
                PyPtr args(PyTuple_New(0));
                PyPtr msg(PyObject_Call(can_message_type.get(), args.get(), kwargs.get()));

                rx_queue.emplace_back(msg.get());

                msg.release();

                SetEvent(rx_event);
            } break;
            case SC_MSG_CAN_TXR: {
                struct sc_msg_can_txr const* txr = (struct sc_msg_can_txr const*)msg;
                uint32_t timestamp_us = 0;

                if (msg->len < sizeof(*txr)) {
                    fprintf(stderr, "malformed sc_msg_can_txr\n");
                    return false;
                }

                timestamp_us = dev->dev_to_host32(txr->timestamp_us);
                double timestamp = track_device_time(timestamp_us);

                if (available_track_id_count == _countof(available_track_id_buffer)) {
                    fprintf(stderr, "TXR track id buffer overrun\n");
                    return false;
                }

                // return track id
                available_track_id_buffer[available_track_id_count++] = txr->track_id;

                if (receive_own_messages) {
                    PyPtr echo(echos[txr->track_id]);
                    echos[txr->track_id] = nullptr;
                    assert(echo.get());

                    PyPtr py_timestamp(PyFloat_FromDouble(timestamp));
                    PyObject_SetAttrString(echo.get(), "is_rx", Py_False);
                    PyObject_SetAttrString(echo.get(), "timestamp", py_timestamp.get());

                    rx_queue.emplace_back(echo.get());

                    echo.release();

                    SetEvent(rx_event);
                }
            } break;
            default:
                fprintf(stderr, "WARN: unhandled msg id=%02x len=%u\n", msg->id, msg->len);
                break;
            }
        }

        return true;
    }

    PyObject* send(PyObject *msg, DWORD timeout_winapi)
    {
        int error = SC_DLL_ERROR_NONE;
        uint32_t buffer[25];
        struct sc_msg_can_tx* tx = (struct sc_msg_can_tx*)buffer;
        void* data_ptr = nullptr;
        memset(tx, 0, sizeof(*tx));
        uint8_t data_len = 0;
        uint8_t extra_len = 0;

        if (fw_ge_060) {
            data_ptr = &tx->data[3];
            extra_len = 3;
        } else {
            data_ptr = &tx->data[0];
            extra_len = 0;
        }

        PyPtr ext(PyObject_GetAttrString(msg, "is_extended_id"));
        PyPtr rtr(PyObject_GetAttrString(msg, "is_remote_frame"));
        PyPtr can_id(PyObject_GetAttrString(msg, "arbitration_id"));
        PyPtr dlc(PyObject_GetAttrString(msg, "dlc"));
        PyPtr fdf(PyObject_GetAttrString(msg, "is_fd"));
        PyPtr brs(PyObject_GetAttrString(msg, "bitrate_switch"));
        PyPtr esi(PyObject_GetAttrString(msg, "error_state_indicator"));
        PyPtr data(PyObject_GetAttrString(msg, "data"));

        if (PyLong_Check(can_id.get())) {
            auto id = (uint32_t)PyLong_AsUnsignedLong(can_id.get());

            if (Py_IsTrue(ext.get())) {
                tx->can_id = dev->dev_to_host32(id & 0x1FFFFFFF);
                tx->flags |= SC_CAN_FRAME_FLAG_EXT;
            } else {
                tx->can_id = dev->dev_to_host32(id & 0x7FF);
            }
        } else {
            PyErr_Format(PyExc_ValueError, "send: arbitration_id must be int");
            return nullptr;
        }

        if (PyLong_Check(dlc.get())) {
            tx->dlc = (uint8_t)(PyLong_AsUnsignedLong(dlc.get()) & 0xf);
            data_len = dlc_to_len(tx->dlc);
        } else {
            PyErr_Format(PyExc_ValueError, "send: dlc must be int");
            return nullptr;
        }

        if (Py_IsTrue(fdf.get())) {
            if (fdf) {
                if (data_len) {
                    Py_buffer buffer;

                    if (0 == PyObject_GetBuffer(data.get(), &buffer, PyBUF_C_CONTIGUOUS)) {
                        memcpy(data_ptr, buffer.buf, std::min<Py_ssize_t>(data_len, buffer.len));
                        PyBuffer_Release(&buffer);
                    }
                    else {
                        return nullptr;
                    }
                }

                tx->flags |= SC_CAN_FRAME_FLAG_FDF;
                if (Py_IsTrue(brs.get())) {
                    tx->flags |= SC_CAN_FRAME_FLAG_BRS;
                }

                if (Py_IsTrue(esi.get())) {
                    tx->flags |= SC_CAN_FRAME_FLAG_ESI;
                }
            } else {
                PyErr_Format(PyExc_ValueError, "send: bus not configured for CAN-FD");
                return nullptr;
            }
        } else {
            if (Py_IsTrue(rtr.get())) {
                tx->flags |= SC_CAN_FRAME_FLAG_RTR;
            } else {
                if (data_len) {
                    Py_buffer buffer;

                    if (0 == PyObject_GetBuffer(data.get(), &buffer, PyBUF_C_CONTIGUOUS)) {
                        memcpy(data_ptr, buffer.buf, std::min<Py_ssize_t>(data_len, buffer.len));
                        PyBuffer_Release(&buffer);
                    }
                    else {
                        return nullptr;
                    }
                }
            }
        }

        tx->id = fw_ge_060 ? 0x25 : SC_MSG_CAN_TX;
        tx->len = (uint8_t)(sizeof(*tx) + data_len + extra_len);

        // align
        if (tx->len & (SC_MSG_CAN_LEN_MULTIPLE - 1)) {
            tx->len += SC_MSG_CAN_LEN_MULTIPLE - (tx->len & (SC_MSG_CAN_LEN_MULTIPLE - 1));
        }

        uint64_t const start = mono_millis();

        for (;;) {
            // one pass through rx loop
            for (unsigned events_at_start = event_counter - 1;
                events_at_start != event_counter;
                events_at_start = event_counter) {

                ResetEvent(rx_event);
                error = sc_can_stream_rx(stream, 0);

                switch (error) {
                case SC_DLL_ERROR_NONE:
                case SC_DLL_ERROR_TIMEOUT:
                case SC_DLL_ERROR_USER_HANDLE_SIGNALED:
                    break;
                default:
                    SetCanOperationError("send: stream failed: %s (%d)", sc_strerror(error), error);
                    return nullptr;
                }
            }

            if (available_track_id_count) {
                break;
            }

            if (INFINITE == timeout_winapi) {
                ResetEvent(rx_event);
                error = sc_can_stream_rx(stream, INFINITE);
            } else {
                uint64_t const now = mono_millis();

                uint64_t const elapsed = now - start;

                if (elapsed >= timeout_winapi) {
                    break;
                } else {
                    ResetEvent(rx_event);
                    error = sc_can_stream_rx(stream, timeout_winapi - static_cast<DWORD>(elapsed));
                }
            }

            switch (error) {
            case SC_DLL_ERROR_NONE:
            case SC_DLL_ERROR_TIMEOUT:
            case SC_DLL_ERROR_USER_HANDLE_SIGNALED:
                break;

            default:
                SetCanOperationError("send: stream failed: %s (%d)", sc_strerror(error), error);
                return nullptr;
            }
        }

        assert(available_track_id_count);

        // create echo frame
        PyPtr echo;
        if (receive_own_messages) {
            echo.reset(PyObject_CallOneArg(copy_deepcopy.get(), msg));
            if (!echo) {
                return nullptr;
            }
        }

        uint8_t const track_id = available_track_id_buffer[--available_track_id_count];

        tx->track_id = track_id;

        error = sc_can_stream_tx(stream, (uint8_t*)tx, tx->len);
        if (SC_DLL_ERROR_NONE == error) {
            echos[track_id] = echo.release();
        } else {
            available_track_id_buffer[available_track_id_count++] = track_id;
            SetCanOperationError("send: failed: %s (%d)", sc_strerror(error), error);
            return nullptr;
        }

        Py_RETURN_NONE;
    }

    PyObject* recv(DWORD timeout_winapi)
    {
        uint64_t const start = mono_millis();
        int error = 0;

        for (;;) {
            // one pass through rx loop
            for (unsigned events_at_start = event_counter - 1;
                events_at_start != event_counter;
                events_at_start = event_counter) {

                ResetEvent(rx_event);
                error = sc_can_stream_rx(stream, 0);
                switch (error) {
                case SC_DLL_ERROR_NONE:
                case SC_DLL_ERROR_TIMEOUT:
                case SC_DLL_ERROR_USER_HANDLE_SIGNALED:
                    break;
                default:
                    SetCanOperationError("recv: stream failed: %s (%d)", sc_strerror(error), error);
                    return nullptr;
                }

                if (!rx_queue.empty()) {
                    PyObject* ret = PyTuple_New(2);
                    PyObject* o = rx_queue.front();

                    rx_queue.pop_front();
                    
                    PyTuple_SET_ITEM(ret, 0, o);
                    PyTuple_SET_ITEM(ret, 1, Py_NewRef(Py_False));

                    return ret;
                }
            }

            if (INFINITE == timeout_winapi) {
                ResetEvent(rx_event);
                error = sc_can_stream_rx(stream, INFINITE);
            } else {
                uint64_t const now = mono_millis();

                uint64_t const elapsed = now - start;

                if (elapsed >= timeout_winapi) {
                    break;
                } else {
                    ResetEvent(rx_event);
                    error = sc_can_stream_rx(stream, timeout_winapi - static_cast<DWORD>(elapsed));
                }
            }

            switch (error) {
            case SC_DLL_ERROR_NONE:
            case SC_DLL_ERROR_TIMEOUT:
            case SC_DLL_ERROR_USER_HANDLE_SIGNALED:
                break;
            default:
                SetCanOperationError("recv: stream failed: %s (%d)", sc_strerror(error), error);
                return nullptr;
            }
        }

        Py_INCREF(rx_no_msg_result.get());

        return rx_no_msg_result.get();
    }

    PyObject* get_state() const 
    {
        PyObject* result = nullptr;

        switch (bus_status) {
        case SC_CAN_STATUS_ERROR_PASSIVE:
            result = can_bus_state_passive.get();
            break;
        case SC_CAN_STATUS_BUS_OFF:
            result = can_bus_state_error.get();
            break;
        default:
            result = can_bus_state_active.get();
            break;
        }

        return Py_NewRef(result);
    }

    PyObject* get_channel_info() const
    {
        return Py_NewRef(channel_info.get());
    }
};

struct py_sc_exclusive
{
    sc_exclusive sc;
};

int sc_exclusive_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    py_sc_exclusive* sc = new ((py_sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size)) py_sc_exclusive();
    sc_config config;

    if (!sc_parse_args(args, kwargs, config)) {
        return -1;
    }

    if (!sc->sc.init(kwargs, config)) {
        return -1;
    }

    // set filters on base class
    {
        PyObject* args[] = { self, config.filters };
        PyPtr set_filters(PyObject_GetAttrString((PyObject*)Py_TYPE(self)->tp_base, "set_filters"));
        PyPtr ret(PyObject_Vectorcall(set_filters.get(), args, _countof(args), nullptr));
    }

    if (config.fdf)
    {
        PyPtr ev(PyObject_GetAttrString(can_bus_can_protocol_type.get(), "CAN_FD"));
        PyObject_SetAttrString(self, "_can_protocol", ev.get());
    }

    {
        PyPtr desc(sc->sc.get_channel_info());
        PyObject_SetAttrString(self, "channel_info", desc.get());
    }

    return 0;
}

void sc_exclusive_dealloc(PyObject*self)
{
    py_sc_exclusive* sc = (py_sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size);

    sc->~py_sc_exclusive();

    Py_TYPE(self)->tp_free((PyObject *) self);
}


PyObject *sc_exclusive_send(PyObject*self, PyObject *args, PyObject *kwargs)
{
    py_sc_exclusive* sc = (py_sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size);
    PyObject* msg = nullptr;
    PyObject* timeout = nullptr;
    DWORD timeout_winapi = 0;

    char const * const kwlist[] = {
        "msg",
        "timeout",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "OO|OO",
        (char**)kwlist,
        &msg,
        &timeout)) {
        return nullptr;
    }

    if (!PyObject_IsInstance(msg, can_message_type.get())) {
        PyErr_Format(PyExc_ValueError, "send: first argument must be an instance of can.Message");
        return nullptr;
    }

    if (!sc_to_timeout(timeout, &timeout_winapi)) {
        return nullptr;
    }

    return sc->sc.send(msg, timeout_winapi);
}

PyObject *sc_exclusive__recv_internal(PyObject*self, PyObject *args, PyObject *kwargs)
{
    py_sc_exclusive* sc = (py_sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size);
    PyObject* timeout = nullptr;
    DWORD timeout_winapi = 0;

    char const * const kwlist[] = {
        "timeout",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "O|O",
        (char**)kwlist,
        &timeout)) {
        return nullptr;
    }

    if (!sc_to_timeout(timeout, &timeout_winapi)) {
        return nullptr;
    }

    return sc->sc.recv(timeout_winapi);
}


PyObject *sc_exclusive_shutdown(PyObject*self, PyObject */* args = nullptr */)
{
    // call base class shutdown()
    PyPtr func(PyObject_GetAttrString((PyObject*)Py_TYPE(self)->tp_base, "shutdown"));
    PyPtr result(PyObject_CallFunctionObjArgs(func.get(), self, nullptr));

    // cleanup
    py_sc_exclusive* sc = (py_sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size);
    sc->sc.stop();

    Py_RETURN_NONE;
}

PyObject *sc_exclusive_state_get(PyObject* self, void*)
{
    py_sc_exclusive* sc = (py_sc_exclusive *)(((uint8_t*)self) + can_bus_abc_size);
    
    return sc->sc.get_state();
}

PyMethodDef sc_exclusive_methods[] = {
    {"send", (PyCFunction) sc_exclusive_send, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("transmit CAN frame")},
    {"_recv_internal", (PyCFunction) sc_exclusive__recv_internal, METH_VARARGS | METH_KEYWORDS, nullptr},
    {"shutdown", (PyCFunction) sc_exclusive_shutdown, METH_NOARGS, PyDoc_STR("shutdown CAN bus")},
    {nullptr},
};

PyGetSetDef sc_exclusive_getset[] = {
    {"state", sc_exclusive_state_get, nullptr, PyDoc_STR("Return the current state of the hardware"), nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot sc_exclusive_type_spec_slots[] = {
    { Py_tp_doc, (void*)
        "SuperCAN device exclusive CAN bus access\n" 
        "\n"
        "For parameter description, see :class: supercan.Bus"
        "\n"
    },
    { Py_tp_init, (void*)&sc_exclusive_init },
    { Py_tp_dealloc, (void*)&sc_exclusive_dealloc },
    { Py_tp_methods, sc_exclusive_methods },
    { Py_tp_getset, sc_exclusive_getset },
    {0, nullptr} // sentinel
};

PyType_Spec sc_exclusive_type_spec = {
    .name = "supercan.Exclusive",
    .basicsize = -(int)sizeof(py_sc_exclusive),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE,
    .slots = sc_exclusive_type_spec_slots,
};



struct sc_mm_data {
    sc_can_mm_header* hdr;
    HANDLE file;
    HANDLE event;
    uint32_t elements;

    ~sc_mm_data()
    {
        unmap();
    }

    sc_mm_data()
    {
        ZeroMemory(this, sizeof(*this));
    }

    void unmap()
    {
        if (hdr) {
            UnmapViewOfFile(hdr);
            hdr = nullptr;
        }

        if (event) {
            CloseHandle(event);
            event = nullptr;
        }

        if (file) {
            CloseHandle(file);
            file = nullptr;
        }
    }

    bool map(SuperCAN::SuperCANRingBufferMapping const* y)
    {
        bool result = false;

        elements = y->Elements;

        file = OpenFileMappingW(
                FILE_MAP_READ | FILE_MAP_WRITE,   // read/write access
                FALSE,                 // do not inherit the name
                y->MemoryName);

        if (!file) {
            auto e = GetLastError();

            SetCanInitializationError("OpenFileMappingW failed: %lu\n", e);
            return false;
        }

        event = OpenEventW(
            EVENT_ALL_ACCESS,
            FALSE,
            y->EventName);

        if (!event) {
            auto e = GetLastError();

            SetCanInitializationError("OpenEventW failed: %lu\n", e);
            goto error_exit;
        }
        
        hdr = static_cast<decltype(hdr)>(MapViewOfFile(file, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, y->Bytes));
        if (!hdr) {
            auto e = GetLastError();

            SetCanInitializationError("MapViewOfFile failed: %lu\n", e);            
            goto error_exit;
        }

        result = true;

    success_exit:
        return result;

    error_exit:
        unmap();
        goto success_exit;
    }
};


struct sc_shared : public sc_base
{
    SuperCAN::ISuperCAN2Ptr sc;
    SuperCAN::ISuperCANDevice3Ptr dev;
    PyPtr channel_info;
    sc_mm_data rx;
    sc_mm_data tx;
    uint64_t initial_device_time_us;
    uint64_t initial_system_time_100ns;
    uint32_t track_id;
    uint8_t bus_status;
    bool com_initialized;
    bool dev_initialized;
    bool fdf;
    bool receive_own_messages;


    ~sc_shared() {
        stop_();

        dev = nullptr;
        sc = nullptr;

        if (com_initialized) {
            CoUninitialize();
        }
    }

    sc_shared() {
        com_initialized = false;
        dev_initialized = false;
        track_id = 0;
        fdf = false;
        receive_own_messages = false;
        initial_device_time_us = 0;
        initial_device_time_us = 0;
        bus_status = SC_CAN_STATUS_ERROR_ACTIVE;
    }

    bool init(PyObject* kwargs, sc_config& config)
    {
        PyObject* py_init_access = PyDict_GetItemString(kwargs, "init_access"); // borrowed
        bool init_access = true;

        /*char const * const kwlist[] = {
            "init_access",
            nullptr,
        };
    
        if (!PyArg_ParseTupleAndKeywords(
            args,
            kwargs,
            "|$O",
            (char**)kwlist,
            &py_init_access
        )) {
            return false;
        }*/

        if (!get_bool_arg(py_init_access, "init_access", &init_access)) {
            return false;
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        switch (hr) {
        case S_OK:
            com_initialized = true;    
            break;
        case S_FALSE:
            break;
        case RPC_E_CHANGED_MODE:
            break;
        default:
            SetCanInitializationError("failed to initialze COM (hr=%lx)\n", hr);
            return false;
        }

        hr = sc.CreateInstance(__uuidof(SuperCAN::CSuperCAN));

        if (FAILED(hr)) {
            SetCanInitializationError("failed to created instance of SuperCAN (hr=%lx)\n", hr);
            return false;
        }

        unsigned long dev_count = 0;
        hr = sc->DeviceScan(&dev_count);
        if (FAILED(hr)) {
            SetCanInitializationError("device scan failed (hr=%lx)\n", hr);
            return false;
        }

        if (!dev_count) {
            PyErr_SetString(PyExc_ValueError, "no " SC_NAME " devices found");
            return false;
        }

        unsigned long best_index = dev_count;
        SuperCAN::SuperCANDeviceData2 dev_data;
        SuperCAN::SuperCANRingBufferMapping rx_mm_data, tx_mm_data;
        char serial_str[1 + sizeof(dev_data.sn_bytes) * 2];

        ZeroMemory(&dev_data, sizeof(dev_data));
        ZeroMemory(&rx_mm_data, sizeof(rx_mm_data));
        ZeroMemory(&tx_mm_data, sizeof(tx_mm_data));

        for (unsigned long i = 0; i < best_index; ++i) {
            SuperCAN::ISuperCANDevicePtr device_ptr;
            SuperCAN::ISuperCANDevice3Ptr device_ptr3;
                       
            hr = sc->DeviceOpen(i, (SuperCAN::ISuperCANDevice**)&device_ptr);
            if (FAILED(hr)) {
                continue;
            }

            hr = device_ptr->QueryInterface(&device_ptr3);
            if (FAILED(hr)) {
               continue;
            }

            hr = device_ptr3->GetDeviceData2(&dev_data);
            if (FAILED(hr)) {
                continue;
            }

            SysFreeString(dev_data.name); // free BSTR
            dev_data.name = nullptr;

            if (config.serial.size()) {
                for (size_t i = 0; i < std::min((size_t)dev_data.sn_length, _countof(serial_str) - 1); ++i) {
                    snprintf(&serial_str[i * 2], 3, "%02x", dev_data.sn_bytes[i]);
                }

                if (_stricmp(serial_str, config.serial.c_str()) == 0) {
                    if (config.channel_index < 0 || (unsigned)config.channel_index == dev_data.ch_index) {
                        best_index = i;
                        break;
                    }
                }
            } else {
                best_index = i;
                break;
            }
        }

        if (dev_count == best_index) {
            PyPtr msg(PyUnicode_FromFormat("failed to find device matching serial '%s', channel index %d", config.serial.c_str(), config.channel_index));
            PyErr_SetObject(PyExc_ValueError, msg.get());
    
            return false;
        }

        {
            SuperCAN::ISuperCANDevicePtr device_ptr;

            hr = sc->DeviceOpen(best_index, (SuperCAN::ISuperCANDevice**)&device_ptr);
            if (FAILED(hr)) {
                SetCanInitializationError("failed to open device (hr=%lx)\n", hr);
                return false;
            }

            hr = device_ptr->QueryInterface(&dev);
            if (FAILED(hr)) {
                SetCanInitializationError("QueryInterface failed (hr=%lx)\n", hr);
                return false;
            }
        }

        hr = dev->GetDeviceData2(&dev_data);
        if (FAILED(hr)) {
            SetCanInitializationError("failed to get device data (hr=%lx)\n", hr);
            return false;
        }

        std::wstring dev_name = dev_data.name;

        SysFreeString(dev_data.name); // free BSTR
        dev_data.name = nullptr;

        for (size_t i = 0; i < std::min((size_t)dev_data.sn_length, _countof(serial_str) - 1); ++i) {
            snprintf(&serial_str[i * 2], 3, "%02x", dev_data.sn_bytes[i]);
        }


        hr = dev->GetRingBufferMappings(&rx_mm_data, &tx_mm_data);
        if (FAILED(hr)) {
            SetCanInitializationError("failed to get RX/TX ring buffer configuration (hr=%lx)\n", hr);
            return false;
        }

        auto rx_map_ok = rx.map(&rx_mm_data);
        auto tx_map_ok = tx.map(&tx_mm_data);

        SysFreeString(tx_mm_data.EventName); // free BSTR
        SysFreeString(tx_mm_data.MemoryName); // free BSTR
        tx_mm_data.EventName = nullptr;
        tx_mm_data.MemoryName = nullptr;

        SysFreeString(rx_mm_data.EventName); // free BSTR
        SysFreeString(rx_mm_data.MemoryName); // free BSTR
        rx_mm_data.EventName = nullptr;
        rx_mm_data.MemoryName = nullptr;


        if (!rx_map_ok || !tx_map_ok) {
            return false;
        }

        if (init_access) {
            char config_access = 0;
            unsigned long access_timeout_ms = 0;
            struct can_bit_timing_settings nominal_settings, data_settings;
            struct can_bit_timing_hw_contraints nominal_hw_constraints, data_hw_constraints;
    
            hr = dev->AcquireConfigurationAccess(&config_access, &access_timeout_ms);
            if (FAILED(hr)) {
                SetCanInitializationError("failed to acquire config access (hr=%lx)\n", hr);
                return false;
            }
    
            if (!config_access) {
                SetCanInitializationError("failed to get configuration access\n");
                return false;
            }
    
            // {
            //     ISuperCANDevice2Ptr device_ptr2;
    
            //     hr = dev->QueryInterface(&device_ptr2);
            //     if (SUCCEEDED(hr)) {
            //         hr = device_ptr2->SetLogLevel(ac->debug_log_level);
            //         if (FAILED(hr)) {
            //             fprintf(stderr, "ERROR: failed to set device log level (hr=%lx)\n", hr);
            //             return map_hr_to_error(hr);
            //         }
            //     }
            //     else if (E_NOINTERFACE == hr) {
            //         // ok, unsupported
            //     }
            //     else {
            //         fprintf(stderr, "ERROR: failed to query interface for ISuperCANDevice2 (hr=%lx)\n", hr);
            //         return map_hr_to_error(hr);
            //     }
            // }
        
            SuperCAN::SuperCANBitTimingParams params;
            ZeroMemory(&params, sizeof(params));
    
            hr = dev->SetBus(0);
            if (FAILED(hr)) {
                SetCanInitializationError("failed to go off bus (hr=%lx)\n", hr);
                return false;
            }
    
            nominal_hw_constraints.brp_min = dev_data.nm_min.brp;
            nominal_hw_constraints.brp_max = dev_data.nm_max.brp;
            nominal_hw_constraints.brp_step = 1;
            nominal_hw_constraints.clock_hz = dev_data.can_clock_hz;
            nominal_hw_constraints.sjw_max = dev_data.nm_max.sjw;
            nominal_hw_constraints.tseg1_min = dev_data.nm_min.tseg1;
            nominal_hw_constraints.tseg1_max = dev_data.nm_max.tseg1;
            nominal_hw_constraints.tseg2_min = dev_data.nm_min.tseg2;
            nominal_hw_constraints.tseg2_max = dev_data.nm_max.tseg2;
    
            data_hw_constraints.brp_min = dev_data.dt_min.brp;
            data_hw_constraints.brp_max = dev_data.dt_max.brp;
            data_hw_constraints.brp_step = 1;
            data_hw_constraints.clock_hz = dev_data.can_clock_hz;
            data_hw_constraints.sjw_max = dev_data.dt_max.sjw;
            data_hw_constraints.tseg1_min = dev_data.dt_min.tseg1;
            data_hw_constraints.tseg1_max = dev_data.dt_max.tseg1;
            data_hw_constraints.tseg2_min = dev_data.dt_min.tseg2;
            data_hw_constraints.tseg2_max = dev_data.dt_max.tseg2;
    
            auto error = cia_fd_cbt_real(
                &nominal_hw_constraints,
                &data_hw_constraints,
                &config.nominal_user_constraints,
                &config.data_user_constraints,
                &nominal_settings,
                &data_settings);
            switch (error) {
            case CAN_BTRE_NO_SOLUTION:
                SetCanInitializationError("The chosen nominal/data bitrate/sjw cannot be configured on the device.\n");
                return false;
            case CAN_BTRE_NONE:
                break;
            default:
                SetCanInitializationError("Ooops. Failed to compute CAN bittiming.\n");
                return false;
            }
    
            params.brp = static_cast<unsigned short>(nominal_settings.brp);
            params.sjw = static_cast<unsigned char>(nominal_settings.sjw);
            params.tseg1 = static_cast<unsigned short>(nominal_settings.tseg1);
            params.tseg2 = static_cast<unsigned char>(nominal_settings.tseg2);
            hr = dev->SetNominalBitTiming(params);
            if (FAILED(hr)) {
                SetCanInitializationError("failed to set nominal bit timing (hr=%lx)\n", hr);
                return false;
            }
    
            unsigned long features = SC_FEATURE_FLAG_TXR;
            if (config.fdf) {
                features |= SC_FEATURE_FLAG_FDF;
    
                params.brp = static_cast<unsigned short>(data_settings.brp);
                params.sjw = static_cast<unsigned char>(data_settings.sjw);
                params.tseg1 = static_cast<unsigned short>(data_settings.tseg1);
                params.tseg2 = static_cast<unsigned char>(data_settings.tseg2);
                hr = dev->SetDataBitTiming(params);
                if (FAILED(hr)) {
                    SetCanInitializationError("failed to set data bit timing (hr=%lx)\n", hr);
                    return false;
                }
            }
    
            hr = dev->SetFeatureFlags(features);
            if (FAILED(hr)) {
                SetCanInitializationError("failed to set features (hr=%lx)\n", hr);
                return false;
            }
    
            hr = dev->SetBus(1);
            if (FAILED(hr)) {
                SetCanInitializationError("failed to go on bus (hr=%lx)\n", hr);
                return false;
            }

            dev_initialized = true;
        }

        fdf = config.fdf;
        receive_own_messages = config.receive_own_messages;
        channel_info.reset(PyUnicode_FromFormat("%ls (%s) CH%u", dev_name.c_str(), serial_str, dev_data.ch_index));
      
        return true;
    }

    void stop_()
    {
        if (dev_initialized) {
            char config_access = 0;
            unsigned long access_timeout_ms = 0;

            auto hr = dev->AcquireConfigurationAccess(&config_access, &access_timeout_ms);
            if (SUCCEEDED(hr) && config_access) {
                (void)dev->SetBus(0);
            }
        }
    }

    void stop()
    {
        stop_();    
    }

    double track_device_time(uint64_t device_time_us) {
        uint64_t now_100ns;
        
        if (initial_device_time_us == 0) {
            FILETIME now;

            GetSystemTimeAsFileTime(&now);

            initial_system_time_100ns = now.dwHighDateTime;
            initial_system_time_100ns <<= 32;
            initial_system_time_100ns |= now.dwLowDateTime;

            initial_device_time_us = device_time_us;
        }

        now_100ns = initial_system_time_100ns;
        now_100ns += (device_time_us - initial_device_time_us) * UINT64_C(10);
        now_100ns -= system_time_to_epoch_offset_100ns;

        return now_100ns * 1e-7;
    }

    PyObject* send(PyObject *msg, DWORD timeout_winapi)
    {
        auto const gi = tx.hdr->get_index;
        auto const pi = tx.hdr->put_index;
        decltype(pi) const used = pi - gi;

        if (used > tx.elements) {
            SetCanOperationError("TX mm data mismatch (pi=%lu gi=%u used=%u elements=%u)\n",
                static_cast<unsigned long>(pi),
                static_cast<unsigned long>(gi),
                static_cast<unsigned long>(used),
                static_cast<unsigned long>(tx.elements));
            return nullptr;
        }

        if (used == tx.elements) {
            SetCanOperationError("TX queue full\n");
            return nullptr;
        }
        
        auto const index = pi % tx.elements;
        auto* tx_slot = &tx.hdr->elements[index].tx;
        unsigned data_len = 0;

        tx_slot->flags = 0;


        PyPtr ext(PyObject_GetAttrString(msg, "is_extended_id"));
        PyPtr rtr(PyObject_GetAttrString(msg, "is_remote_frame"));
        PyPtr can_id(PyObject_GetAttrString(msg, "arbitration_id"));
        PyPtr dlc(PyObject_GetAttrString(msg, "dlc"));
        PyPtr fdf(PyObject_GetAttrString(msg, "is_fd"));
        PyPtr brs(PyObject_GetAttrString(msg, "bitrate_switch"));
        PyPtr esi(PyObject_GetAttrString(msg, "error_state_indicator"));
        PyPtr data(PyObject_GetAttrString(msg, "data"));

        if (PyLong_Check(can_id.get())) {
            auto id = (uint32_t)PyLong_AsUnsignedLong(can_id.get());

            if (Py_IsTrue(ext.get())) {
                tx_slot->can_id = id & 0x1FFFFFFFU;
                tx_slot->flags |= SC_CAN_FRAME_FLAG_EXT;
            } else {
                tx_slot->can_id = id & 0x7FFU;
            }
        } else {
            PyErr_Format(PyExc_ValueError, "send: arbitration_id must be int");
            return nullptr;
        }

        if (PyLong_Check(dlc.get())) {
            tx_slot->dlc = (uint8_t)(PyLong_AsUnsignedLong(dlc.get()) & 0xf);
            data_len = dlc_to_len(tx_slot->dlc);
        } else {
            PyErr_Format(PyExc_ValueError, "send: dlc must be int");
            return nullptr;
        }

        if (Py_IsTrue(fdf.get())) {
            if (fdf) {
                if (data_len) {
                    Py_buffer buffer;

                    if (0 == PyObject_GetBuffer(data.get(), &buffer, PyBUF_C_CONTIGUOUS)) {
                        memcpy(tx_slot->data, buffer.buf, std::min<Py_ssize_t>(data_len, buffer.len));
                        PyBuffer_Release(&buffer);
                    }
                    else {
                        return nullptr;
                    }
                }

                tx_slot->flags |= SC_CAN_FRAME_FLAG_FDF;
                if (Py_IsTrue(brs.get())) {
                    tx_slot->flags |= SC_CAN_FRAME_FLAG_BRS;
                }

                if (Py_IsTrue(esi.get())) {
                    tx_slot->flags |= SC_CAN_FRAME_FLAG_ESI;
                }
            } else {
                PyErr_Format(PyExc_ValueError, "send: bus not configured for CAN-FD");
                return nullptr;
            }
        } else {
            if (Py_IsTrue(rtr.get())) {
                tx_slot->flags |= SC_CAN_FRAME_FLAG_RTR;
            } else {
                if (data_len) {
                    Py_buffer buffer;

                    if (0 == PyObject_GetBuffer(data.get(), &buffer, PyBUF_C_CONTIGUOUS)) {
                        memcpy(tx_slot->data, buffer.buf, std::min<Py_ssize_t>(data_len, buffer.len));
                        PyBuffer_Release(&buffer);
                    }
                    else {
                        return nullptr;
                    }
                }
            }
        }

        tx_slot->type = SC_MM_DATA_TYPE_CAN_TX;
        tx_slot->track_id = track_id++;    
        tx.hdr->put_index = pi + 1;
        
        // notify COM server of work
        SetEvent(tx.event);

        Py_RETURN_NONE;
    }

    PyObject* recv(DWORD timeout_winapi)
    {
        auto rx_lost = InterlockedExchange(&rx.hdr->can_lost_rx, 0);
        if (rx_lost) {
            //fprintf(stderr, "ERROR: %lu rx messages lost\n", rx_lost);
        }

        auto tx_lost = InterlockedExchange(&rx.hdr->can_lost_tx, 0);
        if (tx_lost) {
            //fprintf(stderr, "ERROR: %lu tx messages lost\n", tx_lost);
        }

        auto status_lost = InterlockedExchange(&rx.hdr->can_lost_status, 0);
        if (status_lost) {
            //fprintf(stderr, "ERROR: %lu status messages lost\n", status_lost);
        }

        auto error_lost = InterlockedExchange(&rx.hdr->can_lost_error, 0);
        if (error_lost) {
            //fprintf(stderr, "ERROR: ERROR %lu error messages lost\n", error_lost);
        }
        

        auto const start = mono_millis();
        auto gi = rx.hdr->get_index;

        for (;;) {
            auto const pi = rx.hdr->put_index;
            decltype(pi) used = pi - gi;

            if (used > rx.elements) {
                SetCanOperationError("RX mm data mismatch (pi=%lu gi=%u used=%u elements=%u)\n",
                    static_cast<unsigned long>(pi),
                    static_cast<unsigned long>(gi),
                    static_cast<unsigned long>(used),
                    static_cast<unsigned long>(rx.elements));
                return nullptr;
            }

            if (used) {
                PyObject* result = nullptr;

                for (uint32_t i = 0; i < used; ++i, ++gi) {
                    auto const index = gi % rx.elements;
                    auto* hdr = &rx.hdr->elements[index].hdr;
        
                    switch (hdr->type) {
                    case SC_MM_DATA_TYPE_CAN_STATUS: {
                        auto* status = &rx.hdr->elements[index].status;
                        bus_status = status->bus_status;
                        
                        // if (!ac->candump && (ac->log_flags & LOG_FLAG_CAN_STATE)) {
                        //     bool log = false;
                        //     if (ac->log_on_change) {
                        //         log = ac->can_rx_errors_last != status->rx_errors ||
                        //             ac->can_tx_errors_last != status->tx_errors ||
                        //             ac->can_bus_state_last != status->bus_status;
                        //     }
                        //     else {
                        //         log = true;
                        //     }
        
                        //     ac->can_rx_errors_last = status->rx_errors;
                        //     ac->can_tx_errors_last = status->tx_errors;
                        //     ac->can_bus_state_last = status->bus_status;
        
                        //     if (log) {
                        //         fprintf(stdout, "CAN rx errors=%u tx errors=%u bus=", status->rx_errors, status->tx_errors);
                        //         switch (status->bus_status) {
                        //         case SC_CAN_STATUS_ERROR_ACTIVE:
                        //             fprintf(stdout, "error_active");
                        //             break;
                        //         case SC_CAN_STATUS_ERROR_WARNING:
                        //             fprintf(stdout, "error_warning");
                        //             break;
                        //         case SC_CAN_STATUS_ERROR_PASSIVE:
                        //             fprintf(stdout, "error_passive");
                        //             break;
                        //         case SC_CAN_STATUS_BUS_OFF:
                        //             fprintf(stdout, "off");
                        //             break;
                        //         default:
                        //             fprintf(stdout, "unknown");
                        //             break;
                        //         }
                        //         fprintf(stdout, "\n");
                        //     }
                        // }
        
                        // if (!ac->candump && (ac->log_flags & LOG_FLAG_USB_STATE)) {
                        //     bool log = false;
                        //     bool irq_queue_full = status->flags & SC_CAN_STATUS_FLAG_IRQ_QUEUE_FULL;
                        //     bool desync = status->flags & SC_CAN_STATUS_FLAG_TXR_DESYNC;
                        //     auto rx_lost2 = status->rx_lost;
                        //     auto tx_dropped = status->tx_dropped;
        
                        //     if (ac->log_on_change) {
                        //         log = ac->usb_rx_lost != rx_lost2 ||
                        //             ac->usb_tx_dropped != tx_dropped ||
                        //             irq_queue_full || desync;
                        //     }
                        //     else {
                        //         log = true;
                        //     }
        
                        //     ac->usb_rx_lost = rx_lost2;
                        //     ac->usb_tx_dropped = tx_dropped;
        
                        //     if (log) {
                        //         fprintf(stdout, "CAN->USB rx lost=%u USB->CAN tx dropped=%u irqf=%u desync=%u\n", rx_lost2, tx_dropped, irq_queue_full, desync);
                        //     }
                        // }
                    } break;
                    case SC_MM_DATA_TYPE_CAN_RX: {
                        auto* rx_slot = &rx.hdr->elements[index].rx;
                        auto timestamp = track_device_time(rx_slot->timestamp_us);
        
                        result = sc_create_can_message(true, timestamp, rx_slot->can_id, rx_slot->flags, rx_slot->dlc, rx_slot->data);
                        i = used;
                    } break;
                    case SC_MM_DATA_TYPE_CAN_TX: {
                        if (receive_own_messages) {
                            auto* tx_slot = &rx.hdr->elements[index].tx;
                            
                            if (!tx_slot->echo && !(tx_slot->flags & SC_CAN_FRAME_FLAG_DRP)) {
                                auto timestamp = track_device_time(tx_slot->timestamp_us);

                                result = sc_create_can_message(false, timestamp, tx_slot->can_id, tx_slot->flags, tx_slot->dlc, tx_slot->data);
                                i = used;
                            }
                        }
                    } break;
                    case SC_MM_DATA_TYPE_CAN_ERROR: {
                        // auto* error = &sc->rx.hdr->elements[index].error;
        
                        // if (SC_CAN_ERROR_NONE != error->error) {
                        //     fprintf(
                        //         stdout, "CAN ERROR %s %s ",
                        //         (error->flags & SC_CAN_ERROR_FLAG_RXTX_TX) ? "tx" : "rx",
                        //         (error->flags & SC_CAN_ERROR_FLAG_NMDT_DT) ? "data" : "arbitration");
                        //     switch (error->error) {
                        //     case SC_CAN_ERROR_STUFF:
                        //         fprintf(stdout, "stuff ");
                        //         break;
                        //     case SC_CAN_ERROR_FORM:
                        //         fprintf(stdout, "form ");
                        //         break;
                        //     case SC_CAN_ERROR_ACK:
                        //         fprintf(stdout, "ack ");
                        //         break;
                        //     case SC_CAN_ERROR_BIT1:
                        //         fprintf(stdout, "bit1 ");
                        //         break;
                        //     case SC_CAN_ERROR_BIT0:
                        //         fprintf(stdout, "bit0 ");
                        //         break;
                        //     case SC_CAN_ERROR_CRC:
                        //         fprintf(stdout, "crc ");
                        //         break;
                        //     default:
                        //         fprintf(stdout, "<unknown> ");
                        //         break;
                        //     }
                        //     fprintf(stdout, "error\n");
                        // }
                    } break;
                    case SC_MM_DATA_TYPE_LOG_DATA: {
                        // if (!ac->candump) {
                        //     auto* log_data = &sc->rx.hdr->elements[index].log_data;
        
                        //     fprintf(stderr, "%s: %s", log_data->src == SC_LOG_DATA_SRC_DLL ? "DLL" : "SRV", log_data->data);
                        // }
                    } break;
                    default: {
                        // fprintf(stderr, "WARN: unhandled msg id=%02x\n", hdr->type);
                    } break;
                    }
                }

                rx.hdr->get_index = gi;

                if (result) {
                    return result;
                }
            } else {
                DWORD wait_result = WAIT_OBJECT_0;

                if (INFINITE == timeout_winapi) {
                    ResetEvent(rx.event);
                    wait_result = WaitForSingleObject(rx.event, INFINITE);
                } else {
                    uint64_t const now = mono_millis();
        
                    uint64_t const elapsed = now - start;
        
                    if (elapsed >= timeout_winapi) {
                        break;
                    } else {
                        ResetEvent(rx.event);
                        wait_result = WaitForSingleObject(rx.event, timeout_winapi - static_cast<DWORD>(elapsed));
                    }
                }

                if (WAIT_OBJECT_0 != wait_result) {
                    auto e = GetLastError();
                    SetCanInitializationError("WaitForSingleObject failed: %lu\n", e);            
                }
            }
        }

        Py_INCREF(rx_no_msg_result.get());
        
        return rx_no_msg_result.get();
    }

    PyObject* get_state() const
    {
        PyObject* result = nullptr;

        switch (bus_status) {
        case SC_CAN_STATUS_ERROR_PASSIVE:
            result = can_bus_state_passive.get();
            break;
        case SC_CAN_STATUS_BUS_OFF:
            result = can_bus_state_error.get();
            break;
        default:
            result = can_bus_state_active.get();
            break;
        }
    
        return Py_NewRef(result);
    }

    PyObject* get_channel_info() const
    {
        return Py_NewRef(channel_info.get());
    }
};

struct py_sc_shared {
    sc_shared sc;
};

void sc_shared_dealloc(PyObject*self)
{
    py_sc_shared* sc = (py_sc_shared *)(((uint8_t*)self) + can_bus_abc_size);

    sc->~py_sc_shared();

    Py_TYPE(self)->tp_free((PyObject *) self);
}

PyObject* sc_shared_shutdown(PyObject* self, PyObject* /* args = nullptr */)
{
    // call base class shutdown()
    PyPtr func(PyObject_GetAttrString((PyObject*)Py_TYPE(self)->tp_base, "shutdown"));
    PyPtr result(PyObject_CallFunctionObjArgs(func.get(), self, nullptr));

    // cleanup
    py_sc_shared* sc = (py_sc_shared*)(((uint8_t*)self) + can_bus_abc_size);
    sc->sc.stop();

    Py_RETURN_NONE;
}

int sc_shared_init(PyObject*self, PyObject *args, PyObject *kwargs)
{
    py_sc_shared* sc = new ((py_sc_shared *)(((uint8_t*)self) + can_bus_abc_size)) py_sc_shared();   
    sc_config config;

    if (!sc_parse_args(args, kwargs, config)) {
        return -1;
    }

    if (!sc->sc.init(kwargs, config)) {
        return -1;
    }

    // set filters on base class
    {
        PyObject* args[] = { self, config.filters };
        PyPtr set_filters(PyObject_GetAttrString((PyObject*)Py_TYPE(self)->tp_base, "set_filters"));
        PyPtr ret(PyObject_Vectorcall(set_filters.get(), args, _countof(args), nullptr));
    }

    if (config.fdf)
    {
        PyPtr ev(PyObject_GetAttrString(can_bus_can_protocol_type.get(), "CAN_FD"));
        PyObject_SetAttrString(self, "_can_protocol", ev.get());
    }

    {
        PyPtr desc(sc->sc.get_channel_info());
        PyObject_SetAttrString(self, "channel_info", desc.get());
    }

    return 0;
}

PyObject *sc_shared_send(PyObject*self, PyObject *args, PyObject *kwargs)
{
    py_sc_shared* sc = (py_sc_shared *)(((uint8_t*)self) + can_bus_abc_size);
    PyObject* msg = nullptr;
    PyObject* timeout = nullptr;
    DWORD timeout_winapi = 0;

    char const * const kwlist[] = {
        "msg",
        "timeout",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "OO|OO",
        (char**)kwlist,
        &msg,
        &timeout)) {
        return nullptr;
    }

    if (!PyObject_IsInstance(msg, can_message_type.get())) {
        PyErr_Format(PyExc_ValueError, "send: first argument must be an instance of can.Message");
        return nullptr;
    }

    if (!sc_to_timeout(timeout, &timeout_winapi)) {
        return nullptr;
    }

    return sc->sc.send(msg, timeout_winapi);
}

PyObject *sc_shared__recv_internal(PyObject*self, PyObject *args, PyObject *kwargs)
{
    py_sc_shared* sc = (py_sc_shared *)(((uint8_t*)self) + can_bus_abc_size);
    PyObject* timeout = nullptr;
    DWORD timeout_winapi = 0;

    char const * const kwlist[] = {
        "timeout",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "O|O",
        (char**)kwlist,
        &timeout)) {
        return nullptr;
    }

    if (!sc_to_timeout(timeout, &timeout_winapi)) {
        return nullptr;
    }

    return sc->sc.recv(timeout_winapi);
}


PyObject *sc_shared_state_get(PyObject* self, void*)
{
    py_sc_shared* sc = (py_sc_shared *)(((uint8_t*)self) + can_bus_abc_size);
    
    return sc->sc.get_state();
}

PyMethodDef sc_shared_methods[] = {
    {"send", (PyCFunction) sc_shared_send, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("transmit CAN frame")},
    {"_recv_internal", (PyCFunction) sc_shared__recv_internal, METH_VARARGS | METH_KEYWORDS, nullptr},
    {"shutdown", (PyCFunction) sc_shared_shutdown, METH_NOARGS, PyDoc_STR("shutdown CAN bus")},
    {nullptr},
};

PyGetSetDef sc_shared_getset[] = {
    {"state", sc_shared_state_get, nullptr, PyDoc_STR("Return the current state of the hardware"), nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot sc_shared_type_spec_slots[] = {
    { Py_tp_doc, (void*)
        "SuperCAN device shared CAN bus access\n" 
        "\n"
        "For parameter description, see :class: supercan.Bus"
        "\n"
    },
    { Py_tp_init, (void*)&sc_shared_init },
    { Py_tp_dealloc, (void*)&sc_shared_dealloc },
    { Py_tp_methods, sc_shared_methods },
    { Py_tp_getset, sc_shared_getset },
    {0, nullptr} // sentinel
};

PyType_Spec sc_shared_type_spec = {
    .name = "supercan.Shared",
    .basicsize = -(int)sizeof(py_sc_shared),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE,
    .slots = sc_shared_type_spec_slots,
};


struct sc_bus
{
    sc_base* impl;

    ~sc_bus()
    {
        delete impl;
    }

    sc_bus()
    {
        impl = nullptr;
    }

    bool init(PyObject* kwargs)
    {
        bool shared = false;
        // detect default value for 'shared'
        {
            bool com_initialized = false;
            SuperCAN::ISuperCAN2Ptr sc;
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

            switch (hr) {
            case S_OK:
                com_initialized = true;    
                break;
            case S_FALSE:
                break;
            case RPC_E_CHANGED_MODE:
                break;
            default:
                SetCanInitializationError("failed to initialze COM (hr=%lx)\n", hr);
                return false;
            }

            hr = sc.CreateInstance(__uuidof(SuperCAN::CSuperCAN));

            shared = SUCCEEDED(hr);

            // release
            sc = nullptr;

            // cleanup COM
            if (com_initialized) {
                CoUninitialize();
            }
        }


        PyObject* py_shared = PyDict_GetItemString(kwargs, "shared"); // borrowed        

        if (!get_bool_arg(py_shared, "shared", &shared)) {
            return false;
        }    

        if (shared) {
            impl = new sc_shared();
        } else {
            impl = new sc_exclusive();
        }

        return true;
    }
};

void sc_bus_dealloc(PyObject*self)
{
    sc_bus* sc = (sc_bus *)(((uint8_t*)self) + can_bus_abc_size);

    sc->~sc_bus();

    Py_TYPE(self)->tp_free((PyObject *) self);
}

PyObject* sc_bus_shutdown(PyObject* self, PyObject* /* args = nullptr */)
{
    // call base class shutdown()
    PyPtr func(PyObject_GetAttrString((PyObject*)Py_TYPE(self)->tp_base, "shutdown"));
    PyPtr result(PyObject_CallFunctionObjArgs(func.get(), self, nullptr));

    // cleanup
    sc_bus* sc = (sc_bus*)(((uint8_t*)self) + can_bus_abc_size);
    sc->impl->stop();

    Py_RETURN_NONE;
}

int sc_bus_init(PyObject*self, PyObject *args, PyObject *kwargs)
{
    sc_bus* sc = new ((sc_bus *)(((uint8_t*)self) + can_bus_abc_size)) sc_bus();   
    sc_config config;

    if (!sc_parse_args(args, kwargs, config)) {
        return -1;
    }

    if (!sc->init(kwargs)) {
        return -1;
    }

    if (!sc->impl->init(kwargs, config)) {
        return -1;
    }

    // set filters on base class
    {
        PyObject* args[] = { self, config.filters };
        PyPtr set_filters(PyObject_GetAttrString((PyObject*)Py_TYPE(self)->tp_base, "set_filters"));
        PyPtr ret(PyObject_Vectorcall(set_filters.get(), args, _countof(args), nullptr));
    }

    if (config.fdf)
    {
        PyPtr ev(PyObject_GetAttrString(can_bus_can_protocol_type.get(), "CAN_FD"));
        PyObject_SetAttrString(self, "_can_protocol", ev.get());
    }

    {
        PyPtr desc(sc->impl->get_channel_info());
        PyObject_SetAttrString(self, "channel_info", desc.get());
    }

    return 0;
}

PyObject *sc_bus_send(PyObject*self, PyObject *args, PyObject *kwargs)
{
    sc_bus* sc = (sc_bus *)(((uint8_t*)self) + can_bus_abc_size);
    PyObject* msg = nullptr;
    PyObject* timeout = nullptr;
    DWORD timeout_winapi = 0;

    char const * const kwlist[] = {
        "msg",
        "timeout",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "OO|OO",
        (char**)kwlist,
        &msg,
        &timeout)) {
        return nullptr;
    }

    if (!PyObject_IsInstance(msg, can_message_type.get())) {
        PyErr_Format(PyExc_ValueError, "send: first argument must be an instance of can.Message");
        return nullptr;
    }

    if (!sc_to_timeout(timeout, &timeout_winapi)) {
        return nullptr;
    }

    return sc->impl->send(msg, timeout_winapi);
}

PyObject *sc_bus__recv_internal(PyObject*self, PyObject *args, PyObject *kwargs)
{
    sc_bus* sc = (sc_bus *)(((uint8_t*)self) + can_bus_abc_size);
    PyObject* timeout = nullptr;
    DWORD timeout_winapi = 0;

    char const * const kwlist[] = {
        "timeout",
        nullptr,
    };

    if (!PyArg_ParseTupleAndKeywords(
        args,
        kwargs,
        "O|O",
        (char**)kwlist,
        &timeout)) {
        return nullptr;
    }

    if (!sc_to_timeout(timeout, &timeout_winapi)) {
        return nullptr;
    }

    return sc->impl->recv(timeout_winapi);
}


PyObject *sc_bus_state_get(PyObject* self, void*)
{
    sc_bus* sc = (sc_bus *)(((uint8_t*)self) + can_bus_abc_size);
    
    return sc->impl->get_state();
}

PyMethodDef sc_bus_methods[] = {
    {"send", (PyCFunction) sc_bus_send, METH_VARARGS | METH_KEYWORDS, PyDoc_STR("transmit CAN frame")},
    {"_recv_internal", (PyCFunction) sc_bus__recv_internal, METH_VARARGS | METH_KEYWORDS, nullptr},
    {"shutdown", (PyCFunction) sc_bus_shutdown, METH_NOARGS, PyDoc_STR("shutdown CAN bus")},
    {nullptr},
};

PyGetSetDef sc_bus_getset[] = {
    {"state", sc_bus_state_get, nullptr, PyDoc_STR("Return the current state of the hardware"), nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

PyType_Slot sc_bus_type_spec_slots[] = {
    { Py_tp_doc, (void*)
        "SuperCAN device CAN bus access\n"
        "\n"
        "SuperCAN device channels can be accessed in _exclusive_ or _shared_ mode.\n"
        "Exclusive mode gives your application exclusive access over the chosen CAN bus channel. No one else can send or receive on that channel.\n"
        "On the other hand shared access allows multiple applications to concurrently make use of a CAN bus channel. This can be useful for channel monitoring or to simulate multiple bus participants. Only one application can initialize the bus, however. That application needs to request `init_access`.\n"
        "\n"
        "Common keyword parameters:\n"
        ":param int channel: Channel index, optional, defaults to 0\n" 
        ":param filters: CAN frame filters, optional, defaults to None\n"
        ":param str serial: Device serial number hex string, defaults to the empty string which means the first device found is used\n"
        ":param int bitrate: Arbitration bitrate\n"
        ":param int data_bitrate: CAN-FD data bitrate\n"
        ":param bool fd: Request CAN-FD mode\n"
        ":param float sample_point: Sample point during arbitration, defaults to 0.8\n"
        ":param float data_sample_point: Sample point during CAN-FD data phase, defaults to 0.7\n"
        ":param int sjw_abr: SJW during arbitration, optional\n"
        ":param int sjw_dbr: SJW during CAN-FD data phase, optional\n"
        ":param bool receive_own_messages: Echo back messages transmitted by this instance on receive path, defaults to False\n"
        "\n"
        "Shared keyword parameters:\n"
        ":param bool init_access: Shared bus instances only, request to initialize the bus, else assume the bus is already initialized.\n"
        "\n"
        "Bus keyword parameters:\n"
        ":param shared: Request shared (True) or exclusive (False) bus instance. If this keyword parameter is omitted, a shared instance will be created if 1. COM is available and 2. the COM server has been registered. Otherwise an exclusive instance will be created.\n"
    },
    { Py_tp_init, (void*)&sc_bus_init },
    { Py_tp_dealloc, (void*)&sc_bus_dealloc },
    { Py_tp_methods, sc_bus_methods },
    { Py_tp_getset, sc_bus_getset },
    {0, nullptr} // sentinel
};

PyType_Spec sc_bus_type_spec = {
    .name = "supercan.Bus",
    .basicsize = -(int)sizeof(sc_bus),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE,
    .slots = sc_bus_type_spec_slots,
};

PyModuleDef module_definition = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "supercan",
    .m_doc = "SuperCAN extension module for Python CAN (python-can).",
    .m_size = -1,
};

} // anon

PyMODINIT_FUNC
PyInit_supercan(void)
{
    if (!QueryPerformanceFrequency((LARGE_INTEGER*)&s_perf_counter_freq)) {
        return nullptr;
    }

    // convert timestamps to UNIX epoch
    FILETIME tf_epoch_start;
    SYSTEMTIME st_epoch_start;
    st_epoch_start.wYear = 1970;
    st_epoch_start.wMonth = 1;
    st_epoch_start.wDay = 1;
    st_epoch_start.wDayOfWeek = 4;
    st_epoch_start.wHour = 0;
    st_epoch_start.wMilliseconds = 0;
    st_epoch_start.wMinute = 0;
    st_epoch_start.wSecond = 0;
    SystemTimeToFileTime(&st_epoch_start, &tf_epoch_start);

    system_time_to_epoch_offset_100ns = tf_epoch_start.dwHighDateTime;
    system_time_to_epoch_offset_100ns <<= 32;
    system_time_to_epoch_offset_100ns |= tf_epoch_start.dwLowDateTime;

    // returns new ref
    PyPtr can_module(PyImport_ImportModule("can"));
    if (!can_module) {
        return nullptr;
    }

    PyPtr can_bus_module(PyImport_ImportModule("can.bus"));
    if (!can_bus_module) {
        return nullptr;
    }

    PyPtr can_exceptions_module(PyImport_ImportModule("can.exceptions"));
    if (!can_exceptions_module) {
        return nullptr;
    }

    // returns new ref
    PyPtr bus_abc_type(PyObject_GetAttrString(can_module.get(), "BusABC"));
    if (!bus_abc_type) {
        return nullptr;
    }

    if (!PyType_Check(bus_abc_type.get())) {
        return nullptr;
    }

    can_bus_abc_size = ((PyTypeObject*)bus_abc_type.get())->tp_basicsize;
    sc_exclusive_type_spec.basicsize = (int)(can_bus_abc_size + sizeof(sc_exclusive));

    can_bit_timing_type.reset(PyObject_GetAttrString(can_module.get(), "BitTiming"));
    if (!can_bit_timing_type) {
        return nullptr;
    }

    can_bit_timing_fd_type.reset(PyObject_GetAttrString(can_module.get(), "BitTimingFd"));
    if (!can_bit_timing_fd_type) {
        return nullptr;
    }

    can_message_type.reset(PyObject_GetAttrString(can_module.get(), "Message"));
    if (!can_message_type) {
        return nullptr;
    }

    can_bus_state_type.reset(PyObject_GetAttrString(can_module.get(), "BusState"));
    if (!can_bus_state_type) {
        return nullptr;
    }

    can_bus_can_protocol_type.reset(PyObject_GetAttrString(can_bus_module.get(), "CanProtocol"));
    if (!can_bus_can_protocol_type) {
        return nullptr;
    }

    can_exceptions_can_initialization_error_type.reset(PyObject_GetAttrString(can_exceptions_module.get(), "CanInitializationError"));
    if (!can_exceptions_can_initialization_error_type) {
        return nullptr;
    }

    can_exceptions_can_operation_error_type.reset(PyObject_GetAttrString(can_exceptions_module.get(), "CanOperationError"));
    if (!can_exceptions_can_operation_error_type) {
        return nullptr;
    }

    PyPtr copy_module(PyImport_ImportModule("copy"));
    if (!copy_module) {
        return nullptr;
    }

    copy_deepcopy.reset(PyObject_GetAttrString(copy_module.get(), "deepcopy"));
    if (!copy_deepcopy) {
        return nullptr;
    }

    can_bus_state_active.reset(PyObject_GetAttrString(can_bus_state_type.get(), "ACTIVE"));
    if (!can_bus_state_active) {
        return nullptr;
    }
    can_bus_state_error.reset(PyObject_GetAttrString(can_bus_state_type.get(), "ERROR"));
    if (!can_bus_state_error) {
        return nullptr;
    }
    can_bus_state_passive.reset(PyObject_GetAttrString(can_bus_state_type.get(), "PASSIVE"));
    if (!can_bus_state_passive) {
        return nullptr;
    }

    rx_no_msg_result.reset(PyTuple_New(2));
    PyTuple_SET_ITEM(rx_no_msg_result.get(), 0, Py_NewRef(Py_None));
    PyTuple_SET_ITEM(rx_no_msg_result.get(), 1, Py_NewRef(Py_False));

    PyPtr module(PyModule_Create(&module_definition));

    if (!module) {
        return nullptr;
    }

    sc_exclusive_type.reset(PyType_FromSpecWithBases(&sc_exclusive_type_spec, bus_abc_type.get()));

    if (!sc_exclusive_type) {
        return nullptr;
    }

    if (PyType_Ready((PyTypeObject*)sc_exclusive_type.get()) < 0) {
        return nullptr;
    }

    if (PyModule_AddObjectRef(module.get(), "Exclusive", sc_exclusive_type.get()) < 0) {
        return nullptr;
    }

    sc_shared_type.reset(PyType_FromSpecWithBases(&sc_shared_type_spec, bus_abc_type.get()));

    if (!sc_shared_type) {
        return nullptr;
    }

    if (PyType_Ready((PyTypeObject*)sc_shared_type.get()) < 0) {
        return nullptr;
    }

    if (PyModule_AddObjectRef(module.get(), "Shared", sc_shared_type.get()) < 0) {
        return nullptr;
    }

    sc_bus_type.reset(PyType_FromSpecWithBases(&sc_bus_type_spec, bus_abc_type.get()));

    if (!sc_bus_type) {
        return nullptr;
    }

    if (PyType_Ready((PyTypeObject*)sc_bus_type.get()) < 0) {
        return nullptr;
    }

    if (PyModule_AddObjectRef(module.get(), "Bus", sc_bus_type.get()) < 0) {
        return nullptr;
    }

    return module.release();
}