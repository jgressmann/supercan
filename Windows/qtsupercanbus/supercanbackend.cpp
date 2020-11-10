#include <cstring>
#include <cstdio>
#include <supercan_misc.h>
#include "supercanbackend.h"


#include <QtCore/qloggingcategory.h>

#ifndef SC_STATIC
#include "supercan_symbols_p.h"
#endif


using namespace std;

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(QT_CANBUS_PLUGINS_SUPERCAN)

#ifdef SC_STATIC
#define SC_CALL(name) sc_##name
#else
Q_GLOBAL_STATIC(QLibrary, supercanLibrary)
#define SC_CALL(name) dl_sc_##name
#endif

namespace
{
constexpr int SC_DEVICE_TIMEOUT_MS = 1000;

inline uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return map[dlc & 0xf];
}

inline uint8_t len_to_dlc(uint8_t len)
{
    if (len <= 8) {
        return len;
    }

    switch (len) {
    case 12: return 9;
    case 16: return 10;
    case 20: return 11;
    case 24: return 12;
    case 32: return 13;
    case 48: return 14;
    case 64: return 15;
    }

    return 0;
}

inline uint16_t dev_to_host16(sc_dev_t* dev, uint16_t value)
{
    return dev->dev_to_host16(value);
}

inline uint32_t dev_to_host32(sc_dev_t* dev, uint32_t value)
{
    return dev->dev_to_host32(value);
}

} // anon




////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/// ////////////////////////////////////////////////////////////////////////////




bool SuperCanBackend::canCreate(QString *errorReason)
{
#ifdef SC_STATIC
    Q_UNUSED(errorReason);
    static bool initialized = false;
    if (!Q_UNLIKELY(!initialized)) {
        std::atexit(SC_CALL(uninit));
        SC_CALL(init)();
        initialized = true;
    }
    return true;
#else
    static bool symbolsResolved = false;
    if (Q_UNLIKELY(!symbolsResolved)) {
        symbolsResolved = resolveSymbols(supercanLibrary());
        if (Q_UNLIKELY(!symbolsResolved)) {
            *errorReason = supercanLibrary()->errorString();
            return false;
        }

        std::atexit(SC_CALL(uninit));
        SC_CALL(init)();
    }

    return true;
#endif
}

QList<QCanBusDeviceInfo> SuperCanBackend::interfaces()
{
    QList<QCanBusDeviceInfo> result;
    QString serialString, nameString;
    sc_dev_t* device = nullptr;
    sc_cmd_ctx_t cmd_ctx;
    char name_buffer[sizeof(sc_msg_dev_info::name_bytes)+1];
    char serial_buffer[sizeof(sc_msg_dev_info::sn_bytes)*2+1];
    uint32_t count;
    int error = SC_DLL_ERROR_NONE;


    SC_CALL(dev_scan)();
    error = SC_CALL(dev_count)(&count);
    if (error) {
        goto error_exit;
    }

    for (uint32_t i = 0; i < count; ++i) {
        sc_msg_req *req_dev_info = nullptr;
        sc_msg_dev_info *dev_info = nullptr;
        uint16_t reply_bytes = 0;
        unsigned len = 0;

        error = sc_dev_open(i, &device);
        if (error) {
            continue;
        }

        error = sc_cmd_ctx_init(&cmd_ctx, device);
        if (error) {
            goto cleanup;
        }

        req_dev_info = (sc_msg_req *)cmd_ctx.tx_buffer;
        memset(req_dev_info, 0, sizeof(*req_dev_info));
        req_dev_info->id = SC_MSG_DEVICE_INFO;
        req_dev_info->len = sizeof(*req_dev_info);

        error = sc_cmd_ctx_run(&cmd_ctx, req_dev_info->len, &reply_bytes, SC_DEVICE_TIMEOUT_MS);
        if (error) {
            qCWarning(
                        QT_CANBUS_PLUGINS_SUPERCAN,
                        "Failed to get device info from device index %u: %s (%d).",
                        (unsigned)i, SC_CALL(strerror)(error), error);
            goto cleanup;
        }

        if (reply_bytes < sizeof(*dev_info)) {
            qCWarning(
                        QT_CANBUS_PLUGINS_SUPERCAN,
                        "Failed device index %u send invalid reply to SC_MSG_DEVICE_INFO.",
                        (unsigned)i);

            goto cleanup;
        }

        dev_info = (sc_msg_dev_info *)cmd_ctx.rx_buffer;
        dev_info->feat_conf = device->dev_to_host16(dev_info->feat_conf);
        dev_info->feat_perm = device->dev_to_host16(dev_info->feat_perm);

        len = dev_info->sn_len;
        if (len >= sizeof(dev_info->sn_bytes)) {
            qCWarning(
                        QT_CANBUS_PLUGINS_SUPERCAN,
                        "Device index %u send out of bounds serial number length.",
                        (unsigned)i);

            len = sizeof(dev_info->sn_bytes);
        }

        for (size_t i = 0; i < len; ++i) {
            snprintf(&serial_buffer[i * 2], 3, "%02x", dev_info->sn_bytes[i]);
        }

        serial_buffer[len * 2] = 0;

        len = dev_info->name_len;

        if (len >= sizeof(dev_info->name_len)) {
            qCWarning(
                        QT_CANBUS_PLUGINS_SUPERCAN,
                        "Device index %u send out of bounds name length.",
                        (unsigned)i);

            len = sizeof(dev_info->name_len);
        }

        memcpy(name_buffer, dev_info->name_bytes, len);
        name_buffer[len] = 0;


        serialString = QString::fromUtf8(serial_buffer);
        nameString = QString::fromUtf8(name_buffer);

        {
            auto devInfo = createDeviceInfo(
                        nameString,
                        serialString,
                        QString(),
                        0,
                        false,
                        SC_FEATURE_FLAG_FDF == (dev_info->feat_conf | dev_info->feat_perm));

            result.append(std::move(devInfo));
        }

cleanup:
        sc_cmd_ctx_uninit(&cmd_ctx);
        sc_dev_close(device);
        device = nullptr;
    }

default_exit:
    return result;

error_exit:
    qCWarning(
                QT_CANBUS_PLUGINS_SUPERCAN,
                "Failed to get list of devices: %s (%d).",
                SC_CALL(strerror)(error), error);

    goto default_exit;
}

