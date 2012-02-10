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

#include "qwaylandinputdevice.h"

#include "qwaylandintegration.h"
#include "qwaylandwindow.h"
#include "qwaylandbuffer.h"
#include "qwaylanddatadevicemanager.h"
#include "qwaylandtouch.h"

#include <QtGui/private/qpixmap_raster_p.h>
#include <QtGui/QPlatformWindow>
#include <QDebug>

#include <unistd.h>
#include <fcntl.h>

#include <QtGui/QGuiApplication>

#ifndef QT_NO_WAYLAND_XKB
#include <X11/extensions/XKBcommon.h>
#include <X11/keysym.h>
#endif

QWaylandInputDevice::QWaylandInputDevice(QWaylandDisplay *display,
					 uint32_t id)
    : mQDisplay(display)
    , mDisplay(display->wl_display())
    , mTransferDevice(0)
    , mPointerFocus(0)
    , mKeyboardFocus(0)
    , mTouchFocus(0)
    , mButtons(0)
{
    mInputDevice = static_cast<struct wl_input_device *>
            (wl_display_bind(mDisplay,id,&wl_input_device_interface));
    wl_input_device_add_listener(mInputDevice,
				 &inputDeviceListener,
				 this);
    wl_input_device_set_user_data(mInputDevice, this);

#ifndef QT_NO_WAYLAND_XKB
    struct xkb_rule_names names;
    names.rules = "evdev";
    names.model = "pc105";
    names.layout = "us";
    names.variant = "";
    names.options = "";

    mXkb = xkb_compile_keymap_from_rules(&names);
#endif

    if (mQDisplay->dndSelectionHandler()) {
        mTransferDevice = mQDisplay->dndSelectionHandler()->getDataDevice(this);
    }

    mTouchDevice = new QTouchDevice;
    mTouchDevice->setType(QTouchDevice::TouchScreen);
    mTouchDevice->setCapabilities(QTouchDevice::Position);
    QWindowSystemInterface::registerTouchDevice(mTouchDevice);
}

void QWaylandInputDevice::handleWindowDestroyed(QWaylandWindow *window)
{
    if (window == mPointerFocus)
        mPointerFocus = 0;
    if (window == mKeyboardFocus)
        mKeyboardFocus = 0;
}

void QWaylandInputDevice::setTransferDevice(struct wl_data_device *device)
{
   mTransferDevice =  device;
}

struct wl_data_device *QWaylandInputDevice::transferDevice() const
{
    Q_ASSERT(mTransferDevice);
    return mTransferDevice;
}

void QWaylandInputDevice::inputHandleMotion(void *data,
					    struct wl_input_device *input_device,
					    uint32_t time,
					    int32_t x, int32_t y,
					    int32_t surface_x, int32_t surface_y)
{
    Q_UNUSED(input_device);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    QWaylandWindow *window = inputDevice->mPointerFocus;

    if (window == NULL) {
	/* We destroyed the pointer focus surface, but the server
	 * didn't get the message yet. */
	return;
    }

    inputDevice->mSurfacePos = QPoint(surface_x, surface_y);
    inputDevice->mGlobalPos = QPoint(x, y);
    inputDevice->mTime = time;
    QWindowSystemInterface::handleMouseEvent(window->window(),
					     time,
					     inputDevice->mSurfacePos,
					     inputDevice->mGlobalPos,
                                             inputDevice->mButtons);
}

void QWaylandInputDevice::inputHandleButton(void *data,
					    struct wl_input_device *input_device,
					    uint32_t time, uint32_t button, uint32_t state)
{
    Q_UNUSED(input_device);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    QWaylandWindow *window = inputDevice->mPointerFocus;
    Qt::MouseButton qt_button;

    if (window == NULL) {
	/* We destroyed the pointer focus surface, but the server
	 * didn't get the message yet. */
	return;
    }

    switch (button) {
    case 272:
	qt_button = Qt::LeftButton;
	break;
    case 273:
	qt_button = Qt::RightButton;
	break;
    case 274:
	qt_button = Qt::MiddleButton;
	break;
    default:
	return;
    }

    if (state)
	inputDevice->mButtons |= qt_button;
    else
	inputDevice->mButtons &= ~qt_button;

    inputDevice->mTime = time;
    QWindowSystemInterface::handleMouseEvent(window->window(),
					     time,
					     inputDevice->mSurfacePos,
					     inputDevice->mGlobalPos,
					     inputDevice->mButtons);
}

