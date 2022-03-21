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


#include "app.h"

#include <atlbase.h>
#include <atlcom.h>

#import "supercan_srv.tlb" raw_interfaces_only
#include "supercan_srv.h"

#ifdef min
#undef min
#endif

#include <algorithm>

using namespace SuperCAN;

namespace
{
int map_hr_to_error(HRESULT hr) 
{
    if (SUCCEEDED(hr)) {
        return SC_DLL_ERROR_NONE;
    }

    auto facility = HRESULT_FACILITY(hr);
    if (facility == SC_FACILITY) {
        return HRESULT_CODE(hr);
    }

    switch (hr) {
    case E_OUTOFMEMORY:
        return SC_DLL_ERROR_OUT_OF_MEM;
    case E_ACCESSDENIED:
        return SC_DLL_ERROR_ACCESS_DENIED;
    default:
        return SC_DLL_ERROR_UNKNOWN;
    }
}

int map_error_code_to_error(DWORD error_code)
{
    switch (error_code) {
    case 0:
        return SC_DLL_ERROR_NONE;
    case ERROR_OUTOFMEMORY:
        return SC_DLL_ERROR_OUT_OF_MEM;
    default:
        return SC_DLL_ERROR_UNKNOWN;
    }
}

class ComUninitializer
{
public:
    ~ComUninitializer()
    {
        CoUninitialize();
    }

