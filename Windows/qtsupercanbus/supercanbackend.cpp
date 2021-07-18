/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Jean Gressmann <jean@0x42.de>
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


#include <cstring>
#include <atomic>
#include <QtCore/qloggingcategory.h>
#include <QEvent>
#include <supercan_winapi.h>
#include <can_bit_timing.h>

#include "supercanbackend.h"



using namespace std;

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(QT_CANBUS_PLUGINS_SUPERCAN)

namespace
{

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


} // anon




////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool SuperCanBackend::canCreate(QString *errorReason)
{
//    *errorReason = tr("SuperCAN COM server not registered");
//    return false;

    ComScope cs;

    if (cs) {
        SuperCAN::ISuperCANPtr sc;
        auto hr = sc.CreateInstance(__uuidof(SuperCAN::CSuperCAN));
        if (SUCCEEDED(hr)) {
            return true;
        }

        *errorReason = tr("SuperCAN COM server not registered");
    } else {
        *errorReason = tr("Failed to initialize COM");
    }

    return false;
}

QList<QCanBusDeviceInfo> SuperCanBackend::interfaces()
{
    QList<QCanBusDeviceInfo> result;
    ComScope cs;

    if (cs) {
        SuperCAN::ISuperCANPtr sc;
        auto hr = sc.CreateInstance(__uuidof(SuperCAN::CSuperCAN));
        if (SUCCEEDED(hr)) {
            unsigned long count = 0;

            hr = sc->DeviceScan(&count);
            if (SUCCEEDED(hr)) {

                for ( unsigned long i = 0; i < count; ++i) {

                    SuperCAN::ISuperCANDevicePtr dev;
                    SuperCAN::SuperCANDeviceData devData;
                    char serial_buffer[2 * sizeof(devData.sn_bytes) + 1];

                    hr = sc->DeviceOpen(i, &dev);
                    if (SUCCEEDED(hr)) {
                        hr = dev->GetDeviceData(&devData);
                        if (SUCCEEDED(hr)) {
                            _bstr_t comName;

                            comName.Attach(devData.name);
                            devData.name = NULL;

                            memset(serial_buffer, 0, sizeof(serial_buffer));

                            for (size_t j = 0, k = 0, e = qMin<size_t>(sizeof(devData.sn_bytes), devData.sn_length); j < e; j += 2, ++k) {
                                serial_buffer[j] = "0123456789abcdef"[(serial_buffer[k] >> 4) & 0xf];
                                serial_buffer[j+1] = "0123456789abcdef"[(serial_buffer[k]) & 0xf];
                            }

                            auto devInfo = createDeviceInfo(
                                        QStringLiteral("%1").arg(i),
                                        QString::fromUtf8(serial_buffer),
                                        QString::fromUtf16((const ushort*)(const wchar_t*)comName),
                                        0, // channel, each dev is on its own
                                        false /* isVirtual */,
                                        SC_FEATURE_FLAG_FDF == (devData.feat_conf | devData.feat_perm));

                            result << devInfo;

                        } else {
                            qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to get device data for device index %zu (hr=%lx).", i, hr);
                        }
                    } else {
                        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to open device index %zu (hr=%lx).", i, hr);
                    }
                }
            } else {
                qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to scan for devices (hr=%lx).", hr);
            }

        } else {
            qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to create SuperCAN::CSuperCAN (hr=%lx).", hr);
        }
    } else {
        qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to initialize COM.");
    }

    return result;
}


SuperCanBackend::~SuperCanBackend()
{
    busCleanup();
    uninit();
}

SuperCanBackend::SuperCanBackend(const QString &name, QObject *parent)
    : QCanBusDevice(parent)
{
    m_RxRingFileHandle = nullptr;
    m_TxRingFileHandle = nullptr;
    m_RxRingEventHandle = nullptr;
    m_TxRingEventHandle = nullptr;
    m_RxRingPtr = nullptr;
    m_TxRingPtr = nullptr;
    m_RxRingElements = 0;
    m_TxRingElements = 0;
    m_IsOnBus = false;
    m_ConfiguredBus = false;
    m_Initialized = false;
    m_ScDeviceIndex = name.toUInt();
    m_InitRequired = true;

    m_Timer.setParent(this);
    m_Timer.setInterval(16);
    m_Timer.setSingleShot(false);

    qCDebug(QT_CANBUS_PLUGINS_SUPERCAN, "Device index %u", m_ScDeviceIndex);


#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    m_BusStatus = QCanBusDevice::CanBusStatus::Unknown;
    setCanBusStatusGetter([this] { return m_BusStatus; });
#endif

    resetConfiguration();
}