#ifndef QT_NO_WAYLAND_XKB
static Qt::KeyboardModifiers translateModifiers(int s)
{
    const uchar qt_alt_mask = XKB_COMMON_MOD1_MASK;
    const uchar qt_meta_mask = XKB_COMMON_MOD4_MASK;

    Qt::KeyboardModifiers ret = 0;
    if (s & XKB_COMMON_SHIFT_MASK)
	ret |= Qt::ShiftModifier;
    if (s & XKB_COMMON_CONTROL_MASK)
	ret |= Qt::ControlModifier;
    if (s & qt_alt_mask)
	ret |= Qt::AltModifier;
    if (s & qt_meta_mask)
	ret |= Qt::MetaModifier;

    return ret;
}

static uint32_t translateKey(uint32_t sym, char *string, size_t size)
{
    Q_UNUSED(size);
    string[0] = '\0';

    switch (sym) {
    case XK_Escape:		return Qt::Key_Escape;
    case XK_Tab:		return Qt::Key_Tab;
    case XK_ISO_Left_Tab:	return Qt::Key_Backtab;
    case XK_BackSpace:		return Qt::Key_Backspace;
    case XK_Return:		return Qt::Key_Return;
    case XK_Insert:		return Qt::Key_Insert;
    case XK_Delete:		return Qt::Key_Delete;
    case XK_Clear:		return Qt::Key_Delete;
    case XK_Pause:		return Qt::Key_Pause;
    case XK_Print:		return Qt::Key_Print;

    case XK_Home:		return Qt::Key_Home;
    case XK_End:		return Qt::Key_End;
    case XK_Left:		return Qt::Key_Left;
    case XK_Up:			return Qt::Key_Up;
    case XK_Right:		return Qt::Key_Right;
    case XK_Down:		return Qt::Key_Down;
    case XK_Prior:		return Qt::Key_PageUp;
    case XK_Next:		return Qt::Key_PageDown;

    case XK_Shift_L:		return Qt::Key_Shift;
    case XK_Shift_R:		return Qt::Key_Shift;
    case XK_Shift_Lock:		return Qt::Key_Shift;
    case XK_Control_L:		return Qt::Key_Control;
    case XK_Control_R:		return Qt::Key_Control;
    case XK_Meta_L:		return Qt::Key_Meta;
    case XK_Meta_R:		return Qt::Key_Meta;
    case XK_Alt_L:		return Qt::Key_Alt;
    case XK_Alt_R:		return Qt::Key_Alt;
    case XK_Caps_Lock:		return Qt::Key_CapsLock;
    case XK_Num_Lock:		return Qt::Key_NumLock;
    case XK_Scroll_Lock:	return Qt::Key_ScrollLock;
    case XK_Super_L:		return Qt::Key_Super_L;
    case XK_Super_R:		return Qt::Key_Super_R;
    case XK_Menu:		return Qt::Key_Menu;

    default:
	string[0] = sym;
	string[1] = '\0';
	return toupper(sym);
    }
}
#endif

void QWaylandInputDevice::inputHandleKey(void *data,
					 struct wl_input_device *input_device,
					 uint32_t time, uint32_t key, uint32_t state)
{
    Q_UNUSED(input_device);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    QWaylandWindow *window = inputDevice->mKeyboardFocus;
#ifndef QT_NO_WAYLAND_XKB
    uint32_t code, sym, level;
    Qt::KeyboardModifiers modifiers;
    QEvent::Type type;
    char s[2];

    if (window == NULL) {
	/* We destroyed the keyboard focus surface, but the server
	 * didn't get the message yet. */
	return;
    }

    code = key + inputDevice->mXkb->min_key_code;

    level = 0;
    if (inputDevice->mModifiers & Qt::ShiftModifier &&
	XkbKeyGroupWidth(inputDevice->mXkb, code, 0) > 1)
	level = 1;

    sym = XkbKeySymEntry(inputDevice->mXkb, code, level, 0);

    modifiers = translateModifiers(inputDevice->mXkb->map->modmap[code]);

    if (state) {
	inputDevice->mModifiers |= modifiers;
	type = QEvent::KeyPress;
    } else {
	inputDevice->mModifiers &= ~modifiers;
	type = QEvent::KeyRelease;
    }

    sym = translateKey(sym, s, sizeof s);

    if (window) {
        QWindowSystemInterface::handleKeyEvent(window->window(),
                                               time, type, sym,
                                               inputDevice->mModifiers,
                                               QString::fromLatin1(s));
    }
#else
    // Generic fallback for single hard keys: Assume 'key' is a Qt key code.
    if (window) {
        QWindowSystemInterface::handleKeyEvent(window->window(),
                                               time, state ? QEvent::KeyPress : QEvent::KeyRelease,
                                               key + 8, // qt-compositor substracts 8 for some reason
                                               inputDevice->mModifiers);
    }
#endif
}