SuperCanBackend::~SuperCanBackend()
{
    qCDebug(QT_CANBUS_PLUGINS_SUPERCAN, "SuperCanBackend %p dtor", this);
    busCleanup();
}

SuperCanBackend::SuperCanBackend(const QString &name, QObject *parent)
    : QCanBusDevice(parent)
{
    qCDebug(QT_CANBUS_PLUGINS_SUPERCAN, "SuperCanBackend %p ctor", this);

    m_Timer.setInterval(16);
    m_Timer.setSingleShot(false);

    m_Fd = false;
    m_IsOnBus = false;
    m_Urbs = -1;
//    memset(&m_Nominal, 0, sizeof(m_Nominal));
//    memset(&m_Data, 0, sizeof(m_Data));

    m_ScDevice = nullptr;
    m_ScCanStream = nullptr;
    m_ScDevIndex = 0;
    m_FeatPerm = 0;
    m_FeatConf = 0;
    m_Initialized = false;
    m_HasFd = false;
    m_HasTxr = false;
    memset(&m_ScCmdCtx, 0, sizeof(m_ScCmdCtx));
    memset(&m_ScCanInfo, 0, sizeof(m_ScCanInfo));
    memset(&m_TimeTracker, 0, sizeof(m_TimeTracker));


    auto device_index = name.toUInt();
    m_ScDevIndex = device_index;

    auto error = sc_dev_open(device_index, &m_ScDevice);
    if (error) {
        setError(SuperCanBackend::tr("Failed to open device %1: %2 (%3)").arg(device_index).arg(SC_CALL(strerror)(error)).arg(error), OperationError);
    } else {
        error = sc_cmd_ctx_init(&m_ScCmdCtx, m_ScDevice);
        if (error) {
            sc_dev_close(m_ScDevice);
            m_ScDevice = nullptr;
            setError(SuperCanBackend::tr("Failed to init command context for device %1: %2 (%3)").arg(device_index).arg(SC_CALL(strerror)(error)).arg(error), OperationError);
        } else {
            //
            sc_msg_req *req = (sc_msg_req *)m_ScCmdCtx.tx_buffer;
            memset(req, 0, sizeof(*req));
            req->id = SC_MSG_DEVICE_INFO;
            req->len = sizeof(*req);

            uint16_t reply_bytes = 0;
            error = sc_cmd_ctx_run(&m_ScCmdCtx, req->len, &reply_bytes, SC_DEVICE_TIMEOUT_MS);
            if (error) {
                setError(SuperCanBackend::tr("Command SC_MSG_DEV_INFO failed for device %1: %2 (%3)").arg(device_index).arg(SC_CALL(strerror)(error)).arg(error), OperationError);
            } else {
                if (reply_bytes < sizeof(m_ScDevInfo)) {
                    setError(SuperCanBackend::tr("Device index %1 send invalid reply to SC_MSG_DEV_INFO.").arg(device_index), OperationError);

                    sc_cmd_ctx_uninit(&m_ScCmdCtx);
                    sc_dev_close(m_ScDevice);
                    m_ScDevice = nullptr;
                } else {
                    sc_msg_dev_info *dev_info = (sc_msg_dev_info *)m_ScCmdCtx.rx_buffer;
                    m_FeatPerm = m_ScDevice->dev_to_host16(dev_info->feat_perm);
                    m_FeatConf = m_ScDevice->dev_to_host16(dev_info->feat_conf);
                    m_HasFd = ((m_FeatPerm | m_FeatConf) & SC_FEATURE_FLAG_FDF) == SC_FEATURE_FLAG_FDF;
                    m_HasTxr = ((m_FeatPerm | m_FeatConf) & SC_FEATURE_FLAG_TXR) == SC_FEATURE_FLAG_TXR;

                    // CAN info
                    req->id = SC_MSG_CAN_INFO;
                    error = sc_cmd_ctx_run(&m_ScCmdCtx, req->len, &reply_bytes, SC_DEVICE_TIMEOUT_MS);
                    if (error) {
                        setError(SuperCanBackend::tr("Command SC_MSG_CAN_INFO failed for device %1: %2 (%3)").arg(device_index).arg(SC_CALL(strerror)(error)).arg(error), OperationError);
                    } else {
                        memcpy(&m_ScCanInfo, m_ScCmdCtx.rx_buffer, sizeof(m_ScCmdCtx));
                        m_ScCanInfo.can_clk_hz = m_ScDevice->dev_to_host32(m_ScCanInfo.can_clk_hz);
                        m_ScCanInfo.nmbt_brp_max = m_ScDevice->dev_to_host16(m_ScCanInfo.nmbt_brp_max);
                        m_ScCanInfo.nmbt_tseg1_max = m_ScDevice->dev_to_host16(m_ScCanInfo.nmbt_tseg1_max);
                        m_ScCanInfo.msg_buffer_size = m_ScDevice->dev_to_host16(m_ScCanInfo.msg_buffer_size);

                        m_Initialized = true;
                    }
                }
            }
        }
    }


#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    m_BusStatus = QCanBusDevice::CanBusStatus::Unknown;
    setCanBusStatusGetter([this] { return m_BusStatus; });
#endif

    resetConfiguration();
}


