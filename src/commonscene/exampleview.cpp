/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "exampleview.h"

#include <cmath>
#include <QMimeData>

#include "engraving/rw/xml.h"

#include "libmscore/masterscore.h"
#include "libmscore/engravingitem.h"
#include "libmscore/page.h"
#include "libmscore/system.h"
#include "libmscore/actionicon.h"
#include "libmscore/chord.h"
#include "libmscore/factory.h"

#include "commonscenetypes.h"

using namespace mu;
using namespace mu::engraving;

namespace Ms {
//---------------------------------------------------------
//   ExampleView
//---------------------------------------------------------

ExampleView::ExampleView(QWidget* parent)
    : QFrame(parent)
{
    _score = 0;
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    resetMatrix();
    _fgPixmap = nullptr;
    _fgColor  = Qt::white;

    if (notationConfiguration()->foregroundUseColor()) {
        _fgColor = notationConfiguration()->foregroundColor();
    } else {
        QString wallpaperPath = notationConfiguration()->foregroundWallpaperPath().toQString();

        _fgPixmap = new QPixmap(wallpaperPath);
        if (_fgPixmap == 0 || _fgPixmap->isNull()) {
            qDebug("no valid pixmap %s", qPrintable(wallpaperPath));
        }
    }
    // setup drag canvas state
    sm          = new QStateMachine(this);
    QState* stateActive = new QState;

    QState* s1 = new QState(stateActive);
    s1->setObjectName("example-normal");
    s1->assignProperty(this, "cursor", QCursor(Qt::ArrowCursor));

    QState* s = new QState(stateActive);
    s->setObjectName("example-drag");
    s->assignProperty(this, "cursor", QCursor(Qt::SizeAllCursor));
    QEventTransition* cl = new QEventTransition(this, QEvent::MouseButtonRelease);
    cl->setTargetState(s1);
    s->addTransition(cl);
    s1->addTransition(new DragTransitionExampleView(this));

    sm->addState(stateActive);
    stateActive->setInitialState(s1);
    sm->setInitialState(stateActive);

    sm->start();

    m_defaultScaling = 0.9 * uiConfiguration()->dpi() / DPI; // 90% of nominal
}

//---------------------------------------------------------
//   ~ExampleView
//---------------------------------------------------------

ExampleView::~ExampleView()
{
    if (_fgPixmap) {
        delete _fgPixmap;
    }
}

//---------------------------------------------------------
//   resetMatrix
//    used to reset scrolling in case time signature num or denom changed
//---------------------------------------------------------

void ExampleView::resetMatrix()
{
    double mag = m_defaultScaling;
    qreal _spatium = SPATIUM20 * mag;
    // example would normally be 10sp from top of page; this leaves 3sp margin above
    _matrix  = QTransform(mag, 0.0, 0.0, mag, _spatium, -_spatium * 7.0);
    imatrix  = _matrix.inverted();
}

void ExampleView::layoutChanged()
{
}

void ExampleView::dataChanged(const RectF&)
{
}

void ExampleView::updateAll()
{
    update();
}

void ExampleView::adjustCanvasPosition(const EngravingItem* /*el*/, bool /*playBack*/, int)
{
}

//---------------------------------------------------------
//   setScore
//---------------------------------------------------------

void ExampleView::setScore(Score* s)
{
    delete _score;
    _score = s;
    _score->addViewer(this);
    _score->setLayoutMode(LayoutMode::LINE);

    ScoreLoad sl;
    _score->doLayout();
    update();
}

void ExampleView::removeScore()
{
}

void ExampleView::changeEditElement(EngravingItem*)
{
}

void ExampleView::setDropRectangle(const RectF&)
{
}

void ExampleView::cmdAddSlur(Note* /*firstNote*/, Note* /*lastNote*/)
{
}

void ExampleView::drawBackground(mu::draw::Painter* p, const RectF& r) const
{
    if (_fgPixmap == 0 || _fgPixmap->isNull()) {
        p->fillRect(r, _fgColor);
    } else {
        p->drawTiledPixmap(r, *_fgPixmap, r.topLeft() - PointF(lrint(_matrix.dx()), lrint(_matrix.dy())));
    }
}

//---------------------------------------------------------
//   drawElements
//---------------------------------------------------------

void ExampleView::drawElements(mu::draw::Painter& painter, const QList<EngravingItem*>& el)
{
    for (EngravingItem* e : el) {
        e->itemDiscovered = 0;
        PointF pos(e->pagePos());
        painter.translate(pos);
        e->draw(&painter);
        painter.translate(-pos);
    }
}

//---------------------------------------------------------
//   paintEvent
//---------------------------------------------------------

void ExampleView::paintEvent(QPaintEvent* ev)
{
    if (_score) {
        mu::draw::Painter p(this, "exampleview");
        p.setAntialiasing(true);
        const RectF r = RectF::fromQRectF(ev->rect());

        drawBackground(&p, r);

        p.setWorldTransform(mu::Transform::fromQTransform(_matrix));
        QRectF fr = imatrix.mapRect(r.toQRectF());

        QRegion r1(r.toQRect());
        Page* page = _score->pages().front();
        QList<EngravingItem*> ell = page->items(RectF::fromQRectF(fr));
        std::stable_sort(ell.begin(), ell.end(), elementLessThan);
        drawElements(p, ell);
    }
    QFrame::paintEvent(ev);
}

//---------------------------------------------------------
//   dragEnterEvent
//---------------------------------------------------------

void ExampleView::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* d = event->mimeData();
    if (d->hasFormat(mu::commonscene::MIME_SYMBOL_FORMAT)) {
        event->acceptProposedAction();

        QByteArray a = d->data(mu::commonscene::MIME_SYMBOL_FORMAT);

// qDebug("ExampleView::dragEnterEvent Symbol: <%s>", a.data());

        XmlReader e(a);
        PointF dragOffset;
        Fraction duration;      // dummy
        ElementType type = EngravingItem::readType(e, &dragOffset, &duration);

        dragElement = Factory::createItem(type, _score->dummy());
        if (dragElement) {
            dragElement->resetExplicitParent();
            dragElement->read(e);
            dragElement->layout();
        }
        return;
    }
}

