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


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>






static void usage(FILE* stream)
{
    fprintf(stream, "SuperCAN demo app (c) 2020 Jean Gressmann <jean@0x42.de>\n");
    fprintf(stream, "supercan_app [options]\n");
    fprintf(stream, "\n");
    fprintf(stream, "-h, --help, /?     print this help\n");
    fprintf(stream, "-i, --index        device index, defaults to first device (index=0)\n");
    fprintf(stream, "--nbitrate INT     nominal bitrate\n");
    fprintf(stream, "--dbitrate INT     data bitrate (CAN-FD)\n");
    fprintf(stream, "--nswj INT         nominal SJW (defaults to 1)\n");
    fprintf(stream, "--dswj INT         data SJW (defaults to 1)\n");
    fprintf(stream, "--nsp FLOAT        nominal sample point (defaults to CiA setting)\n");
    fprintf(stream, "--dsp FLOAT        data sample point (defaults to CiA setting)\n");
    fprintf(stream, "--fd BOOL          enable or disable CAN-FD format (defaults to off)\n");
    fprintf(stream, "--log ITEM         enables logging of ITEM which is one of\n");
    fprintf(stream, "   NONE:       no logging\n");
    fprintf(stream, "   RX_DT:      log rx message timestamp deltas\n");
    fprintf(stream, "   RX_MSG:     log rx message content\n");
    fprintf(stream, "   CAN_STATE:  log CAN status information\n");
    fprintf(stream, "   USB_STATE:  log USB status information\n");
    fprintf(stream, "   TX_MSG:     log tx message information\n");
    fprintf(stream, "   TXR:        log tx message receipts\n");
    fprintf(stream, "   ALL:        log everything\n");
    fprintf(stream, "--log-change BOOL  enable or disable on-change logging for CAN, USB state\n");
    fprintf(stream, "--tx K1=V1,K2...   transmit message\n");
    fprintf(stream, "   keys are:\n");
    fprintf(stream, "       id      CAN ID (hex)\n");
    fprintf(stream, "       len     frame length (bytes)\n");
    fprintf(stream, "       dlc     frame length (dlc)\n");
    fprintf(stream, "       data    payload (hex)\n");
    fprintf(stream, "       int     interval (millis)\n");
    fprintf(stream, "       fd      FD frame format (bool)\n");
    fprintf(stream, "       brs     FD bit rate switching (bool)\n");
    fprintf(stream, "       esi     FD error state indicator (bool)\n");
    fprintf(stream, "       ext     extended format (29 bit identifier) (bool)\n");
    fprintf(stream, "       count   number of messages to generate (default 1)\n");
    fprintf(stream, "--shared BOOL  share device access (enabled by default)\n");
    fprintf(stream, "--single       request exclusive device access\n");
    fprintf(stream, "--config BOOL  request config level access (defaults to on)\n");
    fprintf(stream, "--candump      log received messages in candump log format (overrides other log flags)\n");
    fprintf(stream, "--debug-log-level  LEVEL   debug log level, default OFF (-1)\n");
}




static inline uint8_t hex_to_nibble(char c)
{
    if ('0' <= c && '9' >= c) {
        return c - '0';
    }

    if ('a' <= c && 'f' >= c) {
        return c - 'a' + 10;
    }

    if ('A' <= c && 'F' >= c) {
        return c - 'A' + 10;
    }

    return 0;
}

static inline uint8_t hex_to_byte(char hi, char lo)
{
    return (hex_to_nibble(hi) << 4) | hex_to_nibble(lo);
}