bool SuperCanBackend::open()
{
    if (Q_UNLIKELY(!deviceValid())) {
        return false;
    }

    if (m_HasTxr) {
        m_TxSlotsAvailable.resize(m_ScCanInfo.tx_fifo_size);
        for (unsigned i = 0; i < m_ScCanInfo.tx_fifo_size; ++i) {
            m_TxSlotsAvailable[i] = i;
        }
    }

    auto error = sc_can_stream_init(m_ScDevice, m_ScCanInfo.msg_buffer_size, this, &SuperCanBackend::on_rx, 0, &m_ScCanStream);
    if (error) {
        setError(SuperCanBackend::tr("Failed to init CAN stream for device %1: %2 (%3)").arg(m_ScDevIndex).arg(SC_CALL(strerror)(error)).arg(error), OperationError);
        return false;
    }

//    qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Private @ %p", d);

    //apply all stored configurations
    const auto keys = configurationKeys();
    for (int key : keys) {
        applyConfigurationParameter(key,  configurationParameter(key));
    }

    error = setBus(true);
    if (error) {
        sc_can_stream_uninit(&m_ScCanStream);
        m_ScCanStream = nullptr;
        setError(SuperCanBackend::tr("Failed to go on bus: %1 (%2).").arg(SC_CALL(strerror)(error)).arg(error), ConnectionError);
        return false;
    }

    setState(QCanBusDevice::ConnectedState);



    return true;
}

void SuperCanBackend::close()
{
    if (Q_UNLIKELY(!deviceValid())) {
        return;
    }

    auto error = setBus(false);
    if (error) {
        setError(SuperCanBackend::tr("Failed to go off bus: %1 (%2).").arg(SC_CALL(strerror)(error)).arg(error), UnknownError);
    } else {
        setState(QCanBusDevice::UnconnectedState);
    }

    sc_can_stream_uninit(&m_ScCanStream);
    m_ScCanStream = nullptr;
}

//void SuperCanBackend::setConfigurationParameter(int key, const QVariant &value)
//{
//    if (Q_UNLIKELY(!deviceValid())) {
//        return;
//    }

//    switch (key) {
//    case QCanBusDevice::RawFilterKey:
//        setError(SuperCanBackend::tr("RawFilterKey not implemented."), ConfigurationError);
//        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "RawFilterKey not implemented.");
//        break;
//    case QCanBusDevice::ErrorFilterKey:
//        setError(SuperCanBackend::tr("ErrorFilterKey not implemented."), ConfigurationError);
//        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "ErrorFilterKey not implemented.");
//        break;
//    case QCanBusDevice::LoopbackKey:
//        setError(SuperCanBackend::tr("LoopbackKey not implemented."), ConfigurationError);
//        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "LoopbackKey not implemented.");
//        break;
//    case QCanBusDevice::ReceiveOwnKey:
//        setError(SuperCanBackend::tr("ReceiveOwnKey not implemented."), ConfigurationError);
//        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "ReceiveOwnKey not implemented.");
//        break;
//    case QCanBusDevice::BitRateKey:
//        if (m_IsOnBus) {
//            setError(SuperCanBackend::tr("Nominal bitrate cannot change when on bus."), ConfigurationError);
//        } else {
//            auto bitrate = value.toUInt();
//            auto error = setNominalBitrate(bitrate);
//            if (error) {
//                setError(SuperCanBackend::tr("Failed to set nominal bitrate: %1 (%2).").arg(SC_CALL(strerror)(error)).arg(error), ConfigurationError);
//            }
//        }
//        break;
//    case QCanBusDevice::CanFdKey:
//        if (m_IsOnBus) {
//            setError(SuperCanBackend::tr("FD mode cannot change when on bus."), ConfigurationError);
//        } else {
//            auto on = value.toBool();
//            if (m_Device.info.features & SC_FEATURE_FLAG_CAN_FD) {
//                m_Fd = on;
//            } else {
//                setError(SuperCanBackend::tr("Device doesn't support flexible data rate."), ConfigurationError);
//            }
//        }
//        break;
//    case QCanBusDevice::DataBitRateKey:
//        if (m_IsOnBus) {
//            setError(SuperCanBackend::tr("Data bitrate cannot change when on bus."), ConfigurationError);
//        } else {
//            auto bitrate = value.toUInt();
//            auto error = setDataBitrate(bitrate);
//            if (error) {
//                setError(SuperCanBackend::tr("Failed to set data bitrate: %1 (%2).").arg(SC_CALL(strerror)(error)).arg(error), ConfigurationError);
//            }
//        }
//        break;
//    case SuperCanBackend::UrbsPerChannelKey:
//        if (m_IsOnBus) {
//            setError(SuperCanBackend::tr("Number of Urbs cannot change when on bus."), ConfigurationError);
//        } else {
//            m_Urbs = value.toInt();
//        }
//        break;
//    default:
//        qCDebug(QT_CANBUS_PLUGINS_SUPERCAN, "Key %d not implemented.", key);
//        break;
//    }
//}