//---------------------------------------------------------
//   dragLeaveEvent
//---------------------------------------------------------

void ExampleView::dragLeaveEvent(QDragLeaveEvent*)
{
    if (dragElement) {
        delete dragElement;
        dragElement = 0;
    }
    setDropTarget(0);
}

//---------------------------------------------------------
//   moveElement
//---------------------------------------------------------

struct MoveContext
{
    PointF pos;
    Ms::Score* score = nullptr;
};

static void moveElement(void* data, EngravingItem* e)
{
    MoveContext* ctx = (MoveContext*)data;
    ctx->score->addRefresh(e->canvasBoundingRect());
    e->setPos(ctx->pos);
    ctx->score->addRefresh(e->canvasBoundingRect());
}

//---------------------------------------------------------
//   dragMoveEvent
//---------------------------------------------------------

void ExampleView::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();

    if (!dragElement || dragElement->isActionIcon()) {
        return;
    }

    PointF pos = PointF::fromQPointF(imatrix.map(QPointF(event->pos())));
    QList<EngravingItem*> el = elementsAt(pos);
    bool found = false;
    foreach (const EngravingItem* e, el) {
        if (e->type() == ElementType::NOTE) {
            setDropTarget(const_cast<EngravingItem*>(e));
            found = true;
            break;
        }
    }
    if (!found) {
        setDropTarget(0);
    }

    MoveContext ctx{ pos, _score };
    dragElement->scanElements(&ctx, moveElement, false);
    _score->update();
    return;
}

//---------------------------------------------------------
//   setDropTarget
//---------------------------------------------------------

void ExampleView::setDropTarget(const EngravingItem* el)
{
    if (dropTarget != el) {
        if (dropTarget) {
            dropTarget->setDropTarget(false);
            dropTarget = 0;
        }
        dropTarget = el;
        if (dropTarget) {
            dropTarget->setDropTarget(true);
        }
    }
    if (!dropAnchor.isNull()) {
        QRectF r;
        r.setTopLeft(dropAnchor.p1());
        r.setBottomRight(dropAnchor.p2());
        dropAnchor = QLineF();
    }
    if (dropRectangle.isValid()) {
        dropRectangle = QRectF();
    }
    update();
}

//---------------------------------------------------------
//   dropEvent
//---------------------------------------------------------

void ExampleView::dropEvent(QDropEvent* event)
{
    PointF pos = PointF::fromQPointF(imatrix.map(QPointF(event->pos())));

    if (!dragElement) {
        return;
    }
    if (dragElement->isActionIcon()) {
        delete dragElement;
        dragElement = 0;
        return;
    }
    foreach (EngravingItem* e, elementsAt(pos)) {
        if (e->type() == ElementType::NOTE) {
            ActionIcon* icon = static_cast<ActionIcon*>(dragElement);
            Chord* chord = static_cast<Note*>(e)->chord();
            emit beamPropertyDropped(chord, icon);
            switch (icon->actionType()) {
            case ActionIconType::BEAM_START:
                chord->setBeamMode(BeamMode::BEGIN);
                break;
            case ActionIconType::BEAM_MID:
                chord->setBeamMode(BeamMode::AUTO);
                break;
            case ActionIconType::BEAM_BEGIN_32:
                chord->setBeamMode(BeamMode::BEGIN32);
                break;
            case ActionIconType::BEAM_BEGIN_64:
                chord->setBeamMode(BeamMode::BEGIN64);
                break;
            default:
                break;
            }
            score()->doLayout();
            break;
        }
    }
    event->acceptProposedAction();
    delete dragElement;
    dragElement = 0;
    setDropTarget(0);
}