bool SuperCanBackend::init()
{
    uninit();

    m_Com.Init();

    if (m_Com) {
        SuperCAN::ISuperCANPtr sc;
        auto hr = sc.CreateInstance(__uuidof(SuperCAN::CSuperCAN));

        if (SUCCEEDED(hr)) {
            unsigned long count = 0;

            hr = sc->DeviceScan(&count);
            if (SUCCEEDED(hr)) {
                hr = sc->DeviceOpen(m_ScDeviceIndex, (SuperCAN::ISuperCANDevice * *)&m_ScDevice);
                if (SUCCEEDED(hr)) {
                    hr = m_ScDevice->GetDeviceData(&m_ScDeviceData);
                    if (SUCCEEDED(hr)) {
                        SysFreeString(m_ScDeviceData.name);
                        m_ScDeviceData.name = nullptr;
                        m_Initialized = true;
                        return true;
                    } else {
                        setError(tr("Failed to get device data from device index %1 (hr=%2)").arg(m_ScDeviceIndex).arg(hr, 0, 16), OperationError);
                    }
                } else {
                    setError(tr("Failed to open SuperCAN device index %1 (hr=%2)").arg(m_ScDeviceIndex).arg(hr, 0, 16), OperationError);
                }
            } else {
                setError(tr("Failed to scan for SuperCAN devices (hr=%1)").arg(hr, 0, 16), OperationError);
            }
        } else {
            setError(tr("Failed to open SuperCAN driver (hr=%1)").arg(hr, 0, 16), OperationError);
        }
    } else {
        setError(tr("Failed to initialize COM."), OperationError);
    }

    return false;
}


void SuperCanBackend::uninit()
{
    // release COM object before COM uninitializes
    m_ScDevice = nullptr;
    m_Com.Uninit();
}

bool SuperCanBackend::open()
{
    if (m_InitRequired && !init()) {
        return false;
    }

    if (Q_UNLIKELY(!deviceValid())) {
        return false;
    }

    return setBus(true);
}


void SuperCanBackend::close()
{
    if (Q_UNLIKELY(!m_InitRequired)) {
        return;
    }

    if (Q_UNLIKELY(!deviceValid())) {
        return;
    }

    auto error = setBus(false);
    if (error) {
        setError(SuperCanBackend::tr("Failed to go off bus for device index %1.").arg(m_ScDeviceIndex), OperationError);
    }

    setState(QCanBusDevice::UnconnectedState);
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
        setError(tr("Refusing to write invalid QCanBusFrame"), QCanBusDevice::WriteError);
        return false;
    }

    if (Q_UNLIKELY(newData.frameType() != QCanBusFrame::DataFrame
                   && newData.frameType() != QCanBusFrame::RemoteRequestFrame)) {
        setError(tr("Refusing to write a frame with unacceptable type"),
                 QCanBusDevice::WriteError);
        return false;
    }

    // We are not going to copy frames around, there should be
    // plenty of space in the ring buffer.
//    if (hasOutgoingFrames()) {
//        enqueueOutgoingFrame(newData);
//        return true;
//    }

    uint32_t tx_gi = m_TxRingPtr->get_index;
    uint32_t tx_pi = m_TxRingPtr->put_index;
    uint32_t tx_count = tx_pi - tx_gi;

    if (tx_count > m_TxRingElements) {
        setError(tr("Bad COM server reports %1 elements in %2 element tx ring buffer").arg(tx_count).arg(m_TxRingElements), UnknownError);
        return false;
    }

    if (tx_count == m_TxRingElements) {
        // see comment above
//        enqueueOutgoingFrame(newData);
//        return true;
        return false;
    }

    placeFrame(newData, tx_pi % m_TxRingElements);

    ++tx_pi;

    m_TxRingPtr->put_index = tx_pi;

    SetEvent(m_TxRingEventHandle);

    emit framesWritten(1);

    return true;
}