void SuperCanBackend::applyConfigurationParameter(int key, const QVariant &value)
{
    if (Q_UNLIKELY(!deviceValid())) {
        return;
    }

    switch (key) {
    case QCanBusDevice::RawFilterKey:
        setError(SuperCanBackend::tr("RawFilterKey not implemented."), ConfigurationError);
        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "RawFilterKey not implemented.");
        break;
    case QCanBusDevice::ErrorFilterKey:
        setError(SuperCanBackend::tr("ErrorFilterKey not implemented."), ConfigurationError);
        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "ErrorFilterKey not implemented.");
        break;
    case QCanBusDevice::LoopbackKey:
        setError(SuperCanBackend::tr("LoopbackKey not implemented."), ConfigurationError);
        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "LoopbackKey not implemented.");
        break;
    case QCanBusDevice::ReceiveOwnKey:
        setError(SuperCanBackend::tr("ReceiveOwnKey not implemented."), ConfigurationError);
        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "ReceiveOwnKey not implemented.");
        break;
    case QCanBusDevice::BitRateKey: {
        auto bitrate = value.toUInt();
        auto error = setNominalBitrate(bitrate);
        if (error) {
            setError(SuperCanBackend::tr("Failed to set nominal bitrate: %1 (%2).").arg(SC_CALL(strerror)(error)).arg(error), ConfigurationError);
        }
    } break;
    case QCanBusDevice::CanFdKey: {
        auto on = value.toBool();
        if (on) {
            if (((m_FeatConf | m_FeatPerm) & SC_FEATURE_FLAG_FDF) == SC_FEATURE_FLAG_FDF) {
                m_Fd = on;
            } else {
                setError(SuperCanBackend::tr("Device doesn't support flexible data rate."), ConfigurationError);
            }
        } else {
            if (((m_FeatPerm) & SC_FEATURE_FLAG_FDF) == SC_FEATURE_FLAG_FDF) {
                setError(SuperCanBackend::tr("Flexible data rate cannot be turned off."), ConfigurationError);
            } else {
                m_Fd = on;
            }
        }
    } break;
    case QCanBusDevice::DataBitRateKey: {
        auto bitrate = value.toUInt();
        auto error = setDataBitrate(bitrate);
        if (error) {
            setError(SuperCanBackend::tr("Failed to set data bitrate: %1 (%2).").arg(SC_CALL(strerror)(error)).arg(error), ConfigurationError);
        }
    } break;
    case SuperCanBackend::UrbsPerChannelKey:
        m_Urbs = value.toInt();
        break;
    default:
        qCDebug(QT_CANBUS_PLUGINS_SUPERCAN, "Key %d not implemented.", key);
        break;
    }
}

bool SuperCanBackend::writeFrame(const QCanBusFrame &newData)
{
    if (Q_UNLIKELY(!deviceValid())) {
        return false;
    }

    if (Q_UNLIKELY(!m_IsOnBus)) {
        return false;
    }

    if (Q_UNLIKELY(!newData.isValid())) {
        setError(tr("Cannot write invalid QCanBusFrame"), QCanBusDevice::WriteError);
        return false;
    }

    if (Q_UNLIKELY(newData.frameType() != QCanBusFrame::DataFrame
                   && newData.frameType() != QCanBusFrame::RemoteRequestFrame)) {
        setError(tr("Unable to write a frame with unacceptable type"),
                 QCanBusDevice::WriteError);
        return false;
    }

    if (newData.hasFlexibleDataRateFormat()) {
        if (Q_UNLIKELY(!((m_FeatPerm | m_FeatConf) & SC_FEATURE_FLAG_FDF))) {
            setError(tr("CAN-FD mode not supported."), QCanBusDevice::WriteError);
            return false;
        }

        if (!m_Fd) {
            setError(tr("CAN-FD mode not enabled."), QCanBusDevice::WriteError);
            return false;
        }
    }

    if (hasOutgoingFrames() || !trySendFrame(newData, true)) {
        enqueueOutgoingFrame(newData);
    }

    return true;
}

// TODO: Implement me
QString SuperCanBackend::interpretErrorFrame(const QCanBusFrame &errorFrame)
{
    Q_UNUSED(errorFrame);

    return QString();
}
#if 0

