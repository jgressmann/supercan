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



#include "supercanbackend.h"

#include <QtSerialBus/qcanbus.h>
#include <QtSerialBus/qcanbusdevice.h>
#include <QtSerialBus/qcanbusfactory.h>

#include <QtCore/qloggingcategory.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(QT_CANBUS_PLUGINS_SUPERCAN, "qt.canbus.plugins.SuperCAN")

class SuperCanBusPlugin : public QObject, public QCanBusFactoryV2
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QCanBusFactory" FILE "plugin.json")
    Q_INTERFACES(QCanBusFactoryV2)

public:
    SuperCanBusPlugin(QObject* parent = nullptr);
    QList<QCanBusDeviceInfo> availableDevices(QString *errorMessage) const override;
    QCanBusDevice *createDevice(const QString &interfaceName, QString *errorMessage) const override;

private:
    QString m_ErrorMessage;
};


SuperCanBusPlugin::SuperCanBusPlugin(QObject* parent)
    : QObject(parent)
{
    SuperCanBackend::canCreate(&m_ErrorMessage);
}

QList<QCanBusDeviceInfo> SuperCanBusPlugin::availableDevices(QString *errorMessage) const
{
    if (m_ErrorMessage.size()) {
        if (errorMessage) {
            *errorMessage = m_ErrorMessage;
        }

        return {};
    }


    return SuperCanBackend::interfaces();
}

QCanBusDevice * SuperCanBusPlugin::createDevice(const QString &interfaceName, QString *errorMessage) const
{
    if (m_ErrorMessage.size()) {
        if (errorMessage) {
            *errorMessage = m_ErrorMessage;
        }

        return nullptr;
    }

    return new SuperCanBackend(interfaceName);
}

QT_END_NAMESPACE

#include "main.moc"
