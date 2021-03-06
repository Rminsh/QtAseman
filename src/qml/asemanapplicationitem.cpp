/*
    Copyright (C) 2017 Aseman Team
    http://aseman.co

    AsemanQtTools is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    AsemanQtTools is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "asemanapplicationitem.h"
#include "asemandevices.h"
#include "asemannetworkproxy.h"
#include "asemantools.h"
#include "qtsingleapplication/qtlocalpeer.h"
#ifdef Q_OS_ANDROID
#include "asemanjavalayer.h"
#endif

#include <QDir>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QStandardPaths>
#include <QPointer>
#include <QQmlEngine>
#include <QGuiApplication>

#ifdef Q_OS_ANDROID
#include <QtAndroid>
#endif

static AsemanApplicationItem *aseman_app_singleton = 0;
static QSet<AsemanApplicationItem*> aseman_app_objects;

bool AsemanApplicationItem::aseman_app_inited = AsemanApplicationItem::aseman_app_init();

class AsemanApplicationItemPrivate
{
public:
    QPointer<QQmlEngine> engine;
    QList<QObject*> items;

    static QtLocalPeer *peer;
    QPointer<AsemanNetworkProxy> proxy;
};

QtLocalPeer *AsemanApplicationItemPrivate::peer = 0;


AsemanApplicationItem::AsemanApplicationItem(QQmlEngine *engine) :
    AsemanApplicationItem()
{
    p->engine = engine;
}

AsemanApplicationItem::AsemanApplicationItem() :
    AsemanApplication()
{
    p = new AsemanApplicationItemPrivate;
}

bool AsemanApplicationItem::aseman_app_init()
{
    if(aseman_app_inited)
        return true;

#ifndef FORCE_ASEMAN_DENSITY
#ifdef Q_OS_ANDROID
    const bool nexus5X = (AsemanJavaLayer::instance()->deviceName() == "LGE Nexus 5X");
    if(!nexus5X)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    if(!QGuiApplication::testAttribute(Qt::AA_EnableHighDpiScaling) && qgetenv("QT_SCALE_FACTOR").isNull() && qgetenv("ASEMAN_SCALE_FACTOR").isNull())
    {
        qreal ratio = AsemanJavaLayer::instance()->density()*(AsemanDevices::isTablet()? 1.28 : 1);
        if(nexus5X)
        {
            qreal newRatio = qRound((ratio*20) + 0.01)/20.0;
            qputenv("QT_SCALE_FACTOR",QByteArray::number(newRatio));
            qputenv("ASEMAN_SCALE_FACTOR",QByteArray::number(ratio/newRatio));
        }
        else
            qputenv("QT_SCALE_FACTOR",QByteArray::number(ratio));
    }
#endif
#endif
    return true;
}

void AsemanApplicationItem::setApplicationId(const QString &applicationId)
{
    if(AsemanApplicationItemPrivate::peer && AsemanApplicationItemPrivate::peer->applicationId() == applicationId)
        return;
    if(AsemanApplicationItemPrivate::peer)
        delete AsemanApplicationItemPrivate::peer;

    AsemanApplicationItemPrivate::peer = 0;
    if(!applicationId.isEmpty())
    {
        AsemanApplicationItemPrivate::peer = new QtLocalPeer(0, applicationId);
        for(AsemanApplicationItem *app: aseman_app_objects)
            connect(AsemanApplicationItemPrivate::peer, &QtLocalPeer::messageReceived, app, &AsemanApplicationItem::messageReceived);
    }

    for(AsemanApplicationItem *app: aseman_app_objects)
    {
        Q_EMIT app->applicationIdChanged();
        Q_EMIT app->isRunningChanged();
    }
}

QString AsemanApplicationItem::applicationId()
{
    if(AsemanApplicationItemPrivate::peer)
        return AsemanApplicationItemPrivate::peer->applicationId();
    else
        return QString();
}

bool AsemanApplicationItem::isRunning()
{
    if(AsemanApplicationItemPrivate::peer)
        return AsemanApplicationItemPrivate::peer->isClient();

    return false;
}

void AsemanApplicationItem::sendMessage(const QString &msg)
{
    if(AsemanApplicationItemPrivate::peer)
        AsemanApplicationItemPrivate::peer->sendMessage(msg, 5000);
}

#ifdef QT_QML_LIB
QVariantMap AsemanApplicationItem::requestPermissions(QStringList persmissions, QJSValue callback)
{
    QVariantMap _res;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
#ifdef Q_OS_ANDROID
    auto c_callback = [callback](const QtAndroid::PermissionResultMap &res) -> QVariantMap {
        QVariantMap map;

        QHashIterator<QString, QtAndroid::PermissionResult> i(res);
        while(i.hasNext())
        {
            i.next();
            map[i.key()] = (i.value() == QtAndroid::PermissionResult::Granted);
        }

        return map;
    };

    for(QString pr: persmissions)
        if(QtAndroid::checkPermission(pr) == QtAndroid::PermissionResult::Granted)
        {
            persmissions.removeAll(pr);
            _res[pr] = true;
        }

    if(persmissions.count())
    {
        if(callback.isCallable())
            QtAndroid::requestPermissions(persmissions, [callback, c_callback, _res, this](const QtAndroid::PermissionResultMap &res){
                QVariantMap map = c_callback(res);
                map.unite(_res);

                QJSValue callbackCopy = callback;
                if(p->engine)
                    callbackCopy.call(QJSValueList() << p->engine->toScriptValue<QVariant>(map));
            });
        else
            _res.unite(c_callback( QtAndroid::requestPermissionsSync(persmissions) ));
    }
    else
    {
        if(callback.isCallable())
        {
            if(p->engine)
                callback.call(QJSValueList() << p->engine->toScriptValue<QVariant>(_res));
        }
    }
#endif
#endif
    Q_UNUSED(persmissions)
    Q_UNUSED(callback)
    return _res;
}
#endif

AsemanNetworkProxy *AsemanApplicationItem::proxy() const
{
    return p->proxy;
}

void AsemanApplicationItem::setProxy(AsemanNetworkProxy *proxy)
{
    if(p->proxy == proxy)
        return;

    if(p->proxy)
        delete p->proxy;

    p->proxy = proxy;
    Q_EMIT proxyChanged();
}

QQmlListProperty<QObject> AsemanApplicationItem::items()
{
    return QQmlListProperty<QObject>(this, &p->items, QQmlListProperty<QObject>::AppendFunction(append),
                                                      QQmlListProperty<QObject>::CountFunction(count),
                                                      QQmlListProperty<QObject>::AtFunction(at),
                                     QQmlListProperty<QObject>::ClearFunction(clear) );
}

QList<QObject *> AsemanApplicationItem::itemsList() const
{
    return p->items;
}

void AsemanApplicationItem::append(QQmlListProperty<QObject> *p, QObject *v)
{
    AsemanApplicationItem *aobj = static_cast<AsemanApplicationItem*>(p->object);
    aobj->p->items.append(v);
    Q_EMIT aobj->itemsChanged();
}

int AsemanApplicationItem::count(QQmlListProperty<QObject> *p)
{
    AsemanApplicationItem *aobj = static_cast<AsemanApplicationItem*>(p->object);
    return aobj->p->items.count();
}

QObject *AsemanApplicationItem::at(QQmlListProperty<QObject> *p, int idx)
{
    AsemanApplicationItem *aobj = static_cast<AsemanApplicationItem*>(p->object);
    return aobj->p->items.at(idx);
}

void AsemanApplicationItem::clear(QQmlListProperty<QObject> *p)
{
    AsemanApplicationItem *aobj = static_cast<AsemanApplicationItem*>(p->object);
    aobj->p->items.clear();
    Q_EMIT aobj->itemsChanged();
}

AsemanApplicationItem::~AsemanApplicationItem()
{
    aseman_app_objects.remove(this);
    if(aseman_app_singleton == this)
    {
        if(AsemanApplicationItemPrivate::peer)
        {
            delete AsemanApplicationItemPrivate::peer;
            AsemanApplicationItemPrivate::peer = 0;
        }
        aseman_app_singleton = 0;
    }

    delete p;
}