void SuperCanBackend::expired()
{
    QVector<QCanBusFrame> received;
    int error = SC_DLL_ERROR_NONE;
    DWORD transferred = 0;
    quint64 sent = 0;

    uint8_t* rx_msg_ptr = reinterpret_cast<uint8_t*>(rx_buffer.data());

    // poll, send
    for (bool done = false; !done; ) {
        done = true;
        DWORD result = WaitForMultipleObjects(rx_events.size(), rx_events.data(), FALSE, 0);
        if (/*result >= WAIT_OBJECT_0 && */result < WAIT_OBJECT_0 + DWORD(rx_events.size())) {
            done = false;
            DWORD index = result - WAIT_OBJECT_0;

            error = SC_CALL(dev_result)(m_Device.device, &transferred, &rx_ov[static_cast<int>(index)], 0);
            if (error) {
                qCWarning(
                            QT_CANBUS_PLUGINS_SUPERCAN,
                            "Device %u-%u: failed to get read result: %s (%d).",
                            m_Device.index, m_ChannelIndex, SC_CALL(strerror)(error), error);
                emit errorOccurred(QCanBusDevice::CanBusError::ReadError);
                break;
            }

            // process buffer
            uint8_t* in_beg = rx_msg_ptr + m_Device.device->msg_buffer_size * index;
            uint8_t* in_end = in_beg + transferred;
            uint8_t* in_ptr = in_beg;
            while (in_ptr + SC_HEADER_LEN <= in_end) {
                struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;
                if (in_ptr + msg->len > in_end) {
                    qCWarning(
                                QT_CANBUS_PLUGINS_SUPERCAN,
                                "Device %u-%u: malformed message.",
                                m_Device.index, m_ChannelIndex);
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
                        qCWarning(
                                    QT_CANBUS_PLUGINS_SUPERCAN,
                                    "Device %u-%u: malformed sc_msg_status.",
                                    m_Device.index, m_ChannelIndex);
                        break;
                    }

                    uint32_t timestamp_us = dev_to_host32(m_Device.device, status->timestamp_us);
                    if (m_TsHiUs == 0 && m_TsLoUs == 0) {
//                        m_TsLoUs = timestamp_us;
                    } else if (m_TsLoUs > timestamp_us) {
                        ++m_TsHiUs;
                    }

                    m_TsLoUs = timestamp_us;
//                    uint16_t rx_lost = dev_to_host16(m_Device.device, status->rx_lost);
//                    uint16_t tx_dropped = dev_to_host16(m_Device.device, status->tx_dropped);

//                    fprintf(stdout, "Device ch%u rxl=%u txd=%u\n", status->channel, rx_lost, tx_dropped);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                    if (status->flags & SC_STATUS_FLAG_BUS_OFF) {
                        m_BusStatus = QCanBusDevice::CanBusStatus::BusOff;
                    } else if (status->flags & SC_STATUS_FLAG_ERROR_WARNING) {
                        m_BusStatus = QCanBusDevice::CanBusStatus::Warning;
                    } else if (status->flags & SC_STATUS_FLAG_ERROR_PASSIVE) {
                        m_BusStatus = QCanBusDevice::CanBusStatus::Error;
                    } else {
                        m_BusStatus = QCanBusDevice::CanBusStatus::Good;
                    }
#endif
//                    if (status->flags & SC_STATUS_FLAG_RX_FULL) {
//                        fprintf(stdout, "ch%u rx queue full\n", status->channel);
//                    }

//                    if (status->flags & SC_STATUS_FLAG_TX_FULL) {
//                        fprintf(stdout, "ch%u tx queue full\n", status->channel);
//                    }

//                    if (status->flags & SC_STATUS_FLAG_TXR_DESYNC) {
//                        fprintf(stdout, "ch%u txr desync\n", status->channel);
//                    }
                } break;
                case SC_MSG_CAN_RX: {
                    struct sc_msg_can_rx const *rx = (struct sc_msg_can_rx const*)msg;
                    uint32_t can_id = dev_to_host32(m_Device.device, rx->can_id);
                    uint32_t timestamp_us = dev_to_host32(m_Device.device, rx->timestamp_us);
                    uint8_t payload_len = dlc_to_len(rx->dlc);

                    if (m_TsHiUs == 0 && m_TsLoUs == 0) {
//                        m_TsLoUs = timestamp_us;
                    } else if (m_TsLoUs > timestamp_us) {
                        ++m_TsHiUs;
                    }

                    m_TsLoUs = timestamp_us;

                    received.resize(received.size() + 1);
                    QCanBusFrame& frame = received.back();
                    QByteArray payload(payload_len, Qt::Uninitialized);
                    frame.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds((static_cast<uint64_t>(m_TsHiUs)<<32) | m_TsLoUs));

                    if (rx->flags & SC_CAN_FLAG_FDF) {
                        frame.setFlexibleDataRateFormat(true);
                        if (rx->flags & SC_CAN_FLAG_BRS) {
                            frame.setBitrateSwitch(true);
                        }
                        if (rx->flags & SC_CAN_FLAG_ESI) {
                            frame.setErrorStateIndicator(true);
                        }
                    }

                    if (rx->flags & SC_CAN_FLAG_EXT) {
                        frame.setExtendedFrameFormat(true);
                    }
                    frame.setFrameId(can_id);
                    if (rx->flags & SC_CAN_FLAG_RTR) {
                        frame.setFrameType(QCanBusFrame::RemoteRequestFrame);
                    } else {
                        frame.setFrameType(QCanBusFrame::DataFrame);
                        if (msg->len - sizeof(*rx) < payload_len) {
                            qCWarning(
                                        QT_CANBUS_PLUGINS_SUPERCAN,
                                        "Device %u-%u: malformed sc_msg_can_rx.",
                                        m_Device.index, m_ChannelIndex);
                            break;
                        }

                        memcpy(payload.data(), rx + 1, payload_len);
                    }

                    frame.setPayload(payload);
                } break;
                default:
                    break;
                }
            }

            // re-queue in token
            error = SC_CALL(dev_read)(
                        m_Device.device,
                        m_Device.device->msg_pipe_ptr[m_ChannelIndex] | 0x80,
                        in_beg,
                        m_Device.device->msg_buffer_size,
                        &rx_ov[static_cast<int>(index)]);
            if (error && error != SC_DLL_ERROR_IO_PENDING) {
                qCWarning(
                            QT_CANBUS_PLUGINS_SUPERCAN,
                            "Device %u-%u: failed to submit in token: %s (%d).",
                            m_Device.index, m_ChannelIndex, SC_CALL(strerror)(error), error);
                emit errorOccurred(QCanBusDevice::CanBusError::ReadError);
                break;
            }
        }
        else if (WAIT_TIMEOUT == result) {
        } else {
            qCWarning(
                        QT_CANBUS_PLUGINS_SUPERCAN,
                        "WaitForMultipleObjects failed with error %lu.",
                        GetLastError());
            emit errorOccurred(QCanBusDevice::CanBusError::ReadError);
            break;
        }

        if (tx_events.size()) {
            result = WaitForMultipleObjects(tx_events.size(), tx_events.data(), FALSE, 0);
            if (/*result >= WAIT_OBJECT_0 && */result < WAIT_OBJECT_0 + DWORD(tx_events.size())) {
                done = false;
                DWORD index = result - WAIT_OBJECT_0;

                uint8_t ev_index = tx_events_busy[index];
                tx_events_busy[index] = tx_events_busy[tx_events_busy.size()-1];
                tx_events[index] = tx_events[tx_events.size()-1];
                tx_events_busy.pop_back();
                tx_events.pop_back();
                tx_events_available.push_back(ev_index);

                error = SC_CALL(dev_result)(m_Device.device, &transferred, &tx_ov[ev_index], 0);
                if (error) {
                    qCWarning(
                                QT_CANBUS_PLUGINS_SUPERCAN,
                                "Device %u-%u: failed to get write result: %s (%d).",
                                m_Device.index, m_ChannelIndex, SC_CALL(strerror)(error), error);
                    emit errorOccurred(QCanBusDevice::CanBusError::WriteError);
                    break;
                }
            }
            else if (WAIT_TIMEOUT == result) {

            } else {
                qCWarning(
                            QT_CANBUS_PLUGINS_SUPERCAN,
                            "WaitForMultipleObjects failed with error %lu.",
                            GetLastError());
                emit errorOccurred(QCanBusDevice::CanBusError::WriteError);
                break;
            }
        }


        while (tx_events_available.size()) {
            auto frame = dequeueOutgoingFrame();
            if (frame.frameType() == QCanBusFrame::InvalidFrame) {
                break;
            }

            if (!trySendFrame(frame, false)) {
                break;
            }

            ++sent;
        }
    }

    if (!received.empty()) {
        enqueueReceivedFrames(received);
    }

    if (sent) {
        emit framesWritten(sent);
    }
}