static void parse_tx_job(struct tx_job* job, char* str)
{
    memset(job, 0, sizeof(*job));
    job->interval_ms = -1;
    job->count = 1;

    char* kvs_ctx = NULL;
    for (char* kvs = strtok_s(str, ",", &kvs_ctx); kvs;
        kvs = strtok_s(NULL, ",", &kvs_ctx)) {

        char* eq = strchr(kvs, '=');
        if (!eq) {
            fprintf(stderr, "ERROR ignoring invalid key/value pair '%s'\n", kvs);
            continue;
        }

        char* key = kvs;
        char* value = eq + 1;
        *eq = 0;



        if (0 == _stricmp(key, "id") ||
            0 == _stricmp(key, "can_id") || 
            0 == _stricmp(key, "canid")) {
            job->can_id = strtoul(value, NULL, 16) & 0x1fffffff;
            if (job->can_id & ~0x7ff) {
                job->flags |= SC_CAN_FRAME_FLAG_EXT;
            }
        }
        else if (0 == _stricmp(key, "ext") ||
                 0 == _stricmp(key, "xtd")) {
            bool flag = !is_false(value);
            if (flag) {
                job->flags |= SC_CAN_FRAME_FLAG_EXT;
            }
            else {
                job->flags &= ~SC_CAN_FRAME_FLAG_EXT;
            }
        }
        else if (0 == _stricmp(key, "fd") || 0 == _stricmp(key, "fdf")) {
            bool flag = !is_false(value);
            if (flag) {
                job->flags |= SC_CAN_FRAME_FLAG_FDF;
            }
            else {
                job->flags &= ~(SC_CAN_FRAME_FLAG_FDF | SC_CAN_FRAME_FLAG_BRS | SC_CAN_FRAME_FLAG_ESI);
            }
        }
        else if (0 == _stricmp(key, "len")) {
            uint8_t len = (uint8_t)strtoul(value, NULL, 10);
            job->dlc = len_to_dlc(len);
        }
        else if (0 == _stricmp(key, "dlc")) {
            unsigned dlc = strtoul(value, NULL, 10);
            job->dlc = dlc & 0xf;
        }
        else if (0 == _stricmp(key, "data")) {
            size_t len = strlen(value);
            if (len > 2 * _countof(job->data)) {
                len = 2 * _countof(job->data);
            }

            for (size_t i = 0, j = 0; i < len; i += 2, ++j) {
                job->data[j] = hex_to_byte(value[i], value[i + 1]);
            }
        }
        else if (0 == strcmp(key, "int")) {
            job->interval_ms = strtol(value, NULL, 10);
        }
        else if (0 == strcmp(key, "count")) {
            job->count = strtol(value, NULL, 10);
        }
        else if (0 == _stricmp(key, "brs")) {
            bool flag = !is_false(value);
            if (flag) {
                job->flags |= SC_CAN_FRAME_FLAG_BRS;
            }
            else {
                job->flags &= ~SC_CAN_FRAME_FLAG_BRS;
            }
        }
        else if (0 == _stricmp(key, "esi")) {
            bool flag = !is_false(value);
            if (flag) {
                job->flags |= SC_CAN_FRAME_FLAG_ESI;
            }
            else {
                job->flags &= ~SC_CAN_FRAME_FLAG_ESI;
            }
        }
    }
}

HANDLE s_Shutdown = NULL;


static void SignalHandler(int sig)
{
    (void)sig;

    fprintf(stderr, "\nreceived SIG %d, signalling shutdown\n", sig);

    if (s_Shutdown) {
        SetEvent(s_Shutdown);
    }
}

extern int run_single(struct app_ctx* ac);
extern int run_shared(struct app_ctx* ac);

