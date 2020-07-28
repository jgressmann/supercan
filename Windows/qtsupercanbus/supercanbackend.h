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

QT_BEGIN_NAMESPACE


struct can_regs {
    uint16_t brp;
    uint16_t tseg1;
    uint8_t tseg2;
    uint8_t sjw;
};

class ScDevice
{
public:
    ~ScDevice();
    ScDevice();
    ScDevice(const ScDevice&) = delete;
    ScDevice& operator=(const ScDevice&) = delete;
    ScDevice(ScDevice&& other);
    ScDevice& operator=(ScDevice&& other);

    void close();
    int open(uint32_t index, int timeout_ms);

public:
    sc_dev_t* device;
    uint8_t* cmd_tx_buffer;
    uint8_t* cmd_rx_buffer;
    uint32_t index;
    struct sc_msg_dev_info info;
    OVERLAPPED cmd_tx_ov, cmd_rx_ov;
};


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
    inline bool deviceValid() const { return !!m_Device.device; }
    int setBus(bool on);
    int setNominalBitrate(unsigned value);
    int setDataBitrate(unsigned value);
    void resetConfiguration();
    void applyConfigurationParameter(int key, const QVariant &value);

private:
    QTimer m_Timer;
    ScDevice m_Device;
    QByteArray rx_buffer;
    QByteArray tx_buffer;
    QVector<HANDLE> rx_events;
    QVector<HANDLE> tx_events;
    QVector<uint8_t> tx_events_available;
    QVector<uint8_t> tx_events_busy;
    QVector<OVERLAPPED> rx_ov;
    QVector<OVERLAPPED> tx_ov;
    can_regs m_Nominal, m_Data;
    uint32_t m_TsHiUs;
    uint32_t m_TsLoUs;
    int m_Urbs;
    uint8_t m_ChannelIndex;
    bool m_IsOnBus;
    bool m_Fd;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QCanBusDevice::CanBusStatus m_BusStatus;
#endif

};

QT_END_NAMESPACE