int SuperCanBackend::setBus(bool on)
{
    if (m_IsOnBus == on) {
        return SC_DLL_ERROR_NONE;
    }

    if (on) {
        return busOn();
    }

    busOff();

    return SC_DLL_ERROR_NONE;
}


int SuperCanBackend::busOn()
{
    int error = SC_DLL_ERROR_NONE;
    const size_t count = m_Urbs <= 0 ? 16 : (m_Urbs > MAXIMUM_WAIT_OBJECTS ? MAXIMUM_WAIT_OBJECTS : m_Urbs);
    rx_buffer.resize(static_cast<int>(count * m_Device.device->msg_buffer_size));
    tx_buffer.resize(static_cast<int>(count * m_Device.device->msg_buffer_size));

    rx_ov.resize(static_cast<int>(count));
    tx_ov.resize(static_cast<int>(count));
    rx_events.resize(static_cast<int>(count));
    tx_events_available.resize(static_cast<int>(count));

    // clear events so in case of error we can clean up
    for (size_t i = 0; i < count; ++i) {
        rx_ov[static_cast<int>(i)].hEvent = nullptr;
        tx_ov[static_cast<int>(i)].hEvent = nullptr;

    }

    // create events
    for (size_t i = 0; i < count; ++i) {
        rx_ov[static_cast<int>(i)].hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        tx_ov[static_cast<int>(i)].hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!rx_ov[static_cast<int>(i)].hEvent || !tx_ov[static_cast<int>(i)].hEvent) {
            busCleanup();
            return SC_DLL_ERROR_OUT_OF_MEM;
        }

        rx_events[static_cast<int>(i)] = rx_ov[static_cast<int>(i)].hEvent;
        tx_events_available[static_cast<int>(i)] = static_cast<uint8_t>(i);
    }


    // submit in tokens for CAN
    uint8_t* msg_ptr = reinterpret_cast<uint8_t*>(rx_buffer.data());
    for (size_t i = 0; i < count; ++i) {
        error = SC_CALL(dev_read)(
                    m_Device.device,
                    m_Device.device->msg_pipe_ptr[m_ChannelIndex] | 0x80,
                    msg_ptr + i * m_Device.device->msg_buffer_size,
                    m_Device.device->msg_buffer_size,
                    &rx_ov[static_cast<int>(i)]);
        if (SC_DLL_ERROR_IO_PENDING != error) {
            busCleanup();
            return error;
        }
    }

    // submit in token for cmd
    error = SC_CALL(dev_read)(
                m_Device.device,
                m_Device.device->cmd_pipe | 0x80,
                m_Device.cmd_rx_buffer,
                m_Device.device->cmd_buffer_size,
                &m_Device.cmd_rx_ov);
    if (SC_DLL_ERROR_IO_PENDING != error) {
        busCleanup();
        return error;
    }

    uint8_t* cmd_tx_ptr_beg = m_Device.cmd_tx_buffer;
    uint8_t* cmd_tx_ptr = cmd_tx_ptr_beg;
    memset(cmd_tx_ptr, 0, m_Device.device->cmd_buffer_size);

    struct sc_msg_config* bus_off = (struct sc_msg_config*)cmd_tx_ptr;
    bus_off->id = SC_MSG_BUS;
    bus_off->len = sizeof(*bus_off);
    cmd_tx_ptr += bus_off->len;

    bus_off->channel = m_ChannelIndex;
    bus_off->args[0] = 0; // off

    // set bittiming
    struct sc_msg_bittiming* bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
    bt->id = SC_MSG_BITTIMING;
    bt->len = sizeof(*bt);
    cmd_tx_ptr += bt->len;

    bt->channel = m_ChannelIndex;
    bt->nmbt_brp = dev_to_host16(m_Device.device, m_Nominal.brp);
    bt->nmbt_sjw = m_Nominal.sjw;
    bt->nmbt_tseg1 = dev_to_host16(m_Device.device, m_Nominal.tseg1);
    bt->nmbt_tseg2 = m_Nominal.tseg2;
    bt->dtbt_brp = m_Data.brp;
    bt->dtbt_sjw = m_Data.sjw;
    bt->dtbt_tseg1 = m_Data.tseg1;
    bt->dtbt_tseg2 = m_Data.tseg2;

    // set mode
    struct sc_msg_config* mode = (struct sc_msg_config*)cmd_tx_ptr;
    mode->id = SC_MSG_MODE;
    mode->len = sizeof(*mode);
    cmd_tx_ptr += mode->len;

    mode->channel = m_ChannelIndex;
    mode->args[0] = SC_MODE_FLAG_RX | SC_MODE_FLAG_TX | SC_MODE_FLAG_AUTO_RE | SC_MODE_FLAG_EH;
    if (m_Fd) {
        mode->args[0] |= SC_MODE_FLAG_BRS | SC_MODE_FLAG_FD;
    }

    // set bus on
    struct sc_msg_config* bus_on = (struct sc_msg_config*)cmd_tx_ptr;
    bus_on->id = SC_MSG_BUS;
    bus_on->len = sizeof(*bus_on);
    cmd_tx_ptr += bus_on->len;

    bus_on->channel = m_ChannelIndex;
    bus_on->args[0] = 1; // on

    auto bytes = (ULONG)(cmd_tx_ptr - cmd_tx_ptr_beg);
    Q_ASSERT(bytes <= m_Device.device->cmd_buffer_size);

    error = SC_CALL(dev_write)(
                m_Device.device,
                m_Device.device->cmd_pipe,
                cmd_tx_ptr_beg,
                bytes,
                &m_Device.cmd_tx_ov);
    if (SC_DLL_ERROR_IO_PENDING != error) {
        busCleanup();
        return error;
    }

    DWORD transferred;
    error = SC_CALL(dev_result)(m_Device.device, &transferred, &m_Device.cmd_tx_ov, 1000);
    if (error) {
        busCleanup();
        return error;
    }

    if (transferred != bytes) {
        busCleanup();
        return SC_DLL_ERROR_UNKNOWN;
    }

    m_TsLoUs = 0;
    m_TsHiUs = 0;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    m_BusStatus = QCanBusDevice::CanBusStatus::Unknown;
