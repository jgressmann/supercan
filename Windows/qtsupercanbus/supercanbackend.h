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

#pragma once

#include <QtSerialBus/qcanbusframe.h>
#include <QtSerialBus/qcanbusdevice.h>
#include <QtSerialBus/qcanbusdeviceinfo.h>

#include <QtCore/qvariant.h>
#include <QtCore/qvector.h>
#include <QtCore/qlist.h>
#include <QtCore/qtimer.h>

#ifndef _MSC_VER
    #error Please use a Microsoft compiler to build this project
#endif

#include <qt_windows.h>
#include <supercan_srv.h>
#include <supercan_winapi.h>
#import <supercan_srv.tlb> raw_interfaces_only


QT_BEGIN_NAMESPACE


class ComScope
{
public:
    ~ComScope()
    {
        Uninit();
    }

    ComScope()
        : m_Uninit(false)
        , m_Available(false)
    {
        Init();
    }

    Q_DISABLE_COPY_MOVE(ComScope);

    operator bool() const
    {
        return m_Available;
    }

    void Init()
    {
        Uninit();

        auto hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

        if (SUCCEEDED(hr)) {
            m_Available = true;
            m_Uninit = true;
        } else {
            m_Uninit = false;
            m_Available = RPC_E_CHANGED_MODE == hr;
        }
    }

    void Uninit()
    {
        if (m_Uninit) {
            m_Uninit = false;
            CoUninitialize();
        }

        m_Available = false;
    }

private:
    bool m_Uninit;
    bool m_Available;
};


class SuperCanBackend : public QCanBusDevice
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SuperCanBackend)
public:
    enum SuperCanConfigKey {
        DontConfigureKey = UserKey, // Don't attempt to configure the bus
        NominalSamplePoint,         // defaults to CiA recommendataion
        DataSamplePoint,            // defaults to CiA recommendataion
        NominalSjw,                 // defaults to CiA recommendataion
        DataSjw,                    // defaults to CiA recommendataion
    };
    Q_ENUM(SuperCanConfigKey)

public:
    ~SuperCanBackend() override;
    explicit SuperCanBackend(const QString &name, QObject *parent = nullptr);

    bool open() override;
    void close() override;

    bool writeFrame(const QCanBusFrame &newData) override;

    QString interpretErrorFrame(const QCanBusFrame &errorFrame) override;

    static bool canCreate(QString *errorReason);
    static QList<QCanBusDeviceInfo> interfaces();
    bool event(QEvent* ev) override;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
Q_SIGNALS:
    void busStatusChanged(QCanBusDevice::CanBusStatus status);
#endif


private slots:
    void expired();

private:
    bool busOn();
    void busOff();
    void busCleanup();
    inline bool deviceValid() const { return m_Initialized; }
    bool setBus(bool on);
    void resetConfiguration();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    void setBusStatus(QCanBusDevice::CanBusStatus status);
#endif
    static QCanBusFrame::TimeStamp convertTimestamp(quint64 t);
    void placeFrame(const QCanBusFrame& frame, quint32 index);
    bool init();
    void uninit();

private:
    QTimer m_Timer;
    ComScope m_Com;
    SuperCAN::ISuperCANDevicePtr m_ScDevice;
    SuperCAN::SuperCANDeviceData m_ScDeviceData;
    HANDLE m_RxRingFileHandle;
    HANDLE m_TxRingFileHandle;
    HANDLE m_RxRingEventHandle;
    HANDLE m_TxRingEventHandle;
    sc_can_mm_header* m_RxRingPtr;
    sc_can_mm_header* m_TxRingPtr;
    size_t m_RxRingElements;
    size_t m_TxRingElements;
    unsigned m_ScDeviceIndex;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QCanBusDevice::CanBusStatus m_BusStatus;
#endif
    bool m_IsOnBus;
    bool m_Initialized;
    bool m_ConfiguredBus;
    bool m_InitRequired;
    bool m_Echo;
};

QT_END_NAMESPACE