void QWaylandInputDevice::inputHandlePointerFocus(void *data,
						  struct wl_input_device *input_device,
						  uint32_t time, struct wl_surface *surface,
						  int32_t x, int32_t y, int32_t sx, int32_t sy)
{
    Q_UNUSED(input_device);
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(sx);
    Q_UNUSED(sy);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    QWaylandWindow *window;

    if (inputDevice->mPointerFocus) {
	window = inputDevice->mPointerFocus;
	QWindowSystemInterface::handleLeaveEvent(window->window());
	inputDevice->mPointerFocus = NULL;
    }

    if (surface) {
	window = (QWaylandWindow *) wl_surface_get_user_data(surface);
	QWindowSystemInterface::handleEnterEvent(window->window());
	inputDevice->mPointerFocus = window;
    }

    inputDevice->mTime = time;
}

void QWaylandInputDevice::inputHandleKeyboardFocus(void *data,
						   struct wl_input_device *input_device,
						   uint32_t time,
						   struct wl_surface *surface,
						   struct wl_array *keys)
{
    Q_UNUSED(input_device);
    Q_UNUSED(time);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    QWaylandWindow *window;

    inputDevice->mModifiers = 0;

#ifndef QT_NO_WAYLAND_XKB
    uint32_t *k, *end;
    uint32_t code;

    end = (uint32_t *) ((char *) keys->data + keys->size);
    for (k = (uint32_t *) keys->data; k < end; k++) {
	code = *k + inputDevice->mXkb->min_key_code;
	inputDevice->mModifiers |=
	    translateModifiers(inputDevice->mXkb->map->modmap[code]);
    }
#else
    Q_UNUSED(keys);
#endif

    if (surface) {
	window = (QWaylandWindow *) wl_surface_get_user_data(surface);
	inputDevice->mKeyboardFocus = window;
        inputDevice->mQDisplay->setLastKeyboardFocusInputDevice(inputDevice);
	QWindowSystemInterface::handleWindowActivated(window->window());
    } else {
	inputDevice->mKeyboardFocus = NULL;
        inputDevice->mQDisplay->setLastKeyboardFocusInputDevice(0);
	QWindowSystemInterface::handleWindowActivated(0);
    }
}

void QWaylandInputDevice::inputHandleTouchDown(void *data,
                                               struct wl_input_device *wl_input_device,
                                               uint32_t time,
                                               struct wl_surface *surface,
                                               int id,
                                               int x,
                                               int y)
{
    Q_UNUSED(wl_input_device);
    Q_UNUSED(time);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    inputDevice->mTouchFocus = static_cast<QWaylandWindow *>(wl_surface_get_user_data(surface));
    inputDevice->handleTouchPoint(id, x, y, Qt::TouchPointPressed);
}

void QWaylandInputDevice::inputHandleTouchUp(void *data,
                                             struct wl_input_device *wl_input_device,
                                             uint32_t time,
                                             int id)
{
    Q_UNUSED(wl_input_device);
    Q_UNUSED(time);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    inputDevice->mTouchFocus = 0;
    inputDevice->handleTouchPoint(id, 0, 0, Qt::TouchPointReleased);
}

void QWaylandInputDevice::inputHandleTouchMotion(void *data,
                                                 struct wl_input_device *wl_input_device,
                                                 uint32_t time,
                                                 int id,
                                                 int x,
                                                 int y)
{
    Q_UNUSED(wl_input_device);
    Q_UNUSED(time);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    inputDevice->handleTouchPoint(id, x, y, Qt::TouchPointMoved);
}