#endif
    m_IsOnBus = true;
    connect(&m_Timer, &QTimer::timeout, this, &SuperCanBackend::expired);
    m_Timer.start();

    return error;
}

void SuperCanBackend::busOff()
{
    busCleanup();
}

#endif

bool SuperCanBackend::trySendFrame(const QCanBusFrame& frame, bool emit_signal)
{
    uint8_t slot = 0;

    if (!m_HasTxr || tryToAcquireTxSlot(&slot)) {
        auto payload = frame.payload();
        uint8_t *ptr = reinterpret_cast<uint8_t *>(tx_buffer.data());
        struct sc_msg_can_tx *tx = reinterpret_cast<struct sc_msg_can_tx *>(ptr);
        uint8_t bytes = sizeof(*tx);

        if (frame.frameType() == QCanBusFrame::DataFrame) {
            memcpy(tx->data, payload.data(), payload.size());
            bytes += payload.size();
        }

        // align to SC_MSG_CAN_LEN_MULTIPLE bytes
        if (tx->len & (SC_MSG_CAN_LEN_MULTIPLE-1)) {
            tx->len += SC_MSG_CAN_LEN_MULTIPLE - (tx->len & (SC_MSG_CAN_LEN_MULTIPLE-1));
        }

        tx->id = SC_MSG_CAN_TX;
        tx->len = bytes;
        tx->dlc = len_to_dlc(payload.size());
        tx->can_id = m_ScDevice->dev_to_host32(frame.frameId());
        tx->track_id = slot;
        tx->flags = 0;

        if (frame.frameType() == QCanBusFrame::RemoteRequestFrame) {
            tx->flags |= SC_CAN_FRAME_FLAG_RTR;
        }

        if (frame.hasExtendedFrameFormat()) {
            tx->flags |= SC_CAN_FRAME_FLAG_EXT;
        }

        if (frame.hasFlexibleDataRateFormat()) {
            tx->flags |= SC_CAN_FRAME_FLAG_FDF;
            if (frame.hasBitrateSwitch()) {
                tx->flags |= SC_CAN_FRAME_FLAG_BRS;
            }
            if (frame.hasErrorStateIndicator()) {
                tx->flags |= SC_CAN_FRAME_FLAG_ESI;
            }
        }

        auto error = sc_can_stream_tx(m_ScCanStream, tx, tx->len, SC_DEVICE_TIMEOUT_MS, nullptr);
        if (error) {
            if (m_HasTxr) {
                m_TxSlotsAvailable.push_back(slot);
            }

            emit errorOccurred(QCanBusDevice::CanBusError::WriteError);
            return false;
        }

        if (emit_signal) {
            emit framesWritten(1);
        }

        return true;
    }

    return false;
}

