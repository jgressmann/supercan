#include "pch.h"

#include <stdio.h>

#define MAX_PENDING_READS 16


static inline uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return map[dlc & 0xf];
}

int main(int argc, char** argv)
{
    int error = SC_DLL_ERROR_NONE;
    uint32_t count;
    sc_dev_t* dev = NULL;
    
    PUCHAR cmd_tx_buffer = NULL;
    PUCHAR cmd_rx_buffer = NULL;
    HANDLE cmd_tx_event = NULL;
    HANDLE cmd_rx_event = NULL;
    PUCHAR msg0_rx_buffers = NULL;
    PUCHAR msg0_tx_buffer = NULL;
    OVERLAPPED cmd_tx_ov, cmd_rx_ov;
    HANDLE msg0_read_events[MAX_PENDING_READS] = { 0 };
    OVERLAPPED msg0_read_ovs[MAX_PENDING_READS] = { 0 };
    HANDLE msg0_tx_event = NULL;
    OVERLAPPED msg0_tx_ov;
    struct sc_msg_dev_info dev_info;
    DWORD transferred;

    sc_init();

    cmd_tx_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cmd_tx_event) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }

    cmd_rx_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cmd_rx_event) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }

    cmd_tx_ov.hEvent = cmd_tx_event;
    cmd_rx_ov.hEvent = cmd_rx_event;

    for (size_t i = 0; i < _countof(msg0_read_events); ++i) {
        HANDLE h = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!h) {
            error = -1;
            goto Exit;
        }

        msg0_read_events[i] = h;
        msg0_read_ovs[i].hEvent = h;
    }

    msg0_tx_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!msg0_tx_event) {
        error = -1;
        goto Exit;
    }

    msg0_tx_ov.hEvent = msg0_tx_event;

    error = sc_dev_scan();
    if (error) {
        fprintf(stderr, "sc_dev_scan failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    
    error = sc_dev_count(&count);
    if (error) {
        fprintf(stderr, "sc_dev_count failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    if (!count) {
        fprintf(stdout, "no " SC_NAME " devices found\n");
        goto Exit;
    }

    fprintf(stdout, "%u " SC_NAME " devices found\n", count);

    error = sc_dev_open(0, &dev);
    if (error) {
        fprintf(stderr, "sc_dev_open failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    fprintf(stdout, "cmd pipe %#02x\n", dev->cmd_pipe);
    for (uint8_t i = 0; i < dev->msg_pipe_count; ++i) {
        fprintf(stdout, "ch%u pipe %#02x\n", i, dev->msg_pipe_ptr[i]);
    }

    msg0_rx_buffers = malloc(dev->msg_buffer_size * _countof(msg0_read_events));
    if (!msg0_rx_buffers) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }


    // submit all reads tokens for ch0
    for (size_t i = 0; i < _countof(msg0_read_events); ++i) {
        error = sc_dev_read(dev, dev->msg_pipe_ptr[0] | 0x80, msg0_rx_buffers + i * dev->msg_buffer_size, dev->msg_buffer_size, &msg0_read_ovs[i]);
        if (SC_DLL_ERROR_IO_PENDING != error) {
            goto Exit;
        }
    }



    msg0_tx_buffer = malloc(dev->msg_buffer_size);
    if (!msg0_tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }

    

    cmd_tx_buffer = calloc(dev->cmd_buffer_size, 1);
    if (!cmd_tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }
    cmd_rx_buffer = calloc(dev->cmd_buffer_size, 1);
    if (!cmd_rx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }

    // submit in token
    //
    error = sc_dev_read(dev, dev->cmd_pipe | 0x80, cmd_rx_buffer, dev->cmd_buffer_size, &cmd_rx_ov);
    if (SC_DLL_ERROR_IO_PENDING != error) {
        error = -1;
        goto Exit;
    }

    PUCHAR cmd_tx_ptr = cmd_tx_buffer;
    // get device info
    struct sc_msg_header* info = (struct sc_msg_header* )cmd_tx_ptr;
    info->id = SC_MSG_DEVICE_INFO;
    info->len = SC_HEADER_LEN;
    cmd_tx_ptr += info->len;

    ULONG bytes = (ULONG)(cmd_tx_ptr - cmd_tx_buffer);
    
    error = sc_dev_write(dev, dev->cmd_pipe, cmd_tx_buffer, bytes, &cmd_tx_ov);
    if (SC_DLL_ERROR_IO_PENDING != error) {
        error = -1;
        goto Exit;
    }
    
    error = sc_dev_result(dev, &transferred, &cmd_tx_ov, -1);
    if (error) {
        fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    if (transferred != bytes) {
        fprintf(stderr, "sc_dev_write incomplete\n");
        error = -1;
        goto Exit;
    }

    error = sc_dev_result(dev, &transferred, &cmd_rx_ov, -1);
    if (error) {
        fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    if (transferred < sizeof(dev_info)) {
        fprintf(stderr, "sc_dev_write incomplete\n");
        error = -1;
        goto Exit;
    }

    memcpy(&dev_info, cmd_rx_buffer, sizeof(dev_info));
    dev_info.can_clk_hz = dev->dev_to_host32(dev_info.can_clk_hz);
    dev_info.nmbt_brp_max = dev->dev_to_host16(dev_info.nmbt_brp_max);
    dev_info.nmbt_tq_max = dev->dev_to_host16(dev_info.nmbt_tq_max);
    dev_info.nmbt_tseg1_max = dev->dev_to_host16(dev_info.nmbt_tseg1_max);

    fprintf(stdout, "device has %u CAN channels\n", dev_info.channels);
    
    cmd_tx_ptr = cmd_tx_buffer;
    // set bus off ch0
    struct sc_msg_config* bus_off = (struct sc_msg_config*)cmd_tx_ptr;
    bus_off->id = SC_MSG_BUS;
    bus_off->len = sizeof(*bus_off);
    cmd_tx_ptr += bus_off->len;

    bus_off->channel = 0;
    bus_off->args[0] = 0;

    // set bittiming for ch0
    struct sc_msg_bittiming* bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
    bt->id = SC_MSG_BITTIMING;
    bt->len = sizeof(*bt);
    cmd_tx_ptr += bt->len;

    //REG_CAN0_NBTP = CAN_NBTP_NBRP(2) | CAN_NBTP_NTSEG1(62) | CAN_NBTP_NTSEG2(15) | CAN_NBTP_NSJW(15); /* 500kBit @ 120 / 3 = 40MHz, 80% */
    //REG_CAN0_DBTP = CAN_DBTP_DBRP(2) | CAN_DBTP_DTSEG1(12) | CAN_DBTP_DTSEG2(5) | CAN_DBTP_DSJW(5); /* 2MBit @ 120 / 3 = 40MHz, 70% */
    

    bt->channel = 0;
    bt->nmbt_brp = dev->dev_to_host16(3);
    bt->nmbt_sjw = 16;
    bt->nmbt_tseg1 = dev->dev_to_host16(63);
    bt->nmbt_tseg2 = 16;
    bt->dtbt_brp = 3;
    bt->dtbt_sjw = 6;
    bt->dtbt_tseg1 = 13;
    bt->dtbt_tseg2 = 6;

    // set mode for ch0
    struct sc_msg_config* mode = (struct sc_msg_config*)cmd_tx_ptr;
    mode->id = SC_MSG_MODE;
    mode->len = sizeof(*mode);
    cmd_tx_ptr += mode->len;

    mode->channel = 0;
    mode->args[0] = SC_MODE_FLAG_BRS | SC_MODE_FLAG_FD | SC_MODE_FLAG_RX | SC_MODE_FLAG_TX | SC_MODE_FLAG_AUTO_RE | SC_MODE_FLAG_EH;

    // set bus on ch0
    struct sc_msg_config* bus_on = (struct sc_msg_config*)cmd_tx_ptr;
    bus_on->id = SC_MSG_BUS;
    bus_on->len = sizeof(*bus_on);
    cmd_tx_ptr += bus_on->len;

    bus_on->channel = 0;
    bus_on->args[0] = 1;

    bytes = (ULONG)(cmd_tx_ptr - cmd_tx_buffer);

    error = sc_dev_write(dev, dev->cmd_pipe, cmd_tx_buffer, bytes, &cmd_tx_ov);
    if (SC_DLL_ERROR_IO_PENDING != error) {
        error = -1;
        goto Exit;
    }

    error = sc_dev_result(dev, &transferred, &cmd_tx_ov, -1);
    if (error) {
        fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    if (transferred != bytes) {
        fprintf(stderr, "sc_dev_write incomplete\n");
        error = -1;
        goto Exit;
    }

    while (1) {
        DWORD result = WaitForMultipleObjects(_countof(msg0_read_events), msg0_read_events, FALSE, 100);
        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + _countof(msg0_read_events)) {
            size_t index = result - WAIT_OBJECT_0;
            error = sc_dev_result(dev, &transferred, &msg0_read_ovs[index], 0);
            if (error) {
                fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }

            // process buffer
            PUCHAR in_beg = msg0_rx_buffers + dev->msg_buffer_size * index;
            PUCHAR in_end = in_beg + transferred;
            PUCHAR in_ptr = in_beg;
            while (in_ptr + SC_HEADER_LEN <= in_end) {
                struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;
                if (in_ptr + msg->len > in_end) {
                    fprintf(stderr, "malformed msg\n");
                    break;
                }

                if (!msg->len) {
                    break;
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
                        break;
                    }

                    uint32_t timestamp_us = dev->dev_to_host32(status->timestamp_us);
                    uint16_t rx_lost = dev->dev_to_host16(status->rx_lost);
                    uint16_t tx_dropped = dev->dev_to_host16(status->tx_dropped);

                    fprintf(stdout, "ch%u rxl=%u txd=%u\n", status->channel, rx_lost, tx_dropped);
                    if (status->flags & SC_STATUS_FLAG_BUS_OFF) {
                        fprintf(stdout, "ch%u bus off\n", status->channel);
                    }
                    else if (status->flags & SC_STATUS_FLAG_ERROR_WARNING) {
                        fprintf(stdout, "ch%u error warning\n", status->channel);
                    }
                    else if (status->flags & SC_STATUS_FLAG_ERROR_PASSIVE) {
                        fprintf(stdout, "ch%u error passive\n", status->channel);
                    }

                    if (status->flags & SC_STATUS_FLAG_RX_FULL) {
                        fprintf(stdout, "ch%u rx queue full\n", status->channel);
                    }

                    if (status->flags & SC_STATUS_FLAG_TX_FULL) {
                        fprintf(stdout, "ch%u tx queue full\n", status->channel);
                    }

                    if (status->flags & SC_STATUS_FLAG_TXR_DESYNC) {
                        fprintf(stdout, "ch%u txr desync\n", status->channel);
                    }
                } break;
                case SC_MSG_CAN_RX: {
                    struct sc_msg_can_rx const *rx = (struct sc_msg_can_rx const*)msg;
                    uint32_t can_id = dev->dev_to_host32(rx->can_id);
                    uint8_t len = dlc_to_len(rx->dlc);
                    bytes = sizeof(*rx);
                    if (!(rx->flags & SC_CAN_FLAG_RTR)) {
                        bytes += len;
                    }
                    
                    if (msg->len < len) {
                        fprintf(stderr, "malformed sc_msg_can_rx\n");
                        break;
                    }

                    fprintf(stdout, "ch%u %x [%u] ", rx->channel, can_id, len);
                    if (rx->flags & SC_CAN_FLAG_RTR) {
                        fprintf(stdout, "RTR");
                    } else {
                        for (uint8_t i = 0; i < len; ++i) {
                            fprintf(stdout, "%02x ", rx->data[i]);
                        }
                    }
                    fputc('\n', stdout);
                } break;
                default:
                    break;
                }
            }

            // re-queue in token
            error = sc_dev_read(dev, dev->msg_pipe_ptr[0] | 0x80, in_beg, dev->msg_buffer_size, &msg0_read_ovs[index]);
            if (error && error != SC_DLL_ERROR_IO_PENDING) {
                fprintf(stderr, "sc_dev_read failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }

            goto send;
        }
        else if (WAIT_TIMEOUT == result) {
            struct sc_msg_can_tx* tx;
send:
            tx = (struct sc_msg_can_tx*)msg0_tx_buffer;
            tx->id = SC_MSG_CAN_TX;
            tx->len = sizeof(*tx) + 4;
            tx->can_id = 0x42;
            tx->channel = 0;
            tx->dlc = 4;
            tx->flags = 0;
            tx->track_id = 0;
            tx->data[0] = 0xde;
            tx->data[1] = 0xad;
            tx->data[2] = 0xbe;
            tx->data[3] = 0xef;

            error = sc_dev_write(dev, dev->msg_pipe_ptr[0], (uint8_t*)tx, tx->len, &msg0_tx_ov);
            if (error && error != SC_DLL_ERROR_IO_PENDING) {
                fprintf(stderr, "sc_dev_write failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }

            error = sc_dev_result(dev, &transferred, &cmd_tx_ov, -1);
            if (error) {
                fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }
        } else {
            fprintf(stderr, "WaitForMultipleObjects failed: %s (%d)\n", sc_strerror(error), error);
            break;
        }
    }


Exit:
    if (dev) {
        sc_dev_close(dev);
    }

    free(msg0_rx_buffers);
    free(msg0_tx_buffer);
    free(cmd_tx_buffer);
    free(cmd_rx_buffer);

    if (msg0_tx_event) {
        CloseHandle(msg0_tx_event);
    }

    if (cmd_tx_event) {
        CloseHandle(cmd_tx_event);
    }

    if (cmd_rx_event) {
        CloseHandle(cmd_rx_event);
    }

    for (size_t i = 0; i < _countof(msg0_read_events); ++i) {
        if (msg0_read_events[i]) {
            CloseHandle(msg0_read_events[i]);
        }
    }

    sc_uninit();
    return error;
}