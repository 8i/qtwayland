/****************************************************************************
**
** This file is part of QtCompositor**
**
** Copyright © 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
**
** Contact:  Nokia Corporation qt-info@nokia.com
**
** You may use this file under the terms of the BSD license as follows:
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**
** Redistributions of source code must retain the above copyright
** notice, this list of conditions and the following disclaimer.
**
** Redistributions in binary form must reproduce the above copyright
** notice, this list of conditions and the following disclaimer in the
** documentation and/or other materials provided with the distribution.
**
** Neither the name of Nokia Corporation and its Subsidiary(-ies) nor the
** names of its contributors may be used to endorse or promote products
** derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************/

#ifndef WLSELECTION_H
#define WLSELECTION_H

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QMimeData>
#include <wayland-server.h>

QT_BEGIN_NAMESPACE
class QSocketNotifier;
QT_END_NAMESPACE

namespace Wayland {

class Selection : public QObject
{
    Q_OBJECT

public:
    static Selection *instance();
    Selection();
    ~Selection();
    void create(struct wl_client *client, uint32_t id);
    void setRetainedSelection(bool enable);
    typedef void (*Watcher)(QMimeData*, void*);
    void setRetainedSelectionWatcher(Watcher func, void *param);

private slots:
    void onClientAdded(wl_client *client);
    void readFromClient(int fd);

private:
    static void destroySelection(struct wl_resource *resource, struct wl_client *client);
    static void selOffer(struct wl_client *client,
                         struct wl_selection *selection,
                         const char *type);
    static void selActivate(struct wl_client *client,
                            struct wl_selection *selection,
                            struct wl_input_device *device,
                            uint32_t time);
    static void selDestroy(struct wl_client *client, struct wl_selection *selection);
    static const struct wl_selection_interface selectionInterface;
    static void send(struct wl_client *client,
                     struct wl_selection_offer *offer,
                     const char *mime_type, int fd);
    static const struct wl_selection_offer_interface selectionOfferInterface;

    void retain();
    void finishReadFromClient(bool exhausted = false);

    QStringList m_offerList;
    struct wl_selection *m_currentSelection;
    struct wl_selection_offer *m_currentOffer;
    QMimeData m_retainedData;
    QSocketNotifier *m_retainedReadNotifier;
    QList<QSocketNotifier *> m_obsoleteRetainedReadNotifiers;
    int m_retainedReadIndex;
    QByteArray m_retainedReadBuf;
    struct wl_selection *m_retainedSelection;
    bool m_retainedSelectionEnabled;
    Watcher m_watchFunc;
    void *m_watchFuncParam;
};

}

#endif // WLSELECTION_H