void QWaylandInputDevice::handleTouchPoint(int id, int x, int y, Qt::TouchPointState state)
{
    QWindowSystemInterface::TouchPoint tp;

    // Find out the coordinates for Released events.
    bool coordsOk = false;
    if (state == Qt::TouchPointReleased)
        for (int i = 0; i < mPrevTouchPoints.count(); ++i)
            if (mPrevTouchPoints.at(i).id == id) {
                tp.area = mPrevTouchPoints.at(i).area;
                coordsOk = true;
                break;
            }

    if (!coordsOk) {
        // x and y are surface relative.
        // We need a global (screen) position.
        QWaylandWindow *win = mTouchFocus;

        //is it possible that mTouchFocus is null;
        if (!win)
            win = mPointerFocus;
        if (!win)
            win = mKeyboardFocus;
        if (!win || !win->window())
            return;

        tp.area = QRectF(0, 0, 8, 8);
        tp.area.moveCenter(win->window()->mapToGlobal(QPoint(x, y)));
    }

    tp.state = state;
    tp.id = id;
    tp.pressure = tp.state == Qt::TouchPointReleased ? 0 : 1;
    mTouchPoints.append(tp);
}

void QWaylandInputDevice::inputHandleTouchFrame(void *data, struct wl_input_device *wl_input_device)
{
    Q_UNUSED(wl_input_device);
    QWaylandInputDevice *inputDevice = (QWaylandInputDevice *) data;
    inputDevice->handleTouchFrame();
}

void QWaylandInputDevice::handleTouchFrame()
{
    // Copy all points, that are in the previous but not in the current list, as stationary.
    for (int i = 0; i < mPrevTouchPoints.count(); ++i) {
        const QWindowSystemInterface::TouchPoint &prevPoint(mPrevTouchPoints.at(i));
        if (prevPoint.state == Qt::TouchPointReleased)
            continue;
        bool found = false;
        for (int j = 0; j < mTouchPoints.count(); ++j)
            if (mTouchPoints.at(j).id == prevPoint.id) {
                found = true;
                break;
            }
        if (!found) {
            QWindowSystemInterface::TouchPoint p = prevPoint;
            p.state = Qt::TouchPointStationary;
            mTouchPoints.append(p);
        }
    }

    if (mTouchPoints.isEmpty()) {
        mPrevTouchPoints.clear();
        return;
    }

    QWindowSystemInterface::handleTouchEvent(0, mTouchDevice, mTouchPoints);

    bool allReleased = true;
    for (int i = 0; i < mTouchPoints.count(); ++i)
        if (mTouchPoints.at(i).state != Qt::TouchPointReleased) {
            allReleased = false;
            break;
        }

    mPrevTouchPoints = mTouchPoints;
    mTouchPoints.clear();

    if (allReleased) {
        QWindowSystemInterface::handleTouchEvent(0, mTouchDevice, mTouchPoints);
        mPrevTouchPoints.clear();
    }
}

void QWaylandInputDevice::inputHandleTouchCancel(void *data, struct wl_input_device *wl_input_device)
{
    Q_UNUSED(wl_input_device);
    QWaylandInputDevice *self = static_cast<QWaylandInputDevice *>(data);

    self->mPrevTouchPoints.clear();
    self->mTouchPoints.clear();

    QWaylandTouchExtension *touchExt = self->mQDisplay->touchExtension();
    if (touchExt)
        touchExt->touchCanceled();

    QWindowSystemInterface::handleTouchCancelEvent(0, self->mTouchDevice);
}

const struct wl_input_device_listener QWaylandInputDevice::inputDeviceListener = {
    QWaylandInputDevice::inputHandleMotion,
    QWaylandInputDevice::inputHandleButton,
    QWaylandInputDevice::inputHandleKey,
    QWaylandInputDevice::inputHandlePointerFocus,
    QWaylandInputDevice::inputHandleKeyboardFocus,
    QWaylandInputDevice::inputHandleTouchDown,
    QWaylandInputDevice::inputHandleTouchUp,
    QWaylandInputDevice::inputHandleTouchMotion,
    QWaylandInputDevice::inputHandleTouchFrame,
    QWaylandInputDevice::inputHandleTouchCancel
};

void QWaylandInputDevice::attach(QWaylandBuffer *buffer, int x, int y)
{
    wl_input_device_attach(mInputDevice, mTime, buffer->buffer(), x, y);
}