int main(int argc, char** argv)
{
    struct app_ctx ac;
   
    int error = SC_DLL_ERROR_NONE;
    bool shared = true;
    

    memset(&ac, 0, sizeof(ac));

    ac.nominal_user_constraints.bitrate = 500000;
    ac.nominal_user_constraints.min_tqs = 0;
    ac.nominal_user_constraints.sample_point = 0.875f;
    ac.nominal_user_constraints.sjw = 1;
    ac.data_user_constraints.bitrate = 500000;
    ac.data_user_constraints.min_tqs = 0;
    ac.data_user_constraints.sample_point = 0.875f;
    ac.data_user_constraints.sjw = 1;
    ac.config = true;
    ac.can_rx_errors_last = -1;
    ac.can_tx_errors_last = -1;
    ac.can_bus_state_last = -1;
    ac.candump = false;
    ac.debug_log_level = SC_DLL_LOG_LEVEL_OFF;

    cia_fd_cbt_init_default_real(&ac.nominal_user_constraints, &ac.data_user_constraints);

    for (int i = 1; i < argc; ) {
        if (0 == strcmp("-h", argv[i]) ||
            0 == strcmp("--help", argv[i]) ||
            0 == strcmp("/?", argv[i])) {
            usage(stdout);
            goto Exit;
        }
        else if (0 == strcmp("--fd", argv[i])) {
            if (i + 1 < argc) {
                ac.fdf = !is_false(argv[i + 1]);
                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--log", argv[i])) {
            if (i + 1 < argc) {
                const char* arg = argv[i + 1];
                if (0 == _stricmp("RX_DT", arg)) {
                    ac.log_flags |= LOG_FLAG_RX_DT;
                }
                else if (0 == _stricmp("RX_MSG", arg)) {
                    ac.log_flags |= LOG_FLAG_RX_MSG;
                }
                else if (0 == _stricmp("BUS_STATE", arg) ||
                    0 == _stricmp("CAN_STATE", arg)) {
                    ac.log_flags |= LOG_FLAG_CAN_STATE;
                } 
                else if (0 == _stricmp("USB_STATE", arg)) {
                    ac.log_flags |= LOG_FLAG_USB_STATE;
                }
                else if (0 == _stricmp("TX_MSG", arg)) {
                    ac.log_flags |= LOG_FLAG_TX_MSG;
                }
                else if (0 == _stricmp("TXR", arg)) {
                    ac.log_flags |= LOG_FLAG_TXR;
                }
                else if (0 == _stricmp("NONE", arg)) {
                    ac.log_flags = 0u;
                }
                else if (0 == _stricmp("NONE", arg)) {
                    ac.log_flags = 0u;
                }
                else if (0 == _stricmp("ALL", arg)) {
                    ac.log_flags = ~0u;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("-i", argv[i]) || 0 == strcmp("--index", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.device_index = (uint32_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--nbitrate", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.nominal_user_constraints.bitrate = (uint32_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (ac.nominal_user_constraints.bitrate == 0 || ac.nominal_user_constraints.bitrate > UINT32_C(1000000)) {
                    fprintf(stderr, "ERROR invalid nominal bitrate %lu\n", (unsigned long)ac.nominal_user_constraints.bitrate);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--dbitrate", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.data_user_constraints.bitrate = (uint32_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (ac.data_user_constraints.bitrate == 0 || ac.data_user_constraints.bitrate > UINT32_C(8000000)) {
                    fprintf(stderr, "ERROR invalid data bitrate %lu\n", (unsigned long)ac.data_user_constraints.bitrate);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--nsjw", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.nominal_user_constraints.sjw = (uint8_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (ac.nominal_user_constraints.sjw == 0) {
                    fprintf(stderr, "ERROR invalid nominal sjw %u\n", (unsigned)ac.nominal_user_constraints.sjw);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--dsjw", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.data_user_constraints.sjw = (uint8_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (ac.data_user_constraints.sjw == 0) {
                    fprintf(stderr, "ERROR invalid data sjw %u\n", (unsigned)ac.data_user_constraints.sjw);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--nsp", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.nominal_user_constraints.sample_point = strtof(argv[i + 1], &end);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to float\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (ac.nominal_user_constraints.sample_point <= 0 || ac.nominal_user_constraints.sample_point >= 1) {
                    fprintf(stderr, "ERROR invalid nominal sample point %f\n", ac.nominal_user_constraints.sample_point);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a float argument in range (0-1)\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--dsp", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.data_user_constraints.sample_point = strtof(argv[i + 1], &end);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to float\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (ac.data_user_constraints.sample_point <= 0 || ac.data_user_constraints.sample_point >= 1) {
                    fprintf(stderr, "ERROR invalid data sample point %f\n", ac.nominal_user_constraints.sample_point);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a float argument in range (0-1)\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--tx", argv[i])) {
            if (i + 1 < argc) {
                if (ac.tx_job_count == _countof(ac.tx_jobs)) {
                    fprintf(stderr, "ERROR Only %zu tx jobs available\n", _countof(ac.tx_jobs));
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                struct tx_job* job = &ac.tx_jobs[ac.tx_job_count++];
                parse_tx_job(job, argv[i + 1]);

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a key/value string argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--shared", argv[i])) {
            if (i + 1 < argc) {
                shared = !is_false(argv[i + 1]);
                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        } 

        else if (0 == strcmp("--single", argv[i])) {
            shared = false;
            ++i;
        }
        else if (0 == strcmp("--candump", argv[i])) {
            ac.candump = true;
            ++i;
        }
        else if (0 == strcmp("--config", argv[i])) {
            if (i + 1 < argc) {
                ac.config = !is_false(argv[i + 1]);
                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--log-change", argv[i])) {
            if (i + 1 < argc) {
                ac.log_on_change = !is_false(argv[i + 1]);
                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--debug-log-level", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                ac.debug_log_level = (int)strtol(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to int\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects an integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else {
            ++i;
        }
    }

    s_Shutdown = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s_Shutdown) {
        error = -1;
        fprintf(stderr, "failed to create shutdown event: (%d)\n", GetLastError());
        goto Exit;
    }

    ac.shutdown_event = s_Shutdown;

    signal(SIGINT, &SignalHandler);
    signal(SIGTERM, &SignalHandler);

    if (shared) {
        error = run_shared(&ac);
    }
    else {
        error = run_single(&ac);
    }

Exit:
    if (s_Shutdown) {
        CloseHandle(s_Shutdown);
    }

    return error;
}