    ComUninitializer() = default;
};

struct sc_mm_data {
    sc_can_mm_header* hdr;
    HANDLE file;
    HANDLE event;
    uint32_t elements;
};

struct com_dev_ctx {
    ISuperCANDevice* dev;
    SuperCANDeviceData dev_data;
    sc_mm_data rx;
    sc_mm_data tx;
    uint32_t track_id;
};


void process_rx(app_ctx* ac)
{
    com_dev_ctx* com_ctx = static_cast<com_dev_ctx*>(ac->priv);

    auto rx_lost = InterlockedExchange(&com_ctx->rx.hdr->lost_rx, 0);
    if (rx_lost) {
        fprintf(stderr, "ERROR: %lu rx messages lost\n", rx_lost);
    }

    auto tx_lost = InterlockedExchange(&com_ctx->rx.hdr->lost_tx, 0);
    if (tx_lost) {
        fprintf(stderr, "ERROR: %lu tx messages lost\n", tx_lost);
    }

    auto status_lost = InterlockedExchange(&com_ctx->rx.hdr->lost_status, 0);
    if (status_lost) {
        fprintf(stderr, "ERROR: %lu status messages lost\n", status_lost);
    }

    auto error_lost = InterlockedExchange(&com_ctx->rx.hdr->lost_error, 0);
    if (error_lost) {
        fprintf(stderr, "ERROR: ERROR %lu error messages lost\n", error_lost);
    }


    auto gi = com_ctx->rx.hdr->get_index;
    auto pi = com_ctx->rx.hdr->put_index;
    auto used = pi - gi;
    if (used > com_ctx->rx.elements) {
        fprintf(stderr, "ERROR: RX mm data mismatch (pi=%lu gi=%u used=%u elements=%u)\n",
            static_cast<unsigned long>(pi),
            static_cast<unsigned long>(gi),
            static_cast<unsigned long>(used),
            static_cast<unsigned long>(com_ctx->rx.elements));
    }
    else if (used) {
        for (uint32_t i = 0; i < used; ++i, ++gi) {
            auto index = gi % com_ctx->rx.elements;
            auto* hdr = &com_ctx->rx.hdr->elements[index].hdr;

            switch (hdr->type) {
            case SC_CAN_DATA_TYPE_STATUS: {
                auto* status = &com_ctx->rx.hdr->elements[index].status;
                
                if (!ac->candump && (ac->log_flags & LOG_FLAG_CAN_STATE)) {
                    bool log = false;
                    if (ac->log_on_change) {
                        log = ac->can_rx_errors_last != status->rx_errors ||
                            ac->can_tx_errors_last != status->tx_errors ||
                            ac->can_bus_state_last != status->bus_status;
                    }
                    else {
                        log = true;
                    }

                    ac->can_rx_errors_last = status->rx_errors;
                    ac->can_tx_errors_last = status->tx_errors;
                    ac->can_bus_state_last = status->bus_status;

                    if (log) {
                        fprintf(stdout, "CAN rx errors=%u tx errors=%u bus=", status->rx_errors, status->tx_errors);
                        switch (status->bus_status) {
                        case SC_CAN_STATUS_ERROR_ACTIVE:
                            fprintf(stdout, "error_active");
                            break;
                        case SC_CAN_STATUS_ERROR_WARNING:
                            fprintf(stdout, "error_warning");
                            break;
                        case SC_CAN_STATUS_ERROR_PASSIVE:
                            fprintf(stdout, "error_passive");
                            break;
                        case SC_CAN_STATUS_BUS_OFF:
                            fprintf(stdout, "off");
                            break;
                        default:
                            fprintf(stdout, "unknown");
                            break;
                        }
                        fprintf(stdout, "\n");
                    }
                }

                if (!ac->candump && (ac->log_flags & LOG_FLAG_USB_STATE)) {
                    bool log = false;
                    bool irq_queue_full = status->flags & SC_CAN_STATUS_FLAG_IRQ_QUEUE_FULL;
                    bool desync = status->flags & SC_CAN_STATUS_FLAG_TXR_DESYNC;
                    auto rx_lost2 = status->rx_lost;
                    auto tx_dropped = status->tx_dropped;

                    if (ac->log_on_change) {
                        log = ac->usb_rx_lost != rx_lost2 ||
                            ac->usb_tx_dropped != tx_dropped ||
                            irq_queue_full || desync;
                    }
                    else {
                        log = true;
                    }

                    ac->usb_rx_lost = rx_lost2;
                    ac->usb_tx_dropped = tx_dropped;

                    if (log) {
                        fprintf(stdout, "CAN->USB rx lost=%u USB->CAN tx dropped=%u irqf=%u desync=%u\n", rx_lost2, tx_dropped, irq_queue_full, desync);
                    }
                }
            } break;
            case SC_CAN_DATA_TYPE_RX: {
                auto* rx = &com_ctx->rx.hdr->elements[index].rx;

                if (ac->candump) {
                    log_candump(ac, stdout, rx->timestamp_us, rx->can_id, rx->flags, rx->dlc, rx->data);
                }
                else {
                    if (ac->log_flags & LOG_FLAG_RX_DT) {
                        int64_t dt_us = 0;
                        if (ac->rx_last_ts) {
                            dt_us = rx->timestamp_us - ac->rx_last_ts;
                            if (dt_us < 0) {
                                fprintf(stderr, "WARN negative rx msg dt [us]: %lld\n", dt_us);
                            }
                        }

                        ac->rx_last_ts = rx->timestamp_us;

                        fprintf(stdout, "rx delta %.3f [ms]\n", dt_us * 1e-3f);
                    }

                    if (ac->log_flags & LOG_FLAG_RX_MSG) {
                        fprintf(stdout, "RX ");
                        log_msg(ac, rx->can_id, rx->flags, rx->dlc, rx->data);
                    }
                }
            } break;
            case SC_CAN_DATA_TYPE_TX: {
                auto* tx = &com_ctx->rx.hdr->elements[index].tx;

                if (ac->candump) {
                    log_candump(ac, stdout, tx->timestamp_us, tx->can_id, tx->flags, tx->dlc, tx->data);
                }
                else {
                    if (!tx->echo && (ac->log_flags & LOG_FLAG_TXR)) {
                        if (tx->flags & SC_CAN_FRAME_FLAG_DRP) {
                            fprintf(stdout, "TXR %#08x was dropped @ %016llx\n", tx->track_id, tx->timestamp_us);
                        }
                        else {
                            fprintf(stdout, "TXR %#08x was sent @ %016llx\n", tx->track_id, tx->timestamp_us);
                        }
                    }

                    if ((ac->log_flags & LOG_FLAG_TX_MSG)) {
                        fprintf(stdout, "TX ");
                        log_msg(ac, tx->can_id, tx->flags, tx->dlc, tx->data);
                    }
                }
            } break;
            case SC_CAN_DATA_TYPE_ERROR: {
                auto* error = &com_ctx->rx.hdr->elements[index].error;

                if (SC_CAN_ERROR_NONE != error->error) {
                    fprintf(
                        stdout, "CAN ERROR %s %s ",
                        (error->flags & SC_CAN_ERROR_FLAG_RXTX_TX) ? "tx" : "rx",
                        (error->flags & SC_CAN_ERROR_FLAG_NMDT_DT) ? "data" : "arbitration");
                    switch (error->error) {
                    case SC_CAN_ERROR_STUFF:
                        fprintf(stdout, "stuff ");
                        break;
                    case SC_CAN_ERROR_FORM:
                        fprintf(stdout, "form ");
                        break;
                    case SC_CAN_ERROR_ACK:
                        fprintf(stdout, "ack ");
                        break;
                    case SC_CAN_ERROR_BIT1:
                        fprintf(stdout, "bit1 ");
                        break;
                    case SC_CAN_ERROR_BIT0:
                        fprintf(stdout, "bit0 ");
                        break;
                    case SC_CAN_ERROR_CRC:
                        fprintf(stdout, "crc ");
                        break;
                    default:
                        fprintf(stdout, "<unknown> ");
                        break;
                    }
                    fprintf(stdout, "error\n");
                }
            } break;
            default: {
                fprintf(stderr, "WARN: unhandled msg id=%02x\n", hdr->type);
            } break;
            }
        }

        com_ctx->rx.hdr->get_index = gi;
    }
}

static bool tx(app_ctx* ac, struct tx_job* job)
{
    com_dev_ctx* com_ctx = static_cast<com_dev_ctx*>(ac->priv);
    auto gi = com_ctx->tx.hdr->get_index;
    auto pi = com_ctx->tx.hdr->put_index;
    auto used = pi - gi;

   if (used > com_ctx->tx.elements) {
        fprintf(stderr, "ERROR: TX mm data mismatch (pi=%lu gi=%u used=%u elements=%u)\n",
            static_cast<unsigned long>(pi),
            static_cast<unsigned long>(gi),
            static_cast<unsigned long>(used),
            static_cast<unsigned long>(com_ctx->tx.elements));
    }
    else if (used == com_ctx->tx.elements) {
        // full
    }
    else {
        auto index = pi % com_ctx->tx.elements;
        auto* tx = &com_ctx->tx.hdr->elements[index].tx;

        tx->type = SC_CAN_DATA_TYPE_TX;
        tx->can_id = job->can_id;
        tx->flags = job->flags;
        tx->track_id = com_ctx->track_id++;
        tx->dlc = job->dlc;
        if (!(job->flags & SC_CAN_FRAME_FLAG_RTR)) {
            memcpy(tx->data, job->data, dlc_to_len(job->dlc));
        }

        com_ctx->tx.hdr->put_index = pi + 1;

        return true;
    }

   return false;
}

int run(app_ctx* ac)
{
    com_dev_ctx* com_ctx = static_cast<com_dev_ctx*>(ac->priv);
    ISuperCANDevice* dev = com_ctx->dev;
    struct can_bit_timing_settings nominal_settings, data_settings;
    struct can_bit_timing_hw_contraints nominal_hw_constraints, data_hw_constraints;
    int error = SC_DLL_ERROR_NONE;
    char config_access = 0;
    HRESULT hr = S_OK;

    memset(&nominal_settings, 0, sizeof(nominal_settings));
    memset(&data_settings, 0, sizeof(data_settings));
    memset(&nominal_hw_constraints, 0, sizeof(nominal_hw_constraints));
    memset(&data_hw_constraints, 0, sizeof(data_hw_constraints));

    if (ac->config) {
        unsigned long access_timeout_ms = 0;
        hr = dev->AcquireConfigurationAccess(&config_access, &access_timeout_ms);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed acquire config access (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        if (!config_access) {
            fprintf(stderr, "ERROR: failed to get configuration access\n");
            return SC_DLL_ERROR_ACCESS_DENIED;
        }
    
    
        SuperCANBitTimingParams params;
        memset(&params, 0, sizeof(params));

        hr = dev->SetBus(0);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to go off bus (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        nominal_hw_constraints.brp_min = com_ctx->dev_data.nm_min.brp;
        nominal_hw_constraints.brp_max = com_ctx->dev_data.nm_max.brp;
        nominal_hw_constraints.brp_step = 1;
        nominal_hw_constraints.clock_hz = com_ctx->dev_data.can_clock_hz;
        nominal_hw_constraints.sjw_max = com_ctx->dev_data.nm_max.sjw;
        nominal_hw_constraints.tseg1_min = com_ctx->dev_data.nm_min.tseg1;
        nominal_hw_constraints.tseg1_max = com_ctx->dev_data.nm_max.tseg1;
        nominal_hw_constraints.tseg2_min = com_ctx->dev_data.nm_min.tseg2;
        nominal_hw_constraints.tseg2_max = com_ctx->dev_data.nm_max.tseg2;

        data_hw_constraints.brp_min = com_ctx->dev_data.dt_min.brp;
        data_hw_constraints.brp_max = com_ctx->dev_data.dt_max.brp;
        data_hw_constraints.brp_step = 1;
        data_hw_constraints.clock_hz = com_ctx->dev_data.can_clock_hz;
        data_hw_constraints.sjw_max = com_ctx->dev_data.dt_max.sjw;
        data_hw_constraints.tseg1_min = com_ctx->dev_data.dt_min.tseg1;
        data_hw_constraints.tseg1_max = com_ctx->dev_data.dt_max.tseg1;
        data_hw_constraints.tseg2_min = com_ctx->dev_data.dt_min.tseg2;
        data_hw_constraints.tseg2_max = com_ctx->dev_data.dt_max.tseg2;

        error = cia_fd_cbt_real(
            &nominal_hw_constraints,
            &data_hw_constraints,
            &ac->nominal_user_constraints,
            &ac->data_user_constraints,
            &nominal_settings,
            &data_settings);
        switch (error) {
        case CAN_BTRE_NO_SOLUTION:
            fprintf(stderr, "ERROR: The chosen nominal/data bitrate/sjw cannot be configured on the device.\n");
            return SC_DLL_ERROR_INVALID_PARAM;
        case CAN_BTRE_NONE:
            break;
        default:
            fprintf(stderr, "Ooops.\n");
            return SC_DLL_ERROR_UNKNOWN;
        }

        params.brp = static_cast<unsigned short>(nominal_settings.brp);
        params.sjw = static_cast<unsigned char>(nominal_settings.sjw);
        params.tseg1 = static_cast<unsigned short>(nominal_settings.tseg1);
        params.tseg2 = static_cast<unsigned char>(nominal_settings.tseg2);
        hr = dev->SetNominalBitTiming(params);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to set nominal bit timing (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        unsigned long features = SC_FEATURE_FLAG_TXR;
        if (ac->fdf) {
            features |= SC_FEATURE_FLAG_FDF;

            params.brp = static_cast<unsigned short>(data_settings.brp);
            params.sjw = static_cast<unsigned char>(data_settings.sjw);
            params.tseg1 = static_cast<unsigned short>(data_settings.tseg1);
            params.tseg2 = static_cast<unsigned char>(data_settings.tseg2);
            hr = dev->SetDataBitTiming(params);
            if (FAILED(hr)) {
                fprintf(stderr, "ERROR: failed to set data bit timing (hr=%lx)\n", hr);
                return map_hr_to_error(hr);
            }
        }

        hr = dev->SetFeatureFlags(features);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to set features (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }


        hr = dev->SetBus(1);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to go on bus (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }
    }

    HANDLE handles[] = {
        ac->shutdown_event,
        com_ctx->rx.event
    };

    DWORD timeout_ms = 0;
    bool was_full = false;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (1) {

        auto r = WaitForMultipleObjects(static_cast<DWORD>(_countof(handles)), handles, FALSE, timeout_ms);
        if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + _countof(handles)) {
            auto index = r - WAIT_OBJECT_0;
            auto handle = handles[index];
            if (handle == ac->shutdown_event) {
                break;
            }
        }
        else if (WAIT_TIMEOUT == r) {
            // pass
        }
        else {
            auto e = GetLastError();
            fprintf(stderr, "ERROR: wait failed (error_code=%lu)\n", e);
            return map_error_code_to_error(e);
        }

        if (com_ctx->rx.hdr->error) {
            return com_ctx->rx.hdr->error;
        }

        process_rx(ac);

        if (ac->tx_job_count) {
            timeout_ms = 0xffffffff;
            ULONGLONG now = GetTickCount64();
            bool queued = false;

            for (size_t i = 0; i < ac->tx_job_count; ++i) {
                struct tx_job* job = &ac->tx_jobs[i];

                if ((job->interval_ms >= 0 &&
                    (0 == job->last_tx_ts_ms || now - job->last_tx_ts_ms >= (unsigned)job->interval_ms)) ||
                    (job->interval_ms < 0 && job->count > 0)) {
                    auto const count = job->count;

                    job->last_tx_ts_ms = now;

                    while (job->count > 0) {
                        if (tx(ac, job)) {
                            --job->count;
                            queued = true;
                            was_full = false;
                        }
                        else {
                            if (!was_full) {
                                was_full = true;
                                fprintf(stderr, "ERROR: TX ring full\n");
                                break;
                            }
                        }
                    }

                    if (job->interval_ms >= 0) {
                        // reload count
                        job->count = count;

                        if (job->interval_ms >= 0 && (DWORD)job->interval_ms < timeout_ms) {
                            timeout_ms = job->interval_ms;
                        }
                    }
                }
            }

            if (queued) {
                SetEvent(com_ctx->tx.event);
            }
        }
        else {
            timeout_ms = INFINITE;
        }
    }

    if (ac->config) {
        unsigned long access_timeout_ms = 0;
        hr = dev->AcquireConfigurationAccess(&config_access, &access_timeout_ms);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed acquire config access (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        if (!config_access) {
            fprintf(stderr, "ERROR: failed to get configuration access\n");
            return SC_DLL_ERROR_ACCESS_DENIED;
        }

        hr = dev->SetBus(0);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to go off bus (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }
    }

    return error;
}

void unmap(sc_mm_data* x)
{
    if (x->hdr) {
        UnmapViewOfFile(x->hdr);
        x->hdr = nullptr;
    }

    if (x->event) {
        CloseHandle(x->event);
        x->event = nullptr;
    }

    if (x->file) {
        CloseHandle(x->file);
        x->file = nullptr;
    }
}

int map(sc_mm_data* x, SuperCANRingBufferMapping const* y)
{
    int error = SC_DLL_ERROR_NONE;

    x->elements = y->Elements;

    x->file = OpenFileMappingW(
            FILE_MAP_READ | FILE_MAP_WRITE,   // read/write access
            FALSE,                 // do not inherit the name
            y->MemoryName);

    if (!x->file) {
        auto e = GetLastError();
        fprintf(stderr, "ERROR: OpenFileMappingW failed: %lu\n", e);
        error = map_error_code_to_error(e);
        goto error_exit;
    }

    x->event = OpenEventW(
        EVENT_ALL_ACCESS,
        FALSE,
        y->EventName);

    if (!x->event) {
        auto e = GetLastError();
        fprintf(stderr, "ERROR: OpenEventW failed: %lu\n", e);
        error = map_error_code_to_error(e);
        goto error_exit;
    }
    
    x->hdr = static_cast<decltype(x->hdr)>(MapViewOfFile(x->file, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, y->Bytes));
    if (!x->hdr) {
        auto e = GetLastError();
        fprintf(stderr, "ERROR: MapViewOfFile failed: %lu\n", e);
        error = map_error_code_to_error(e);
        goto error_exit;
    }

success_exit:
    return error;

error_exit:
    unmap(x);
    goto success_exit;
}

} // anon

extern "C" int run_shared(struct app_ctx* ac)
{
    com_dev_ctx com_ctx;
    SuperCANRingBufferMapping rx_mm_data, tx_mm_data;
    
    int error = SC_DLL_ERROR_NONE;
    wchar_t serial_str[1 + sizeof(com_ctx.dev_data.sn_bytes) * 2] = { 0 };

    memset(&com_ctx, 0, sizeof(com_ctx));
    memset(&rx_mm_data, 0, sizeof(rx_mm_data));
    memset(&tx_mm_data, 0, sizeof(tx_mm_data));
    
    ac->priv = &com_ctx;

    try {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to initialze COM (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        ComUninitializer com_cleanup;

        ISuperCANPtr sc;
        hr = sc.CreateInstance(__uuidof(CSuperCAN));
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to created instance of SuperCAN (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        unsigned long dev_count = 0;
        hr = sc->DeviceScan(&dev_count);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: device scan failed (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        if (!dev_count) {
            fprintf(stdout, "no " SC_NAME " devices found\n");
            return SC_DLL_ERROR_NONE;
        }
        
        if (!ac->candump) {
            fprintf(stdout, "%u " SC_NAME " devices found\n", (unsigned)dev_count);
        }

        if (ac->device_index >= dev_count) {
            fprintf(stdout, "Requested device index %u out of range\n", ac->device_index);
            return SC_DLL_ERROR_INVALID_PARAM;
        }

        ISuperCANDevicePtr device_ptr;
        hr = sc->DeviceOpen(ac->device_index, (ISuperCANDevice**)&device_ptr);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to open device index=0 (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        // release SuperCAN to verify the device keeps the COM server loaded
        sc.Release();


        com_ctx.dev = device_ptr;

        hr = device_ptr->GetDeviceData(&com_ctx.dev_data);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to get device data (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        if (!ac->candump) {
            fprintf(stdout, "device features perm=%#04x conf=%#04x\n", com_ctx.dev_data.feat_perm, com_ctx.dev_data.feat_conf);

            for (size_t i = 0; i < std::min((size_t)com_ctx.dev_data.sn_length, _countof(serial_str) - 1); ++i) {
                _snwprintf_s(&serial_str[i * 2], 3, _TRUNCATE, L"%02x", com_ctx.dev_data.sn_bytes[i]);
            }

            fwprintf(stdout, L"device identifies as %ls, serial no %ls, firmware version %u.% u.% u\n",
                com_ctx.dev_data.name, serial_str, com_ctx.dev_data.fw_ver_major, com_ctx.dev_data.fw_ver_minor, com_ctx.dev_data.fw_ver_patch);
        }

        SysFreeString(com_ctx.dev_data.name);
        com_ctx.dev_data.name = nullptr;

        hr = device_ptr->GetRingBufferMappings(&rx_mm_data, &tx_mm_data);
        if (FAILED(hr)) {
            fprintf(stderr, "ERROR: failed to get RX/TX ring buffer configuration (hr=%lx)\n", hr);
            return map_hr_to_error(hr);
        }

        error = map(&com_ctx.rx, &rx_mm_data);
        if (error) {
            goto cleanup;
        }

        error = map(&com_ctx.tx, &tx_mm_data);
        if (error) {
            goto cleanup;
        }

        error = run(ac);
        
cleanup:
        unmap(&com_ctx.rx);
        unmap(&com_ctx.tx);
        SysFreeString(rx_mm_data.MemoryName);
        SysFreeString(rx_mm_data.EventName);
        SysFreeString(tx_mm_data.MemoryName);
        SysFreeString(tx_mm_data.EventName);
    }
    catch (...) {
        error = SC_DLL_ERROR_UNKNOWN;
    }

    return error;
}
