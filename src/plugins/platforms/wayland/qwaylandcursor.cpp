/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwaylandcursor.h"

#include "qwaylanddisplay.h"
#include "qwaylandinputdevice.h"
#include "qwaylandscreen.h"
#include "qwaylandshmbackingstore.h"

#include <QtGui/QImageReader>
#include <QDebug>

#define DATADIR "/usr/share"

static const struct pointer_image {
    const char *filename;
    int hotspot_x, hotspot_y;
} pointer_images[] = {
    /* FIXME: Half of these are wrong... */
    /* Qt::ArrowCursor */
    { DATADIR "/wayland/left_ptr.png",			10,  5 },
    /* Qt::UpArrowCursor */
    { DATADIR "/wayland/top_side.png",			18,  8 },
    /* Qt::CrossCursor */
    { DATADIR "/wayland/top_side.png",			18,  8 },
    /* Qt::WaitCursor */
    { DATADIR "/wayland/top_side.png",			18,  8 },
    /* Qt::IBeamCursor */
    { DATADIR "/wayland/xterm.png",			15, 15 },
    /* Qt::SizeVerCursor */
    { DATADIR "/wayland/top_side.png",			18,  8 },
    /* Qt::SizeHorCursor */
    { DATADIR "/wayland/bottom_left_corner.png",	 6, 30 },
    /* Qt::SizeBDiagCursor */
    { DATADIR "/wayland/bottom_right_corner.png",	28, 28 },
    /* Qt::SizeFDiagCursor */
    { DATADIR "/wayland/bottom_side.png",		16, 20 },
    /* Qt::SizeAllCursor */
    { DATADIR "/wayland/left_side.png",			10, 20 },
    /* Qt::BlankCursor */
    { DATADIR "/wayland/right_side.png",		30, 19 },
    /* Qt::SplitVCursor */
    { DATADIR "/wayland/sb_v_double_arrow.png",		15, 15 },
    /* Qt::SplitHCursor */
    { DATADIR "/wayland/sb_h_double_arrow.png",		15, 15 },
    /* Qt::PointingHandCursor */
    { DATADIR "/wayland/hand2.png",			14,  8 },
    /* Qt::ForbiddenCursor */
    { DATADIR "/wayland/top_right_corner.png",		26,  8 },
    /* Qt::WhatsThisCursor */
    { DATADIR "/wayland/top_right_corner.png",		26,  8 },
    /* Qt::BusyCursor */
    { DATADIR "/wayland/top_right_corner.png",		26,  8 },
    /* Qt::OpenHandCursor */
    { DATADIR "/wayland/hand1.png",			18, 11 },
    /* Qt::ClosedHandCursor */
    { DATADIR "/wayland/grabbing.png",			20, 17 },
    /* Qt::DragCopyCursor */
    { DATADIR "/wayland/dnd-copy.png",			13, 13 },
    /* Qt::DragMoveCursor */
    { DATADIR "/wayland/dnd-move.png",			13, 13 },
    /* Qt::DragLinkCursor */
    { DATADIR "/wayland/dnd-link.png",			13, 13 },
};

QWaylandCursor::QWaylandCursor(QWaylandScreen *screen)
    : mBuffer(0)
    , mDisplay(screen->display())
    , mSurface(0)
{
}

QWaylandCursor::~QWaylandCursor()
{
    if (mSurface)
        wl_surface_destroy(mSurface);

    delete mBuffer;
}

void QWaylandCursor::ensureSurface(const QSize &size)
{
    if (!mBuffer || mBuffer->size() != size) {
        delete mBuffer;
        mBuffer = new QWaylandShmBuffer(mDisplay, size,
                                        QImage::Format_ARGB32);
    }

    if (!mSurface)
        mSurface = mDisplay->createSurface(0);

    wl_surface_attach(mSurface, mBuffer->buffer(), 0, 0);
}

void QWaylandCursor::changeCursor(QCursor *cursor, QWindow *window)
{
    const struct pointer_image *p;

    if (window == NULL)
        return;

    p = NULL;
    bool isBitmap = false;

    switch (cursor->shape()) {
    case Qt::ArrowCursor:
        p = &pointer_images[cursor->shape()];
        break;
    case Qt::UpArrowCursor:
    case Qt::CrossCursor:
    case Qt::WaitCursor:
        break;
    case Qt::IBeamCursor:
        p = &pointer_images[cursor->shape()];
        break;
    case Qt::SizeVerCursor:	/* 5 */
    case Qt::SizeHorCursor:
    case Qt::SizeBDiagCursor:
    case Qt::SizeFDiagCursor:
    case Qt::SizeAllCursor:
    case Qt::BlankCursor:	/* 10 */
        break;
    case Qt::SplitVCursor:
    case Qt::SplitHCursor:
    case Qt::PointingHandCursor:
        p = &pointer_images[cursor->shape()];
        break;
    case Qt::ForbiddenCursor:
    case Qt::WhatsThisCursor:	/* 15 */
    case Qt::BusyCursor:
        break;
    case Qt::OpenHandCursor:
    case Qt::ClosedHandCursor:
    case Qt::DragCopyCursor:
    case Qt::DragMoveCursor: /* 20 */
    case Qt::DragLinkCursor:
        p = &pointer_images[cursor->shape()];
        break;

    case Qt::BitmapCursor:
        isBitmap = true;
        break;

    default:
        break;
    }

    if (!p && !isBitmap) {
        p = &pointer_images[0];
        qWarning("unhandled cursor %d", cursor->shape());
    }

    if (isBitmap && !cursor->pixmap().isNull()) {
        setupPixmapCursor(cursor);
    } else if (isBitmap && cursor->bitmap()) {
        qWarning("unsupported QBitmap cursor");
    } else {
        QImageReader reader(p->filename);
        if (!reader.canRead())
            return;
        ensureSurface(reader.size());
        reader.read(mBuffer->image());
        mDisplay->setCursor(mSurface, p->hotspot_x, p->hotspot_y);
    }
}

void QWaylandCursor::setupPixmapCursor(QCursor *cursor)
{
    if (!cursor) {
        delete mBuffer;
        mBuffer = 0;
        return;
    }
    ensureSurface(cursor->pixmap().size());
    QImage src = cursor->pixmap().toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < src.height(); ++y)
        qMemCopy(mBuffer->image()->scanLine(y), src.scanLine(y), src.bytesPerLine());
    mDisplay->setCursor(mSurface, cursor->hotSpot().x(), cursor->hotSpot().y());
}

void QWaylandDisplay::setCursor(wl_surface *surface, int32_t x, int32_t y)
{
    /* Qt doesn't tell us which input device we should set the cursor
     * for, so set it for all devices. */
    for (int i = 0; i < mInputDevices.count(); i++) {
        QWaylandInputDevice *inputDevice = mInputDevices.at(i);
        inputDevice->setCursor(surface, x, y);
    }
}

void QWaylandCursor::pointerEvent(const QMouseEvent &event)
{
    mLastPos = event.globalPos();
}

QPoint QWaylandCursor::pos() const
{
    return mLastPos;
}

void QWaylandCursor::setPos(const QPoint &pos)
{
    Q_UNUSED(pos);
    qWarning() << "QWaylandCursor::setPos: not implemented";
}