#if 0

int SuperCanBackend::setNominalBitrate(unsigned bitrate)
{
    if (Q_UNLIKELY(!bitrate)) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    can_regs nm_min, nm_max;
    nm_min.brp = m_Device.info.nmbt_brp_min;
    nm_min.sjw = m_Device.info.nmbt_sjw_min;
    nm_min.tseg1 = m_Device.info.nmbt_tseg1_min;
    nm_min.tseg2 = m_Device.info.nmbt_tseg2_min;
    nm_max.brp = m_Device.info.nmbt_brp_max;
    nm_max.sjw = m_Device.info.nmbt_sjw_max;
    nm_max.tseg1 = m_Device.info.nmbt_tseg1_max;
    nm_max.tseg2 = m_Device.info.nmbt_tseg2_max;

    bitrate_to_can_regs(
                m_Device.info.can_clk_hz,
                bitrate,
                204, // 80%
//                210, // 82.5%
//                216, // 85%
//                223, // 87.5%
                &nm_min,
                &nm_max,
                &m_Nominal);

    return SC_DLL_ERROR_NONE;
}

int SuperCanBackend::setDataBitrate(unsigned value)
{
    if (Q_UNLIKELY(!value)) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    auto bitrate = value;
    uint8_t sample_point;
    if (bitrate <= 2000000) {
        sample_point = 180; // 70%
    } else {
        sample_point = 191; // 75%
    }

    can_regs dt_min, dt_max;
    dt_min.brp = m_Device.info.dtbt_brp_min;
    dt_min.sjw = m_Device.info.dtbt_sjw_min;
    dt_min.tseg1 = m_Device.info.dtbt_tseg1_min;
    dt_min.tseg2 = m_Device.info.dtbt_tseg2_min;
    dt_max.brp = m_Device.info.dtbt_brp_max;
    dt_max.sjw = m_Device.info.dtbt_sjw_max;
    dt_max.tseg1 = m_Device.info.dtbt_tseg1_max;
    dt_max.tseg2 = m_Device.info.dtbt_tseg2_max;

    bitrate_to_can_regs(
                m_Device.info.can_clk_hz,
                bitrate,
                sample_point,
                &dt_min,
                &dt_max,
                &m_Data);



    return SC_DLL_ERROR_NONE;
}

void SuperCanBackend::busCleanup()
{
    disconnect(&m_Timer, &QTimer::timeout, this, &SuperCanBackend::expired);
    m_IsOnBus = false;
    m_Timer.stop();

    for (auto& ov : rx_ov) {
        if (ov.hEvent) {
            SC_CALL(dev_cancel(m_Device.device, &ov));
            CloseHandle(ov.hEvent);
        }
    }

    rx_ov.clear();

    for (auto& ov : tx_ov) {
        if (ov.hEvent) {
            SC_CALL(dev_cancel(m_Device.device, &ov));
            CloseHandle(ov.hEvent);
        }
    }

    tx_ov.clear();
    rx_events.clear();
    tx_events.clear();
    tx_events_busy.clear();
    tx_events_available.clear();

    m_TsLoUs = 0;
    m_TsHiUs = 0;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    m_BusStatus = QCanBusDevice::CanBusStatus::Unknown;
#endif
}

void SuperCanBackend::resetConfiguration()
{
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::RawFilterKey, QVariant());
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::ErrorFilterKey, QVariant());
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::LoopbackKey, QVariant());
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::ReceiveOwnKey, QVariant());
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::BitRateKey, 500000);
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::CanFdKey, false);
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::DataBitRateKey, 2000000);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::ProtocolKey, QVariant());
#endif
}
#endif

int SuperCanBackend::on_rx(void* ctx, void const* ptr, uint16_t bytes)
{
    static_cast<SuperCanBackend*>(ctx)->rx(static_cast<uint8_t const*>(ptr), bytes);
    return 0;
}

void SuperCanBackend::rx(uint8_t const* ptr, uint16_t bytes)
{

}

bool SuperCanBackend::tryToAcquireTxSlot(uint8_t* slot)
{
//    if (!m_HasTxr) {
//        *slot = 0;
//        return true;
//    }

    if (m_TxSlotsAvailable.empty()) {
        return false;
    }

    *slot = m_TxSlotsAvailable.back();
    m_TxSlotsAvailable.pop_back();
    return true;
}

QT_END_NAMESPACE
