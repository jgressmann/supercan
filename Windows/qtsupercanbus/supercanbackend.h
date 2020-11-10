/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jean Gressmann <jean@0x42.de>
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

#pragma once

#include <QtSerialBus/qcanbusframe.h>
#include <QtSerialBus/qcanbusdevice.h>
#include <QtSerialBus/qcanbusdeviceinfo.h>

#include <QtCore/qvariant.h>
#include <QtCore/qvector.h>
#include <QtCore/qlist.h>
#include <QtCore/qtimer.h>

#include <qt_windows.h>

#ifndef SC_STATIC
#   define SC_DLL_API __declspec(dllimport)
#endif
#include <supercan_winapi.h>
#include <supercan_dll.h>
#include <supercan_misc.h>

QT_BEGIN_NAMESPACE


class SuperCanBackend : public QCanBusDevice
{
    Q_OBJECT
    Q_DISABLE_COPY(SuperCanBackend)
public:
    enum SuperCanConfigKey {
        UrbsPerChannelKey = UserKey,
    };
    Q_ENUM(SuperCanConfigKey)

public:
    ~SuperCanBackend() override;
    explicit SuperCanBackend(const QString &name, QObject *parent = nullptr);


    bool open() override;
    void close() override;

//    void setConfigurationParameter(int key, const QVariant &value) override;

    bool writeFrame(const QCanBusFrame &newData) override;

    QString interpretErrorFrame(const QCanBusFrame &errorFrame) override;

    static bool canCreate(QString *errorReason);
    static QList<QCanBusDeviceInfo> interfaces();


private slots:
    void expired();


private:
    int busOn();
    void busOff();
    void busCleanup();
    bool trySendFrame(const QCanBusFrame& frame, bool emit_signal);
    inline bool deviceValid() const { return m_Initialized; }
    int setBus(bool on);
    int setNominalBitrate(unsigned value);
    int setDataBitrate(unsigned value);
    void resetConfiguration();
    void applyConfigurationParameter(int key, const QVariant &value);
    static int on_rx(void* ctx, void const* ptr, uint16_t bytes);
    void rx(uint8_t const* ptr, uint16_t bytes);
    bool tryToAcquireTxSlot(uint8_t* slot);

private:
    QTimer m_Timer;
    sc_dev_t* m_ScDevice;
    sc_cmd_ctx_t m_ScCmdCtx;
    sc_can_stream_t m_ScCanStream;
    sc_msg_dev_info m_ScDevInfo;
    sc_msg_can_info m_ScCanInfo;
    sc_dev_time_tracker_t m_TimeTracker;
    uint32_t m_ScDevIndex;
    QByteArray rx_buffer;
    QByteArray tx_buffer;
    QVector<uint8_t> m_TxSlotsAvailable;
    int m_Urbs;
    uint16_t m_FeatPerm;
    uint16_t m_FeatConf;
    bool m_IsOnBus;
    bool m_Fd;
    bool m_HasFd;
    bool m_HasTxr;
    bool m_Initialized;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QCanBusDevice::CanBusStatus m_BusStatus;
#endif

};

QT_END_NAMESPACE