//---------------------------------------------------------
//   mousePressEvent
//---------------------------------------------------------

void ExampleView::mousePressEvent(QMouseEvent* event)
{
    startMove  = imatrix.map(QPointF(event->pos()));
    PointF pos = PointF::fromQPointF(imatrix.map(QPointF(event->pos())));

    foreach (EngravingItem* e, elementsAt(pos)) {
        if (e->type() == ElementType::NOTE) {
            emit noteClicked(static_cast<Note*>(e));
            break;
        }
    }
}

//---------------------------------------------------------
//   sizeHint
//---------------------------------------------------------

QSize ExampleView::sizeHint() const
{
    qreal mag = m_defaultScaling;
    qreal _spatium = SPATIUM20 * mag;
    // staff is 4sp tall with 3sp margin above; this leaves 3sp margin below
    qreal height = 10.0 * _spatium;
    if (score() && score()->pages().size() > 0) {
        height = score()->pages()[0]->tbbox().height() * mag + (6 * _spatium);
    }
    return QSize(1000 * mag, height);
}

//---------------------------------------------------------
//   dragExampleView
//     constrained scrolling ensuring that this ExampleView won't be moved past the borders of its QFrame
//---------------------------------------------------------

void ExampleView::dragExampleView(QMouseEvent* ev)
{
    QPoint d = ev->pos() - _matrix.map(startMove).toPoint();
    int dx   = d.x();
    if (dx == 0) {
        return;
    }

    constraintCanvas(&dx);

    // Perform the actual scrolling
    _matrix.setMatrix(_matrix.m11(), _matrix.m12(), _matrix.m13(), _matrix.m21(),
                      _matrix.m22(), _matrix.m23(), _matrix.dx() + dx, _matrix.dy(), _matrix.m33());
    imatrix = _matrix.inverted();
    scroll(dx, 0);
}

void DragTransitionExampleView::onTransition(QEvent* e)
{
    QStateMachine::WrappedEvent* we = static_cast<QStateMachine::WrappedEvent*>(e);
    QMouseEvent* me = static_cast<QMouseEvent*>(we->event());
    canvas->dragExampleView(me);
}

//---------------------------------------------------------
//   wheelEvent
//---------------------------------------------------------

void ExampleView::wheelEvent(QWheelEvent* event)
{
    QPoint pixelsScrolled = event->pixelDelta();
    QPoint stepsScrolled = event->angleDelta();
    int dx = 0, dy = 0;
    if (!pixelsScrolled.isNull()) {
        dx = pixelsScrolled.x();
        dy = pixelsScrolled.y();
    } else if (!stepsScrolled.isNull()) {
        dx = static_cast<qreal>(stepsScrolled.x()) * qMax(2, width() / 10) / 120;
        dy = static_cast<qreal>(stepsScrolled.y()) * qMax(2, height() / 10) / 120;
    }

    if (dx == 0) {
        if (dy == 0) {
            return;
        } else {
            dx = dy;
        }
    }

    constraintCanvas(&dx);

    _matrix.setMatrix(_matrix.m11(), _matrix.m12(), _matrix.m13(), _matrix.m21(),
                      _matrix.m22(), _matrix.m23(), _matrix.dx() + dx, _matrix.dy(), _matrix.m33());
    imatrix = _matrix.inverted();
    scroll(dx, 0);
}

//-----------------------------------------------------------------------------
//   constraintCanvas
//-----------------------------------------------------------------------------

void ExampleView::constraintCanvas(int* dxx)
{
    int dx = *dxx;

    Q_ASSERT(_score->pages().front()->system(0));   // should exist if doLayout ran

    // form rectangle bounding the system with a spatium margin and translate relative to view space
    qreal xstart = _score->pages().front()->system(0)->bbox().left() - SPATIUM20;
    qreal xend = _score->pages().front()->system(0)->bbox().right() + 2.0 * SPATIUM20;
    QRectF systemScaledViewRect(xstart * _matrix.m11(), 0, xend * _matrix.m11(), 0);
    systemScaledViewRect.translate(_matrix.dx(), 0);

    qreal frameWidth = static_cast<QFrame*>(this)->frameRect().width();

    // constrain the dx of scrolling so that this ExampleView won't be moved past the borders of its QFrame
    if (dx > 0) {
        // when moving right, ensure the left edge of systemScaledViewRect won't be right of frame's left edge
        if (systemScaledViewRect.left() + dx > 0) {
            dx = -systemScaledViewRect.left();
        }
    } else {
        // never move left if entire system already fits entirely within the frame
        if (systemScaledViewRect.width() < frameWidth) {
            dx = 0;
        }
        // when moving left, ensure the right edge of systemScaledViewRect won't be left of frame's right edge
        else if (systemScaledViewRect.right() + dx < frameWidth) {
            dx = frameWidth - systemScaledViewRect.right();
        }
    }

    *dxx = dx;
}
}