// TODO: Implement me
QString SuperCanBackend::interpretErrorFrame(const QCanBusFrame &errorFrame)
{
    Q_UNUSED(errorFrame);

    return QString();
}


void SuperCanBackend::expired()
{
    uint32_t rx_gi = m_RxRingPtr->get_index;
    uint32_t rx_pi = m_RxRingPtr->put_index;
    uint32_t rx_count = rx_pi - rx_gi;

    if (rx_count > m_RxRingElements) {
        setError(tr("Bad COM server reports %1 elements in %2 element rx ring buffer").arg(rx_count).arg(m_RxRingElements), UnknownError);
        return;
    }

    if (rx_count) {
        QVector<QCanBusFrame> received;
        received.reserve(rx_count);

        while (rx_gi != rx_pi) {
            uint32_t index = rx_gi % m_RxRingElements;
            const sc_can_mm_slot_t* s = &m_RxRingPtr->elements[index];

            ++rx_gi;

            switch (s->hdr.type) {
            case SC_CAN_DATA_TYPE_STATUS: {
                switch (s->status.bus_status) {
                case SC_CAN_STATUS_BUS_OFF: {
                    QCanBusFrame f(QCanBusFrame::ErrorFrame);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                    setBusStatus(QCanBusDevice::CanBusStatus::BusOff);
#endif
                    f.setError(QCanBusFrame::FrameError::BusOffError);
                    f.setTimeStamp(convertTimestamp(s->status.timestamp_us));
                    received << f;
                } break;
                case SC_CAN_STATUS_ERROR_ACTIVE:
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                    setBusStatus(QCanBusDevice::CanBusStatus::Good);
#endif
                    break;
                case SC_CAN_STATUS_ERROR_WARNING:
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                    setBusStatus(QCanBusDevice::CanBusStatus::Warning);
                    break;
                    #endif
                case SC_CAN_STATUS_ERROR_PASSIVE:
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                    setBusStatus(QCanBusDevice::CanBusStatus::Error);
#endif
                    break;
                default:
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
                    setBusStatus(QCanBusDevice::CanBusStatus::Unknown);
#endif
                    break;
                }
            } break;
            case SC_CAN_DATA_TYPE_RX: {
                if (s->rx.flags & SC_CAN_FRAME_FLAG_RTR) {
                    QCanBusFrame f(QCanBusFrame::RemoteRequestFrame);

                    f.setTimeStamp(convertTimestamp(s->rx.timestamp_us));
                    f.setFrameId(s->rx.can_id);
                    f.setExtendedFrameFormat((s->rx.flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT);
                    f.setPayload(QByteArray(dlc_to_len(s->rx.dlc), Qt::Uninitialized));

                    received << f;
                } else {
                    QCanBusFrame f(QCanBusFrame::DataFrame);

                    f.setTimeStamp(convertTimestamp(s->rx.timestamp_us));
                    f.setFrameId(s->rx.can_id);
                    f.setExtendedFrameFormat((s->rx.flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT);
                    f.setPayload(QByteArray((const char*)s->rx.data, dlc_to_len(s->rx.dlc)));
                    f.setErrorStateIndicator((s->rx.flags & SC_CAN_FRAME_FLAG_ESI) == SC_CAN_FRAME_FLAG_ESI);
                    f.setFlexibleDataRateFormat((s->rx.flags & SC_CAN_FRAME_FLAG_FDF) == SC_CAN_FRAME_FLAG_FDF);
                    f.setBitrateSwitch((s->rx.flags & SC_CAN_FRAME_FLAG_BRS) == SC_CAN_FRAME_FLAG_BRS);

                    received << f;
                }
            } break;
            case SC_CAN_DATA_TYPE_TX: {
                if (!(s->tx.flags & SC_CAN_FRAME_FLAG_DRP) && m_Echo) {
                    if (s->tx.flags & SC_CAN_FRAME_FLAG_RTR) {
                        QCanBusFrame f(QCanBusFrame::RemoteRequestFrame);

                        f.setLocalEcho(true);
                        f.setTimeStamp(convertTimestamp(s->tx.timestamp_us));
                        f.setFrameId(s->tx.can_id);
                        f.setExtendedFrameFormat((s->tx.flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT);
                        f.setPayload(QByteArray(dlc_to_len(s->tx.dlc), Qt::Uninitialized));

                        received << f;
                    } else {
                        QCanBusFrame f(QCanBusFrame::DataFrame);

                        f.setLocalEcho(true);
                        f.setTimeStamp(convertTimestamp(s->tx.timestamp_us));
                        f.setFrameId(s->tx.can_id);
                        f.setExtendedFrameFormat((s->tx.flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT);
                        f.setPayload(QByteArray((const char*)s->tx.data, dlc_to_len(s->tx.dlc)));
                        f.setErrorStateIndicator((s->tx.flags & SC_CAN_FRAME_FLAG_ESI) == SC_CAN_FRAME_FLAG_ESI);
                        f.setFlexibleDataRateFormat((s->tx.flags & SC_CAN_FRAME_FLAG_FDF) == SC_CAN_FRAME_FLAG_FDF);
                        f.setBitrateSwitch((s->tx.flags & SC_CAN_FRAME_FLAG_BRS) == SC_CAN_FRAME_FLAG_BRS);

                        received << f;
                    }
                }
            } break;
            case SC_CAN_DATA_TYPE_ERROR: {
                QCanBusFrame f(QCanBusFrame::ErrorFrame);
                QCanBusFrame::FrameErrors errors = QCanBusFrame::NoError;

                f.setTimeStamp(convertTimestamp(s->error.timestamp_us));

                switch (s->error.error) {
                case SC_CAN_ERROR_ACK:
                    errors = QCanBusFrame::MissingAcknowledgmentError;
                    break;
                case SC_CAN_ERROR_BIT0:
                case SC_CAN_ERROR_BIT1:
                case SC_CAN_ERROR_FORM:
                case SC_CAN_ERROR_STUFF:
                    errors = QCanBusFrame::ProtocolViolationError;
                    break;
                case SC_CAN_ERROR_CRC:
                    errors = QCanBusFrame::BusError;
                    break;
                default:
                    break;
                }

                if (QCanBusFrame::NoError == errors) {
                    errors = QCanBusFrame::UnknownError;
                }

                f.setError(errors);
                received << f;
            } break;
            default:
                break;
            }
        }


        m_RxRingPtr->get_index = rx_gi;

        enqueueReceivedFrames(received);
    }

    Q_ASSERT(!hasOutgoingFrames());


//    qint64 sent = 0;
//    uint32_t tx_gi = m_TxRingPtr->get_index;

//    while (hasOutgoingFrames()) {
//        uint32_t tx_pi = m_TxRingPtr->put_index;
//        uint32_t tx_count = tx_pi - tx_gi;

//        if (tx_count > m_TxRingElements) {
//            setError(tr("Bad COM server reports %1 elements in %2 element tx ring buffer").arg(tx_count).arg(m_TxRingElements), UnknownError);
//            return;
//        }

//        if (tx_count < m_TxRingElements) {
//            auto frame = dequeueOutgoingFrame();

//            uint32_t index = tx_pi % m_TxRingElements;

//            placeFrame(std::move(f), index);

//            ++tx_pi;

//            m_TxRingPtr->put_index = tx_pi;

//            SetEvent(m_TxRingEventHandle);

//            ++sent;
//        }

//    }

//    if (sent) {
//        emit framesWritten(sent);
//    }
}

void SuperCanBackend::placeFrame(const QCanBusFrame& frame, quint32 index)
{
    sc_can_mm_slot_t* s = &m_TxRingPtr->elements[index];
    auto payload = frame.payload();

    Q_ASSERT(payload.size() <= 64);

    s->tx.type = SC_CAN_DATA_TYPE_TX;
    s->tx.dlc = len_to_dlc(payload.size());
    s->tx.can_id = frame.frameId();
    s->tx.flags = 0;

    if (frame.hasExtendedFrameFormat()) {
        s->tx.flags |= SC_CAN_FRAME_FLAG_EXT;
    }

    if (frame.hasFlexibleDataRateFormat()) {
        s->tx.flags |= SC_CAN_FRAME_FLAG_FDF;
        if (frame.hasBitrateSwitch()) {
            s->tx.flags |= SC_CAN_FRAME_FLAG_BRS;
        }
        if (frame.hasErrorStateIndicator()) {
            s->tx.flags |= SC_CAN_FRAME_FLAG_ESI;
        }
        memcpy(s->tx.data, payload.data(), payload.size());
    } else {
        if (frame.frameType() == QCanBusFrame::RemoteRequestFrame) {
            s->tx.flags |= SC_CAN_FRAME_FLAG_RTR;
        } else {
            memcpy(s->tx.data, payload.data(), payload.size());
        }
    }
}

bool SuperCanBackend::setBus(bool on)
{
    if (m_IsOnBus == on) {
        return true;
    }

    if (on) {
        setState(QCanBusDevice::CanBusDeviceState::ConnectingState);
        if (busOn()) {
            setState(QCanBusDevice::CanBusDeviceState::ConnectedState);
            return true;
        }
        return false;
    }

    setState(QCanBusDevice::CanBusDeviceState::ClosingState);

    busOff();

    setState(QCanBusDevice::CanBusDeviceState::UnconnectedState);

    return true;
}


bool SuperCanBackend::busOn()
{
    SuperCAN::SuperCANRingBufferMapping rx, tx;
    _bstr_t rx_mem_name, rx_ev_name, tx_mem_name, tx_ev_name;
    bool configureBus = !configurationParameter(DontConfigureKey).toBool();
    bool configurationAccess = false;

    memset(&rx, 0, sizeof(rx));
    memset(&tx, 0, sizeof(tx));

    auto hr = m_ScDevice->GetRingBufferMappings(&rx, &tx);
    if (FAILED(hr)) {
        setError(tr("Failed to get ring buffer mappings from device index %1 (hr=%2)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
        goto cleanup;
    }

    m_RxRingElements = rx.Elements;
    m_TxRingElements = tx.Elements;


    rx_mem_name.Attach(rx.MemoryName);
    rx.MemoryName = nullptr;
    rx_ev_name.Attach(rx.EventName);
    rx.EventName = nullptr;
    tx_mem_name.Attach(tx.MemoryName);
    tx.MemoryName = nullptr;
    tx_ev_name.Attach(tx.EventName);
    tx.EventName = nullptr;

    m_RxRingFileHandle = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, rx_mem_name);
    if (!m_RxRingFileHandle) {
        setError(tr("Failed to open rx ring file mapping (error=%1)").arg(GetLastError()), ConnectionError);
        goto cleanup;
    }

    m_TxRingFileHandle = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, tx_mem_name);
    if (!m_TxRingFileHandle) {
        setError(tr("Failed to open tx ring file mapping (error=%1)").arg(GetLastError()), ConnectionError);
        goto cleanup;
    }

    m_RxRingPtr = static_cast<sc_can_mm_header*>(MapViewOfFile(m_RxRingFileHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, rx.Bytes));
    if (!m_RxRingPtr) {
        setError(tr("Failed to map rx ring memory (error=%1)").arg(GetLastError()), ConnectionError);
        goto cleanup;
    }

    m_TxRingPtr = static_cast<sc_can_mm_header*>(MapViewOfFile(m_TxRingFileHandle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, tx.Bytes));
    if (!m_TxRingPtr) {
        setError(tr("Failed to map rx ring memory (error=%1)").arg(GetLastError()), ConnectionError);
        goto cleanup;
    }

    m_RxRingEventHandle = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, rx_ev_name);
    if (!m_RxRingEventHandle) {
        setError(tr("Failed to open rx ring event (error=%1)").arg(GetLastError()), ConnectionError);
        goto cleanup;
    }

    m_TxRingEventHandle = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, tx_ev_name);
    if (!m_TxRingEventHandle) {
        setError(tr("Failed to open tx ring event (error=%1)").arg(GetLastError()), ConnectionError);
        goto cleanup;
    }

    // clear rx ring
    m_RxRingPtr->get_index = m_RxRingPtr->put_index;

    // setup bus
    if (configureBus) {
        char configAccess = 0;
        unsigned long timeoutMs = 0;
        auto hr = m_ScDevice->AcquireConfigurationAccess(&configAccess, &timeoutMs);
        if (FAILED(hr)) {
            setError(tr("Failed to request device configuration access (hr=%1)").arg(hr, 0, 16), ConnectionError);
            goto cleanup;
        }

        configurationAccess = configAccess != 0;

        if (configAccess) {
            bool fd = configurationParameter(CanFdKey).toBool();
            unsigned nominalBitrate = configurationParameter(BitRateKey).toUInt();
            unsigned dataBitrate = configurationParameter(DataBitRateKey).toUInt();
            float nominalSamplePoint = configurationParameter(NominalSamplePoint).toFloat();
            float dataSamplePoint = configurationParameter(DataSamplePoint).toFloat();
            int nominalSjw = configurationParameter(NominalSjw).toInt();
            int dataSjw = configurationParameter(DataSjw).toInt();
            can_bit_timing_hw_contraints nominalConstraints, dataConstraints;
            can_bit_timing_settings nominalSettings, dataSettings;
            can_bit_timing_constraints_real nominalTarget, dataTarget;
            SuperCAN::SuperCANBitTimingParams comBitTimings;

            if (fd && !((m_ScDeviceData.feat_perm | m_ScDeviceData.feat_conf) & SC_FEATURE_FLAG_FDF)) {
                setError(tr("Device index %u doesn't support CAN-FD").arg(m_ScDeviceIndex), ConfigurationError);
                goto cleanup;
            }

            if (!nominalBitrate || nominalBitrate > 1000000u) {
                setError(tr("Nominal bitrate must be positive and less or equal than 1000000"), ConfigurationError);
                goto cleanup;
            }

            if (nominalSamplePoint > 1) {
                setError(tr("Nominal sample point must be in range [0-1]"), ConfigurationError);
                goto cleanup;
            }

            memset(&comBitTimings, 0, sizeof(comBitTimings));

            memset(&nominalTarget, 0, sizeof(nominalTarget));
            memset(&nominalConstraints, 0, sizeof(nominalConstraints));


            nominalConstraints.clock_hz = m_ScDeviceData.can_clock_hz;
            nominalConstraints.brp_max = m_ScDeviceData.nm_max.brp;
            nominalConstraints.brp_min = m_ScDeviceData.nm_min.brp;
            nominalConstraints.brp_step = 1;
            nominalConstraints.sjw_max = m_ScDeviceData.nm_max.sjw;
            nominalConstraints.tseg1_max = m_ScDeviceData.nm_max.tseg1;
            nominalConstraints.tseg1_min = m_ScDeviceData.nm_min.tseg1;
            nominalConstraints.tseg2_max = m_ScDeviceData.nm_max.tseg2;
            nominalConstraints.tseg2_min = m_ScDeviceData.nm_min.tseg2;

            if (fd) {
                if (!dataBitrate || dataBitrate > 100000000u) {
                    setError(tr("Data bitrate must be positive and less or equal than 10000000"), ConfigurationError);
                    goto cleanup;
                }

                if (dataSamplePoint > 1) {
                    setError(tr("Data sample point must be in range [0-1]"), ConfigurationError);
                    goto cleanup;
                }

                memset(&dataTarget, 0, sizeof(dataTarget));
                memset(&dataConstraints, 0, sizeof(dataConstraints));

                dataConstraints.clock_hz = m_ScDeviceData.can_clock_hz;
                dataConstraints.brp_max = m_ScDeviceData.dt_max.brp;
                dataConstraints.brp_min = m_ScDeviceData.dt_min.brp;
                dataConstraints.brp_step = 1;
                dataConstraints.sjw_max = m_ScDeviceData.dt_max.sjw;
                dataConstraints.tseg1_max = m_ScDeviceData.dt_max.tseg1;
                dataConstraints.tseg1_min = m_ScDeviceData.dt_min.tseg1;
                dataConstraints.tseg2_max = m_ScDeviceData.dt_max.tseg2;
                dataConstraints.tseg2_min = m_ScDeviceData.dt_min.tseg2;

                cia_fd_cbt_init_default_real(&nominalTarget, &dataTarget);

                nominalTarget.bitrate = nominalBitrate;
                if (nominalSamplePoint > 0) {
                    nominalTarget.sample_point = nominalSamplePoint;
                }

                if (nominalSjw > 0) {
                    nominalTarget.sjw = nominalSjw;
                }

                dataTarget.bitrate = dataBitrate;
                if (dataSamplePoint > 0) {
                    dataTarget.sample_point = dataSamplePoint;
                }

                if (dataSjw > 0) {
                    dataTarget.sjw = dataSjw;
                }

                auto error = cia_fd_cbt_real(
                            &nominalConstraints,
                            &dataConstraints,
                            &nominalTarget,
                            &dataTarget,
                            &nominalSettings,
                            &dataSettings);

                switch (error) {
                case CAN_BTRE_NO_SOLUTION:
                    setError(tr("Device index %u doesn't support the configured CAN configuration").arg(m_ScDeviceIndex), ConfigurationError);
                    goto cleanup;
                    break;
                case CAN_BTRE_NONE:
                    break;
                default:
                    setError(tr("Device index %u failed to compute CAN bit timing settings").arg(m_ScDeviceIndex), UnknownError);
                    goto cleanup;
                }

                hr = m_ScDevice->SetBus(false);
                if (FAILED(hr)) {
                    setError(tr("Failed to take device %u off bus for configuration (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }

                hr = m_ScDevice->SetFeatureFlags(SC_FEATURE_FLAG_FDF);
                if (FAILED(hr)) {
                    setError(tr("Failed to enable CAN-FD feature for device %u (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }


                comBitTimings.brp = nominalSettings.brp;
                comBitTimings.tseg1 = nominalSettings.tseg1;
                comBitTimings.tseg2 = nominalSettings.tseg2;
                comBitTimings.sjw = nominalSettings.sjw;

                hr = m_ScDevice->SetNominalBitTiming(comBitTimings);
                if (FAILED(hr)) {
                    setError(tr("Failed to set nominal bit timings for device %u (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }

                comBitTimings.brp = dataSettings.brp;
                comBitTimings.tseg1 = dataSettings.tseg1;
                comBitTimings.tseg2 = dataSettings.tseg2;
                comBitTimings.sjw = dataSettings.sjw;

                hr = m_ScDevice->SetDataBitTiming(comBitTimings);
                if (FAILED(hr)) {
                    setError(tr("Failed to set data bit timings for device %u (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }

            } else {
                cia_classic_cbt_init_default_real(&nominalTarget);

                nominalTarget.bitrate = nominalBitrate;
                if (nominalSamplePoint > 0) {
                    nominalTarget.sample_point = nominalSamplePoint;
                }

                if (nominalSjw > 0) {
                    nominalTarget.sjw = nominalSjw;
                }

                auto error = cia_classic_cbt_real(
                            &nominalConstraints,
                            &nominalTarget,
                            &nominalSettings);

                switch (error) {
                case CAN_BTRE_NO_SOLUTION:
                    setError(tr("Device index %u doesn't support the configured CAN configuration").arg(m_ScDeviceIndex), ConfigurationError);
                    goto cleanup;
                    break;
                case CAN_BTRE_NONE:
                    break;
                default:
                    setError(tr("Device index %u failed to compute CAN bit timing settings").arg(m_ScDeviceIndex), UnknownError);
                    goto cleanup;
                }

                hr = m_ScDevice->SetBus(false);
                if (FAILED(hr)) {
                    setError(tr("Failed to take device %u off bus for configuration (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }

                hr = m_ScDevice->SetFeatureFlags(0);
                if (FAILED(hr)) {
                    setError(tr("Failed to enable CAN-FD feature for device %u (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }


                comBitTimings.brp = nominalSettings.brp;
                comBitTimings.tseg1 = nominalSettings.tseg1;
                comBitTimings.tseg2 = nominalSettings.tseg2;
                comBitTimings.sjw = nominalSettings.sjw;

                hr = m_ScDevice->SetNominalBitTiming(comBitTimings);
                if (FAILED(hr)) {
                    setError(tr("Failed to set nominal bit timings for device %u (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                    goto cleanup;
                }

            }

            hr = m_ScDevice->SetBus(true);
            if (FAILED(hr)) {
                setError(tr("Failed to take device %u on bus (hr=%1)").arg(m_ScDeviceIndex).arg(hr, 0, 16), ConnectionError);
                goto cleanup;
            }

            m_ScDevice->ReleaseConfigurationAccess();

            m_ConfiguredBus = true;
            configurationAccess = false;
        } else {
            qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Device configuration access requested but unable to seize the device index %u", m_ScDeviceIndex);
        }
    }


#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    setBusStatus(QCanBusDevice::CanBusStatus::Unknown);
#endif
    m_IsOnBus = true;
    m_Echo = configurationParameter(ReceiveOwnKey).toBool();
    connect(&m_Timer, &QTimer::timeout, this, &SuperCanBackend::expired);
    m_Timer.start();

    return true;

cleanup:
    if (configurationAccess) {
        m_ScDevice->ReleaseConfigurationAccess();
    }
    busCleanup();
    return false;
}

void SuperCanBackend::busOff()
{
    busCleanup();
}

void SuperCanBackend::busCleanup()
{
    disconnect(&m_Timer, &QTimer::timeout, this, &SuperCanBackend::expired);
    m_IsOnBus = false;
    m_Timer.stop();

    if (m_RxRingPtr) {
        UnmapViewOfFile(m_RxRingPtr);
        m_RxRingPtr = nullptr;
    }

    if (m_TxRingPtr) {
        UnmapViewOfFile(m_TxRingPtr);
        m_TxRingPtr = nullptr;
    }

    if (m_RxRingFileHandle) {
        CloseHandle(m_RxRingFileHandle);
        m_RxRingFileHandle = nullptr;
    }

    if (m_TxRingFileHandle) {
        CloseHandle(m_TxRingFileHandle);
        m_TxRingFileHandle = nullptr;
    }

    if (m_RxRingEventHandle) {
        CloseHandle(m_RxRingEventHandle);
        m_RxRingEventHandle = nullptr;
    }

    if (m_TxRingEventHandle) {
        CloseHandle(m_TxRingEventHandle);
        m_TxRingEventHandle = nullptr;
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    setBusStatus(QCanBusDevice::CanBusStatus::Unknown);
#endif

    if (m_ConfiguredBus) {
        char configAccess = 0;
        unsigned long timeoutMs = 0;
        auto hr = m_ScDevice->AcquireConfigurationAccess(&configAccess, &timeoutMs);

        if (SUCCEEDED(hr)) {
            hr = m_ScDevice->SetBus(false);
            if (FAILED(hr)) {
                qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to take device %u off the bus (hr=%lx).", m_ScDeviceIndex, hr);
            }

            m_ScDevice->ReleaseConfigurationAccess();
        } else {
            qCWarning(QT_CANBUS_PLUGINS_SUPERCAN, "Failed to request configuration access to take device %u off the bus (hr=%lx).", m_ScDeviceIndex, hr);
        }
    }
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
    QCanBusDevice::setConfigurationParameter(DontConfigureKey, false);
    QCanBusDevice::setConfigurationParameter(NominalSamplePoint, 0.f);
    QCanBusDevice::setConfigurationParameter(DataSamplePoint, 0.f);
    QCanBusDevice::setConfigurationParameter(NominalSjw, -1);
    QCanBusDevice::setConfigurationParameter(DataSjw, -1);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QCanBusDevice::setConfigurationParameter(QCanBusDevice::ProtocolKey, QVariant());
#endif
}

QCanBusFrame::TimeStamp SuperCanBackend::convertTimestamp(quint64 t)
{
    quint64 secs = t / quint64(1000000);
    return QCanBusFrame::TimeStamp(secs, t - secs * quint64(1000000));
}

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
void SuperCanBackend::setBusStatus(QCanBusDevice::CanBusStatus status)
{
    if (m_BusStatus != status) {
        m_BusStatus = status;
        // WTF: no signal in QCanBusDevice?
        emit busStatusChanged(status);
    }
}
#endif

bool SuperCanBackend::event(QEvent* ev)
{
    if (QEvent::ThreadChange == ev->type()) {
        busCleanup();
        uninit();
        m_InitRequired = true;
    }

    return QCanBusDevice::event(ev);
}

QT_END_NAMESPACE

