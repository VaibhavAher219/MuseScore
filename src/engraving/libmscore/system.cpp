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

/**
 \file
 Implementation of classes SysStaff and System.
*/

#include "system.h"

#include "style/style.h"
#include "rw/xml.h"
#include "layout/layoutcontext.h"

#include "factory.h"
#include "measure.h"
#include "segment.h"
#include "score.h"
#include "sig.h"
#include "key.h"
#include "clef.h"
#include "text.h"
#include "navigate.h"
#include "select.h"
#include "staff.h"
#include "part.h"
#include "page.h"
#include "bracket.h"
#include "mscore.h"
#include "barline.h"
#include "system.h"
#include "box.h"
#include "chordrest.h"
#include "iname.h"
#include "spanner.h"
#include "spacer.h"
#include "systemdivider.h"
#include "textframe.h"
#include "stafflines.h"
#include "bracketItem.h"

#include "log.h"

using namespace mu;
using namespace mu::engraving;

namespace Ms {
//---------------------------------------------------------
//   ~SysStaff
//---------------------------------------------------------

SysStaff::~SysStaff()
{
    qDeleteAll(instrumentNames);
}

//---------------------------------------------------------
//   yBottom
//---------------------------------------------------------

qreal SysStaff::yBottom() const
{
    return skyline().south().valid() ? skyline().south().max() : _height;
}

//---------------------------------------------------------
//   saveLayout
//---------------------------------------------------------

void SysStaff::saveLayout()
{
    _height =  bbox().height();
    _yPos = bbox().y();
}

//---------------------------------------------------------
//   saveLayout
//---------------------------------------------------------

void SysStaff::restoreLayout()
{
    bbox().setY(_yPos);
    bbox().setHeight(_height);
}

//---------------------------------------------------------
//   System
//---------------------------------------------------------

System::System(Page* parent)
    : EngravingItem(ElementType::SYSTEM, parent)
{
}

//---------------------------------------------------------
//   ~System
//---------------------------------------------------------

System::~System()
{
    for (SpannerSegment* ss : spannerSegments()) {
        if (ss->system() == this) {
            ss->resetExplicitParent();
        }
    }
    for (MeasureBase* mb : measures()) {
        if (mb->system() == this) {
            mb->resetExplicitParent();
        }
    }
    qDeleteAll(_staves);
    qDeleteAll(_brackets);
    delete _systemDividerLeft;
    delete _systemDividerRight;
}

void System::moveToPage(Page* parent)
{
    setParent(parent);
}

//---------------------------------------------------------
///   clear
///   Clear layout of System
//---------------------------------------------------------

void System::clear()
{
    for (MeasureBase* mb : measures()) {
        if (mb->system() == this) {
            mb->resetExplicitParent();
        }
    }
    ml.clear();
    for (SpannerSegment* ss : qAsConst(_spannerSegments)) {
        if (ss->system() == this) {
            ss->resetExplicitParent();             // assume parent() is System
        }
    }
    _spannerSegments.clear();
    // _systemDividers are reused
}

//---------------------------------------------------------
//   appendMeasure
//---------------------------------------------------------

void System::appendMeasure(MeasureBase* mb)
{
    Q_ASSERT(!mb->isMeasure() || !(score()->styleB(Sid::createMultiMeasureRests) && toMeasure(mb)->hasMMRest()));
    mb->setParent(this);
    ml.push_back(mb);
}

//---------------------------------------------------------
//   removeMeasure
//---------------------------------------------------------

void System::removeMeasure(MeasureBase* mb)
{
    ml.erase(std::remove(ml.begin(), ml.end(), mb), ml.end());
    if (mb->system() == this) {
        mb->resetExplicitParent();
    }
}

//---------------------------------------------------------
//   removeLastMeasure
//---------------------------------------------------------

void System::removeLastMeasure()
{
    if (ml.empty()) {
        return;
    }
    MeasureBase* mb = ml.back();
    ml.pop_back();
    if (mb->system() == this) {
        mb->resetExplicitParent();
    }
}

//---------------------------------------------------------
//   vbox
//    a system can only contain one vertical frame
//---------------------------------------------------------

Box* System::vbox() const
{
    if (!ml.empty()) {
        if (ml[0]->isVBox() || ml[0]->isTBox()) {
            return toBox(ml[0]);
        }
    }
    return 0;
}

//---------------------------------------------------------
//   insertStaff
//---------------------------------------------------------

SysStaff* System::insertStaff(int idx)
{
    SysStaff* staff = new SysStaff;
    if (idx) {
        // HACK: guess position
        staff->bbox().setY(_staves[idx - 1]->y() + 6 * spatium());
    }
    _staves.insert(idx, staff);
    return staff;
}

//---------------------------------------------------------
//   removeStaff
//---------------------------------------------------------

void System::removeStaff(int idx)
{
    _staves.takeAt(idx);
}

//---------------------------------------------------------
//   adjustStavesNumber
//---------------------------------------------------------

void System::adjustStavesNumber(int nstaves)
{
    for (int i = _staves.size(); i < nstaves; ++i) {
        insertStaff(i);
    }
    const int dn = _staves.size() - nstaves;
    for (int i = 0; i < dn; ++i) {
        removeStaff(_staves.size() - 1);
    }
}

//---------------------------------------------------------
//   systemNamesWidth
//---------------------------------------------------------

qreal System::systemNamesWidth()
{
    qreal instrumentNameOffset = score()->styleMM(Sid::instrumentNameOffset);

    qreal namesWidth = 0.0;

    for (const Part* part : score()->parts()) {
        for (int staffIdx = firstSysStaffOfPart(part); staffIdx <= lastSysStaffOfPart(part); ++staffIdx) {
            SysStaff* staff = this->staff(staffIdx);
            if (!staff) {
                continue;
            }

            for (InstrumentName* name : qAsConst(staff->instrumentNames)) {
                name->layout();
                qreal width = name->width() + instrumentNameOffset;
                namesWidth = qMax(namesWidth, width);
            }
        }
    }

    return namesWidth;
}

//---------------------------------------------------------
//   layoutBrackets
//---------------------------------------------------------

qreal System::layoutBrackets(const LayoutContext& ctx)
{
    int nstaves  = _staves.size();
    int columns = getBracketsColumnsCount();

#if (!defined (_MSCVER) && !defined (_MSC_VER))
    qreal bracketWidth[columns];
#else
    // MSVC does not support VLA. Replace with std::vector. If profiling determines that the
    //    heap allocation is slow, an optimization might be used.
    std::vector<qreal> bracketWidth(columns);
#endif
    for (int i = 0; i < columns; ++i) {
        bracketWidth[i] = 0.0;
    }

    QList<Bracket*> bl;
    bl.swap(_brackets);

    for (int staffIdx = 0; staffIdx < nstaves; ++staffIdx) {
        Staff* s = score()->staff(staffIdx);
        for (int i = 0; i < columns; ++i) {
            for (auto bi : s->brackets()) {
                if (bi->column() != i || bi->bracketType() == BracketType::NO_BRACKET) {
                    continue;
                }
                Bracket* b = createBracket(ctx, bi, i, staffIdx, bl, this->firstMeasure());
                if (b != nullptr) {
                    bracketWidth[i] = qMax(bracketWidth[i], b->width());
                }
            }
        }
    }

    for (Bracket* b : qAsConst(bl)) {
        delete b;
    }

    qreal totalBracketWidth = 0.0;

    qreal bd = score()->styleMM(Sid::bracketDistance);
    if (!_brackets.empty()) {
        for (int w : bracketWidth) {
            totalBracketWidth += w + bd;
        }
    }

    return totalBracketWidth;
}

//---------------------------------------------------------
//   totalBracketOffset
//---------------------------------------------------------

qreal System::totalBracketOffset(const LayoutContext& ctx)
{
    bool hideEmptyStaves = score()->styleB(Sid::hideEmptyStaves);
    score()->setStyleValue(Sid::hideEmptyStaves, false);

    qreal offset = layoutBrackets(ctx);

    score()->setStyleValue(Sid::hideEmptyStaves, hideEmptyStaves);
    return offset;
}

//---------------------------------------------------------
//   layoutSystem
///   Layout the System
//---------------------------------------------------------

void System::layoutSystem(const LayoutContext& ctx, qreal xo1, const bool isFirstSystem, bool firstSystemIndent)
{
    if (_staves.empty()) {                 // ignore vbox
        return;
    }

    qreal instrumentNameOffset = score()->styleMM(Sid::instrumentNameOffset);

    int nstaves  = _staves.size();

    //---------------------------------------------------
    //  find x position of staves
    //---------------------------------------------------
    qreal maxNamesWidth = systemNamesWidth();

    if (isFirstSystem && firstSystemIndent) {
        maxNamesWidth = qMax(maxNamesWidth, styleP(Sid::firstSystemIndentationValue) * mag());
    }

    qreal maxBracketsWidth = totalBracketOffset(ctx);
    qreal bracketsWidth = layoutBrackets(ctx);
    qreal bracketWidthDifference = maxBracketsWidth - bracketsWidth;
    if (maxNamesWidth == 0.0) {
        if (score()->styleB(Sid::alignSystemToMargin)) {
            _leftMargin = bracketWidthDifference;
        } else {
            _leftMargin = maxBracketsWidth;
        }
    } else {
        _leftMargin = maxNamesWidth + bracketWidthDifference + instrumentNameOffset;
    }

    int nVisible = 0;
    for (int staffIdx = 0; staffIdx < nstaves; ++staffIdx) {
        SysStaff* s  = _staves[staffIdx];
        Staff* staff = score()->staff(staffIdx);
        if (!staff->show() || !s->show()) {
            s->setbbox(RectF());
            continue;
        }
        ++nVisible;
        qreal staffMag = staff->staffMag(Fraction(0, 1));         // ??? TODO
        int staffLines = staff->lines(Fraction(0, 1));
        if (staffLines <= 1) {
            qreal h = staff->lineDistance(Fraction(0, 1)) * staffMag * spatium();
            s->bbox().setRect(_leftMargin + xo1, -h, 0.0, 2 * h);
        } else {
            qreal h = (staffLines - 1) * staff->lineDistance(Fraction(0, 1));
            h = h * staffMag * spatium();
            s->bbox().setRect(_leftMargin + xo1, 0.0, 0.0, h);
        }
    }

    //---------------------------------------------------
    //  layout brackets
    //---------------------------------------------------

    setBracketsXPosition(xo1 + _leftMargin);

    //---------------------------------------------------
    //  layout instrument names x position
    //     at this point it is not clear which staves will
    //     be hidden, so layout all instrument names
    //---------------------------------------------------

    for (SysStaff* s : qAsConst(_staves)) {
        for (InstrumentName* t : qAsConst(s->instrumentNames)) {
            // reset align layout
            Align originAlign = t->align();
            t->setAlign(Align(AlignH::LEFT, originAlign.vertical));
            t->layout();
            t->setAlign(originAlign);

            switch (t->align().horizontal) {
            case AlignH::LEFT:
                t->rxpos() = 0 - bracketsWidth;
                break;
            case AlignH::HCENTER:
                t->rxpos() = (maxNamesWidth - t->width()) / 2 - bracketsWidth;
                break;
            case AlignH::RIGHT:
            default:
                t->rxpos() = maxNamesWidth - t->width() - bracketsWidth;
                break;
            }
        }
    }
}

//---------------------------------------------------------
//   setMeasureHeight
//---------------------------------------------------------

void System::setMeasureHeight(qreal height)
{
    qreal _spatium { spatium() };
    for (MeasureBase* m : ml) {
        if (m->isMeasure()) {
            // note that the factor 2 * _spatium must be corrected for when exporting
            // system distance in MusicXML (issue #24733)
            m->bbox().setRect(0.0, -_spatium, m->width(), height + 2.0 * _spatium);
        } else if (m->isHBox()) {
            m->bbox().setRect(0.0, 0.0, m->width(), height);
            toHBox(m)->layout2();
        } else if (m->isTBox()) {
            toTBox(m)->layout();
        } else {
            qDebug("unhandled measure type %s", m->name());
        }
    }
}

//---------------------------------------------------------
//   layoutBracketsVertical
//---------------------------------------------------------

void System::layoutBracketsVertical()
{
    for (Bracket* b : qAsConst(_brackets)) {
        int staffIdx1 = b->firstStaff();
        int staffIdx2 = b->lastStaff();
        qreal sy = 0;                           // assume bracket not visible
        qreal ey = 0;
        // if start staff not visible, try next staff
        while (staffIdx1 <= staffIdx2 && !_staves[staffIdx1]->show()) {
            ++staffIdx1;
        }
        // if end staff not visible, try prev staff
        while (staffIdx1 <= staffIdx2 && !_staves[staffIdx2]->show()) {
            --staffIdx2;
        }
        // if the score doesn't have "alwaysShowBracketsWhenEmptyStavesAreHidden" as true,
        // the bracket will be shown IF:
        // it spans at least 2 visible staves (staffIdx1 < staffIdx2) OR
        // it spans just one visible staff (staffIdx1 == staffIdx2) but it is required to do so
        // (the second case happens at least when the bracket is initially dropped)
        bool notHidden = score()->styleB(Sid::alwaysShowBracketsWhenEmptyStavesAreHidden)
                         ? (staffIdx1 <= staffIdx2) : (staffIdx1 < staffIdx2) || (b->span() == 1 && staffIdx1 == staffIdx2);
        if (notHidden) {                        // set vert. pos. and height to visible spanned staves
            sy = _staves[staffIdx1]->bbox().top();
            ey = _staves[staffIdx2]->bbox().bottom();
        }
        b->rypos() = sy;
        b->setHeight(ey - sy);
        b->layout();
    }
}

//---------------------------------------------------------
//   layoutInstrumentNames
//---------------------------------------------------------

void System::layoutInstrumentNames()
{
    int staffIdx = 0;

    for (Part* p : score()->parts()) {
        SysStaff* s = staff(staffIdx);
        SysStaff* s2;
        int nstaves = p->nstaves();

        int visible = firstVisibleSysStaffOfPart(p);
        if (visible >= 0) {
            // The top staff might be invisible but this top staff contains the instrument names.
            // To make sure these instrument name are drawn, even when the top staff is invisible,
            // move the InstrumentName elements to the the first visible staff of the part.
            if (visible != staffIdx) {
                SysStaff* vs = staff(visible);
                for (InstrumentName* t : qAsConst(s->instrumentNames)) {
                    t->setTrack(visible * VOICES);
                    t->setSysStaff(vs);
                    vs->instrumentNames.append(t);
                }
                s->instrumentNames.clear();
                s = vs;
            }

            for (InstrumentName* t : qAsConst(s->instrumentNames)) {
                //
                // override Text->layout()
                //
                qreal y1, y2;
                switch (t->layoutPos()) {
                default:
                case 0:                         // center at part
                    y1 = s->bbox().top();
                    s2 = staff(staffIdx);
                    for (int i = staffIdx + nstaves - 1; i > 0; --i) {
                        SysStaff* s3 = staff(i);
                        if (s3->show()) {
                            s2 = s3;
                            break;
                        }
                    }
                    y2 = s2->bbox().bottom();
                    break;
                case 1:                         // center at first staff
                    y1 = s->bbox().top();
                    y2 = s->bbox().bottom();
                    break;
                case 2:                         // center between first and second staff
                    y1 = s->bbox().top();
                    y2 = staff(staffIdx + 1)->bbox().bottom();
                    break;
                case 3:                         // center at second staff
                    y1 = staff(staffIdx + 1)->bbox().top();
                    y2 = staff(staffIdx + 1)->bbox().bottom();
                    break;
                case 4:                         // center between first and second staff
                    y1 = staff(staffIdx + 1)->bbox().top();
                    y2 = staff(staffIdx + 2)->bbox().bottom();
                    break;
                case 5:                         // center at third staff
                    y1 = staff(staffIdx + 2)->bbox().top();
                    y2 = staff(staffIdx + 2)->bbox().bottom();
                    break;
                }
                t->rypos() = y1 + (y2 - y1) * .5 + t->offset().y();
            }
        }
        staffIdx += nstaves;
    }
}

//---------------------------------------------------------
//   addBrackets
//   Add brackets in front of this measure, typically behind a HBox
//---------------------------------------------------------

void System::addBrackets(const LayoutContext& ctx, Measure* measure)
{
    if (_staves.empty()) {                 // ignore vbox
        return;
    }

    int nstaves = _staves.size();

    //---------------------------------------------------
    //  find x position of staves
    //    create brackets
    //---------------------------------------------------

    int columns = getBracketsColumnsCount();

    QList<Bracket*> bl;
    bl.swap(_brackets);

    for (int staffIdx = 0; staffIdx < nstaves; ++staffIdx) {
        Staff* s = score()->staff(staffIdx);
        for (int i = 0; i < columns; ++i) {
            for (auto bi : s->brackets()) {
                if (bi->column() != i || bi->bracketType() == BracketType::NO_BRACKET) {
                    continue;
                }
                createBracket(ctx, bi, i, staffIdx, bl, measure);
            }
        }
        if (!staff(staffIdx)->show()) {
            continue;
        }
    }

    //---------------------------------------------------
    //  layout brackets
    //---------------------------------------------------

    setBracketsXPosition(measure->x());

    _brackets.append(bl);
}

//---------------------------------------------------------
//   createBracket
//   Create a bracket if it spans more then one visible system
//   If measure is NULL adds the bracket in front of the system, else in front of the measure.
//   Returns the bracket if it got created, else NULL
//---------------------------------------------------------

Bracket* System::createBracket(const LayoutContext& ctx, Ms::BracketItem* bi, int column, int staffIdx, QList<Ms::Bracket*>& bl,
                               Measure* measure)
{
    int nstaves = _staves.size();
    int firstStaff = staffIdx;
    int lastStaff = staffIdx + bi->bracketSpan() - 1;
    if (lastStaff >= nstaves) {
        lastStaff = nstaves - 1;
    }

    for (; firstStaff <= lastStaff; ++firstStaff) {
        if (staff(firstStaff)->show()) {
            break;
        }
    }
    for (; lastStaff >= firstStaff; --lastStaff) {
        if (staff(lastStaff)->show()) {
            break;
        }
    }
    int span = lastStaff - firstStaff + 1;
    //
    // do not show bracket if it only spans one
    // system due to some invisible staves
    //
    if (span > 1
        || (bi->bracketSpan() == span)
        || (span == 1 && score()->styleB(Sid::alwaysShowBracketsWhenEmptyStavesAreHidden))) {
        //
        // this bracket is visible
        //
        Bracket* b = 0;
        int track = staffIdx * VOICES;
        for (int k = 0; k < bl.size(); ++k) {
            if (bl[k]->track() == track && bl[k]->column() == column && bl[k]->bracketType() == bi->bracketType()
                && bl[k]->measure() == measure) {
                b = bl.takeAt(k);
                break;
            }
        }
        if (b == 0) {
            b = Factory::createBracket(ctx.score()->dummy());
            b->setBracketItem(bi);
            b->setGenerated(true);
            b->setTrack(track);
            b->setMeasure(measure);
        }
        add(b);
        b->setStaffSpan(firstStaff, lastStaff);
        return b;
    }

    return nullptr;
}

int System::getBracketsColumnsCount()
{
    int columns = 0;
    int nstaves = _staves.size();
    for (int idx = 0; idx < nstaves; ++idx) {
        for (auto bi : score()->staff(idx)->brackets()) {
            columns = qMax(columns, bi->column() + 1);
        }
    }
    return columns;
}

void System::setBracketsXPosition(const qreal xPosition)
{
    qreal bracketDistance = score()->styleMM(Sid::bracketDistance);
    for (Bracket* b1 : qAsConst(_brackets)) {
        qreal xOffset = 0;
        for (const Bracket* b2 : qAsConst(_brackets)) {
            bool b1FirstStaffInB2 = (b1->firstStaff() >= b2->firstStaff() && b1->firstStaff() <= b2->lastStaff());
            bool b1LastStaffInB2 = (b1->lastStaff() >= b2->firstStaff() && b1->lastStaff() <= b2->lastStaff());
            if (b1->column() > b2->column()
                && (b1FirstStaffInB2 || b1LastStaffInB2)) {
                xOffset += b2->width() + bracketDistance;
            }
        }
        b1->rxpos() = xPosition - xOffset - b1->width();
    }
}

//---------------------------------------------------------
//   nextVisibleStaff
//---------------------------------------------------------

int System::nextVisibleStaff(int staffIdx) const
{
    int i;
    for (i = staffIdx + 1; i < _staves.size(); ++i) {
        Staff* s  = score()->staff(i);
        SysStaff* ss = _staves[i];
        if (s->show() && ss->show()) {
            break;
        }
    }
    return i;
}

//---------------------------------------------------------
//   firstVisibleStaff
//---------------------------------------------------------

int System::firstVisibleStaff() const
{
    return nextVisibleStaff(-1);
}

//---------------------------------------------------------
//   layout2
//    called after measure layout
//    adjusts staff distance
//---------------------------------------------------------

void System::layout2(const LayoutContext& ctx)
{
    Box* vb = vbox();
    if (vb) {
        vb->layout();
        setbbox(vb->bbox());
        return;
    }

    setPos(0.0, 0.0);
    QList<std::pair<int, SysStaff*> > visibleStaves;

    for (int i = 0; i < _staves.size(); ++i) {
        Staff* s  = score()->staff(i);
        SysStaff* ss = _staves[i];
        if (s->show() && ss->show()) {
            visibleStaves.append(std::pair<int, SysStaff*>(i, ss));
        } else {
            ss->setbbox(RectF());        // already done in layout() ?
        }
    }

    qreal _spatium            = spatium();
    qreal y                   = 0.0;
    qreal minVerticalDistance = score()->styleMM(Sid::minVerticalDistance);
    qreal staffDistance       = score()->styleMM(Sid::staffDistance);
    qreal akkoladeDistance    = score()->styleMM(Sid::akkoladeDistance);
    if (score()->enableVerticalSpread()) {
        staffDistance       = score()->styleMM(Sid::minStaffSpread);
        akkoladeDistance    = score()->styleMM(Sid::minStaffSpread);
    }

    if (visibleStaves.empty()) {
        qDebug("====no visible staves, staves %d, score staves %d", _staves.size(), score()->nstaves());
        return;
    }

    for (auto i = visibleStaves.begin();; ++i) {
        SysStaff* ss  = i->second;
        int si1       = i->first;
        Staff* staff  = score()->staff(si1);
        auto ni       = i + 1;

        qreal dist = staff->height();
        qreal yOffset;
        qreal h;
        if (staff->lines(Fraction(0, 1)) == 1) {
            yOffset = _spatium * BARLINE_SPAN_1LINESTAFF_TO * 0.5;
            h = _spatium * (BARLINE_SPAN_1LINESTAFF_TO - BARLINE_SPAN_1LINESTAFF_FROM) * 0.5;
        } else {
            yOffset = 0.0;
            h = staff->height();
        }
        if (ni == visibleStaves.end()) {
            ss->setYOff(yOffset);
            ss->bbox().setRect(_leftMargin, y - yOffset, width() - _leftMargin, h);
            ss->saveLayout();
            break;
        }

        int si2        = ni->first;
        Staff* staff2  = score()->staff(si2);

        if (staff->part() == staff2->part()) {
            Measure* m = firstMeasure();
            qreal mag = m ? staff->staffMag(m->tick()) : 1.0;
            dist += akkoladeDistance * mag;
        } else {
            dist += staffDistance;
        }
        dist += staff2->userDist();
        bool fixedSpace = false;
        for (MeasureBase* mb : ml) {
            if (!mb->isMeasure()) {
                continue;
            }
            Measure* m = toMeasure(mb);
            Spacer* sp = m->vspacerDown(si1);
            if (sp) {
                if (sp->spacerType() == SpacerType::FIXED) {
                    dist = staff->height() + sp->gap();
                    fixedSpace = true;
                    break;
                } else {
                    dist = qMax(dist, staff->height() + sp->gap());
                }
            }
            sp = m->vspacerUp(si2);
            if (sp) {
                dist = qMax(dist, sp->gap() + staff->height());
            }
        }
        if (!fixedSpace) {
            // check minimum distance to next staff
            // note that in continuous view, we normally only have a partial skyline for the system
            // a full one is only built when triggering a full layout
            // therefore, we don't know the value we get from minDistance will actually be enough
            // so we remember the value between layouts and increase it when necessary
            // (the first layout on switching to continuous view gives us good initial values)
            // the result is space is good to start and grows as needed
            // it does not, however, shrink when possible - only by trigger a full layout
            // (such as by toggling to page view and back)
            qreal d = ss->skyline().minDistance(System::staff(si2)->skyline());
            if (score()->lineMode()) {
                qreal previousDist = ss->continuousDist();
                if (d > previousDist) {
                    ss->setContinuousDist(d);
                } else {
                    d = previousDist;
                }
            }
            dist = qMax(dist, d + minVerticalDistance);
        }
        ss->setYOff(yOffset);
        ss->bbox().setRect(_leftMargin, y - yOffset, width() - _leftMargin, h);
        ss->saveLayout();
        y += dist;
    }

    _systemHeight = staff(visibleStaves.back().first)->bbox().bottom();
    setHeight(_systemHeight);

    setMeasureHeight(_systemHeight);

    //---------------------------------------------------
    //  layout brackets vertical position
    //---------------------------------------------------

    layoutBracketsVertical();

    //---------------------------------------------------
    //  layout instrument names
    //---------------------------------------------------

    layoutInstrumentNames();

    //---------------------------------------------------
    //  layout cross-staff slurs and ties
    //---------------------------------------------------

    Fraction stick = measures().front()->tick();
    Fraction etick = measures().back()->endTick();
    auto spanners = ctx.score()->spannerMap().findOverlapping(stick.ticks(), etick.ticks());

    std::vector<Spanner*> spanner;
    for (auto interval : spanners) {
        Spanner* sp = interval.value;
        if (sp->tick() < etick && sp->tick2() >= stick) {
            if (sp->isSlur()) {
                ChordRest* scr = sp->startCR();
                ChordRest* ecr = sp->endCR();
                int idx = sp->vStaffIdx();
                if (scr && ecr && (scr->vStaffIdx() != idx || ecr->vStaffIdx() != idx)) {
                    sp->layoutSystem(this);
                }
            }
        }
    }
}

//---------------------------------------------------------
//   restoreLayout2
//---------------------------------------------------------

void System::restoreLayout2()
{
    if (vbox()) {
        return;
    }

    for (SysStaff* s : _staves) {
        s->restoreLayout();
    }

    setHeight(_systemHeight);
    setMeasureHeight(_systemHeight);
}

//---------------------------------------------------------
//   setInstrumentNames
//---------------------------------------------------------

void System::setInstrumentNames(const LayoutContext& ctx, bool longName, Fraction tick)
{
    //
    // remark: add/remove instrument names is not undo/redoable
    //         as add/remove of systems is not undoable
    //
    if (vbox()) {                 // ignore vbox
        return;
    }
    if (!score()->showInstrumentNames()
        || (style()->styleB(Sid::hideInstrumentNameIfOneInstrument) && score()->parts().size() == 1)) {
        for (SysStaff* staff : qAsConst(_staves)) {
            foreach (InstrumentName* t, staff->instrumentNames) {
                ctx.score()->removeElement(t);
            }
        }
        return;
    }

    int staffIdx = 0;
    for (SysStaff* staff : qAsConst(_staves)) {
        Staff* s = score()->staff(staffIdx);
        if (!s->isTop() || !s->show()) {
            for (InstrumentName* t : qAsConst(staff->instrumentNames)) {
                ctx.score()->removeElement(t);
            }
            ++staffIdx;
            continue;
        }

        Part* part = s->part();
        const QList<StaffName>& names = longName ? part->longNames(tick) : part->shortNames(tick);

        int idx = 0;
        for (const StaffName& sn : names) {
            InstrumentName* iname = staff->instrumentNames.value(idx);
            if (iname == 0) {
                iname = new InstrumentName(this);
                // iname->setGenerated(true);
                iname->setParent(this);
                iname->setSysStaff(staff);
                iname->setTrack(staffIdx * VOICES);
                iname->setInstrumentNameType(longName ? InstrumentNameType::LONG : InstrumentNameType::SHORT);
                iname->setLayoutPos(sn.pos());
                ctx.score()->addElement(iname);
            }
            iname->setXmlText(sn.name());
            ++idx;
        }
        for (; idx < staff->instrumentNames.size(); ++idx) {
            ctx.score()->removeElement(staff->instrumentNames[idx]);
        }
        ++staffIdx;
    }
}

//---------------------------------------------------------
//   y2staff
//---------------------------------------------------------

/**
 Return staff number for canvas relative y position \a y
 or -1 if not found.

 To allow drag and drop above and below the staff, the actual y range
 considered "inside" the staff is increased by "margin".
*/

int System::y2staff(qreal y) const
{
    y -= pos().y();
    int idx = 0;
    qreal margin = spatium() * 2;
    foreach (SysStaff* s, _staves) {
        qreal y1 = s->bbox().top() - margin;
        qreal y2 = s->bbox().bottom() + margin;
        if (y >= y1 && y < y2) {
            return idx;
        }
        ++idx;
    }
    return -1;
}

//---------------------------------------------------------
//   searchStaff
///   Finds a staff which y position is most close to the
///   given \p y.
///   \param y The y coordinate in system coordinates.
///   \param preferredStaff If not -1, will give more space
///   to a staff with the given number when searching it by
///   coordinate.
///   \returns Number of the found staff.
//---------------------------------------------------------

int System::searchStaff(qreal y, int preferredStaff /* = -1 */, qreal spacingFactor) const
{
    int i = 0;
    const int nstaves = score()->nstaves();
    for (; i < nstaves;) {
        SysStaff* stff = staff(i);
        if (!stff->show() || !score()->staff(i)->show()) {
            ++i;
            continue;
        }
        int ni = i;
        for (;;) {
            ++ni;
            if (ni == nstaves || (staff(ni)->show() && score()->staff(ni)->show())) {
                break;
            }
        }

        qreal sy2;
        if (ni != nstaves) {
            SysStaff* nstaff = staff(ni);
            qreal s1y2       = stff->bbox().y() + stff->bbox().height();
            if (i == preferredStaff) {
                sy2 = s1y2 + (nstaff->bbox().y() - s1y2);
            } else if (ni == preferredStaff) {
                sy2 = s1y2;
            } else {
                sy2 = s1y2 + (nstaff->bbox().y() - s1y2) * spacingFactor;
            }
        } else {
            sy2 = page()->height() - pos().y();
        }
        if (y > sy2) {
            i   = ni;
            continue;
        }
        break;
    }
    return i;
}

//---------------------------------------------------------
//   add
//---------------------------------------------------------

void System::add(EngravingItem* el)
{
    if (!el) {
        return;
    }
// qDebug("%p System::add: %p %s", this, el, el->name());

    el->setParent(this);

    switch (el->type()) {
    case ElementType::INSTRUMENT_NAME:
// qDebug("  staffIdx %d, staves %d", el->staffIdx(), _staves.size());
        _staves[el->staffIdx()]->instrumentNames.append(toInstrumentName(el));
        toInstrumentName(el)->setSysStaff(_staves[el->staffIdx()]);
        break;

    case ElementType::BEAM:
        score()->addElement(el);
        break;

    case ElementType::BRACKET: {
        Bracket* b   = toBracket(el);
        _brackets.append(b);
    }
    break;

    case ElementType::MEASURE:
    case ElementType::HBOX:
    case ElementType::VBOX:
    case ElementType::TBOX:
    case ElementType::FBOX:
        score()->addElement(el);
        break;
    case ElementType::TEXTLINE_SEGMENT:
    case ElementType::HAIRPIN_SEGMENT:
    case ElementType::OTTAVA_SEGMENT:
    case ElementType::TRILL_SEGMENT:
    case ElementType::VIBRATO_SEGMENT:
    case ElementType::VOLTA_SEGMENT:
    case ElementType::SLUR_SEGMENT:
    case ElementType::TIE_SEGMENT:
    case ElementType::PEDAL_SEGMENT:
    case ElementType::LYRICSLINE_SEGMENT:
    case ElementType::GLISSANDO_SEGMENT:
    case ElementType::LET_RING_SEGMENT:
    case ElementType::PALM_MUTE_SEGMENT:
    {
        SpannerSegment* ss = toSpannerSegment(el);
#ifndef NDEBUG
        if (_spannerSegments.contains(ss)) {
            qDebug("System::add() %s %p already there", ss->name(), ss);
        } else
#endif
        _spannerSegments.append(ss);
    }
    break;

    case ElementType::SYSTEM_DIVIDER:
    {
        SystemDivider* sd = toSystemDivider(el);
        if (sd->dividerType() == SystemDivider::Type::LEFT) {
            _systemDividerLeft = sd;
        } else {
            _systemDividerRight = sd;
        }
    }
    break;

    default:
        qDebug("System::add(%s) not implemented", el->name());
        break;
    }
}

//---------------------------------------------------------
//   remove
//---------------------------------------------------------

void System::remove(EngravingItem* el)
{
    switch (el->type()) {
    case ElementType::INSTRUMENT_NAME:
        _staves[el->staffIdx()]->instrumentNames.removeOne(toInstrumentName(el));
        toInstrumentName(el)->setSysStaff(0);
        break;
    case ElementType::BEAM:
        score()->removeElement(el);
        break;
    case ElementType::BRACKET:
    {
        Bracket* b = toBracket(el);
        if (!_brackets.removeOne(b)) {
            qDebug("System::remove: bracket not found");
        }
    }
    break;
    case ElementType::MEASURE:
    case ElementType::HBOX:
    case ElementType::VBOX:
    case ElementType::TBOX:
    case ElementType::FBOX:
        score()->removeElement(el);
        break;
    case ElementType::TEXTLINE_SEGMENT:
    case ElementType::HAIRPIN_SEGMENT:
    case ElementType::OTTAVA_SEGMENT:
    case ElementType::TRILL_SEGMENT:
    case ElementType::VIBRATO_SEGMENT:
    case ElementType::VOLTA_SEGMENT:
    case ElementType::SLUR_SEGMENT:
    case ElementType::TIE_SEGMENT:
    case ElementType::PEDAL_SEGMENT:
    case ElementType::LYRICSLINE_SEGMENT:
    case ElementType::GLISSANDO_SEGMENT:
        if (!_spannerSegments.removeOne(toSpannerSegment(el))) {
            qDebug("System::remove: %p(%s) not found, score %p", el, el->name(), score());
            Q_ASSERT(score() == el->score());
        }
        break;
    case ElementType::SYSTEM_DIVIDER:
        if (el == _systemDividerLeft) {
            _systemDividerLeft = 0;
        } else {
            Q_ASSERT(_systemDividerRight == el);
            _systemDividerRight = 0;
        }
        break;

    default:
        qDebug("System::remove(%s) not implemented", el->name());
        break;
    }
}

//---------------------------------------------------------
//   change
//---------------------------------------------------------

void System::change(EngravingItem* o, EngravingItem* n)
{
    remove(o);
    add(n);
}

//---------------------------------------------------------
//   snap
//---------------------------------------------------------

Fraction System::snap(const Fraction& tick, const PointF p) const
{
    for (const MeasureBase* m : ml) {
        if (p.x() < m->x() + m->width()) {
            return toMeasure(m)->snap(tick, p - m->pos());       //TODO: MeasureBase
        }
    }
    return toMeasure(ml.back())->snap(tick, p - pos());          //TODO: MeasureBase
}

//---------------------------------------------------------
//   snap
//---------------------------------------------------------

Fraction System::snapNote(const Fraction& tick, const PointF p, int staff) const
{
    for (const MeasureBase* m : ml) {
        if (p.x() < m->x() + m->width()) {
            return toMeasure(m)->snapNote(tick, p - m->pos(), staff);        //TODO: MeasureBase
        }
    }
    return toMeasure(ml.back())->snap(tick, p - pos());          // TODO: MeasureBase
}

//---------------------------------------------------------
//   firstMeasure
//---------------------------------------------------------

Measure* System::firstMeasure() const
{
    auto i = std::find_if(ml.begin(), ml.end(), [](MeasureBase* mb) { return mb->isMeasure(); });
    return i != ml.end() ? toMeasure(*i) : 0;
}

//---------------------------------------------------------
//   lastMeasure
//---------------------------------------------------------

Measure* System::lastMeasure() const
{
    auto i = std::find_if(ml.rbegin(), ml.rend(), [](MeasureBase* mb) { return mb->isMeasure(); });
    return i != ml.rend() ? toMeasure(*i) : 0;
}

//---------------------------------------------------------
//   nextMeasure
//---------------------------------------------------------

MeasureBase* System::nextMeasure(const MeasureBase* m) const
{
    if (m == ml.back()) {
        return 0;
    }
    MeasureBase* nm = m->next();
    if (nm->isMeasure() && score()->styleB(Sid::createMultiMeasureRests) && toMeasure(nm)->hasMMRest()) {
        nm = toMeasure(nm)->mmRest();
    }
    return nm;
}

//---------------------------------------------------------
//   scanElements
//---------------------------------------------------------

void System::scanElements(void* data, void (* func)(void*, EngravingItem*), bool all)
{
    EngravingObject::scanElements(data, func, all);
    for (SpannerSegment* ss : qAsConst(_spannerSegments)) {
        ss->scanElements(data, func, all);
    }
}

//---------------------------------------------------------
//   staffYpage
//    return page coordinates
//---------------------------------------------------------

qreal System::staffYpage(int staffIdx) const
{
    if (staffIdx < 0 || staffIdx >= _staves.size()) {
        return pagePos().y();
    }

    return _staves[staffIdx]->y() + y();
}

//---------------------------------------------------------
//   staffCanvasYpage
//    return canvas coordinates
//---------------------------------------------------------

qreal System::staffCanvasYpage(int staffIdx) const
{
    return _staves[staffIdx]->y() + y() + page()->canvasPos().y();
}

SysStaff* System::staff(int staffIdx) const
{
    if (staffIdx >= 0 && staffIdx < _staves.size()) {
        return _staves[staffIdx];
    }

    return nullptr;
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void System::write(XmlWriter& xml) const
{
    xml.startObject(this);
    if (_systemDividerLeft && _systemDividerLeft->isUserModified()) {
        _systemDividerLeft->write(xml);
    }
    if (_systemDividerRight && _systemDividerRight->isUserModified()) {
        _systemDividerRight->write(xml);
    }
    xml.endObject();
}

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void System::read(XmlReader& e)
{
    while (e.readNextStartElement()) {
        const QStringRef& tag(e.name());
        if (tag == "SystemDivider") {
            SystemDivider* sd = new SystemDivider(this);
            sd->read(e);
            add(sd);
        } else {
            e.unknown();
        }
    }
}

//---------------------------------------------------------
//   nextSegmentElement
//---------------------------------------------------------

EngravingItem* System::nextSegmentElement()
{
    Measure* m = firstMeasure();
    if (m) {
        Segment* firstSeg = m->segments().first();
        if (firstSeg) {
            return firstSeg->element(0);
        }
    }
    return score()->lastElement();
}

//---------------------------------------------------------
//   prevSegmentElement
//---------------------------------------------------------

EngravingItem* System::prevSegmentElement()
{
    Segment* seg = firstMeasure()->first();
    EngravingItem* re = 0;
    while (!re) {
        seg = seg->prev1MM();
        if (!seg) {
            return score()->firstElement();
        }

        if (seg->segmentType() == SegmentType::EndBarLine) {
            score()->inputState().setTrack((score()->staves().size() - 1) * VOICES);       //correction
        }
        re = seg->lastElement(score()->staves().size() - 1);
    }
    return re;
}

//---------------------------------------------------------
//   minDistance
//    Return the minimum distance between this system and s2
//    without any element collisions.
//
//    this - top system
//    s2   - bottom system
//---------------------------------------------------------

qreal System::minDistance(System* s2) const
{
    if (vbox() && !s2->vbox()) {
        return qMax(qreal(vbox()->bottomGap()), s2->minTop());
    } else if (!vbox() && s2->vbox()) {
        return qMax(qreal(s2->vbox()->topGap()), minBottom());
    } else if (vbox() && s2->vbox()) {
        return qreal(s2->vbox()->topGap() + vbox()->bottomGap());
    }

    qreal minVerticalDistance = score()->styleMM(Sid::minVerticalDistance);
    qreal dist = score()->enableVerticalSpread() ? styleP(Sid::minSystemSpread) : styleP(Sid::minSystemDistance);
    int firstStaff;
    int lastStaff;

    for (firstStaff = 0; firstStaff < _staves.size() - 1; ++firstStaff) {
        if (score()->staff(firstStaff)->show() && s2->staff(firstStaff)->show()) {
            break;
        }
    }
    for (lastStaff = _staves.size() - 1; lastStaff > 0; --lastStaff) {
        if (score()->staff(lastStaff)->show() && staff(lastStaff)->show()) {
            break;
        }
    }

    Staff* staff = score()->staff(firstStaff);
    qreal userDist = staff ? staff->userDist() : 0.0;
    dist = qMax(dist, userDist);
    fixedDownDistance = false;

    for (MeasureBase* mb1 : ml) {
        if (mb1->isMeasure()) {
            Measure* m = toMeasure(mb1);
            Spacer* sp = m->vspacerDown(lastStaff);
            if (sp) {
                if (sp->spacerType() == SpacerType::FIXED) {
                    dist = sp->gap();
                    fixedDownDistance = true;
                    break;
                } else {
                    dist = qMax(dist, sp->gap().val());
                }
            }
        }
    }
    if (!fixedDownDistance) {
        for (MeasureBase* mb2 : s2->ml) {
            if (mb2->isMeasure()) {
                Measure* m = toMeasure(mb2);
                Spacer* sp = m->vspacerUp(firstStaff);
                if (sp) {
                    dist = qMax(dist, sp->gap().val());
                }
            }
        }

        SysStaff* sysStaff = this->staff(lastStaff);
        qreal sld = sysStaff ? sysStaff->skyline().minDistance(s2->staff(firstStaff)->skyline()) : 0;
        sld -= sysStaff ? sysStaff->bbox().height() - minVerticalDistance : 0;
        dist = qMax(dist, sld);
    }
    return dist;
}

//---------------------------------------------------------
//   topDistance
//    return minimum distance to the above south skyline
//---------------------------------------------------------

qreal System::topDistance(int staffIdx, const SkylineLine& s) const
{
    Q_ASSERT(!vbox());
    Q_ASSERT(!s.isNorth());
    // in continuous view, we only build a partial skyline for performance reasons
    // this means we cannot expect the minDistance calculation to produce meaningful results
    // so just give up on autoplace for spanners in continuous view
    // (or any other calculations that rely on this value)
    if (score()->lineMode()) {
        return 0.0;
    }
    return s.minDistance(staff(staffIdx)->skyline().north());
}

//---------------------------------------------------------
//   bottomDistance
//---------------------------------------------------------

qreal System::bottomDistance(int staffIdx, const SkylineLine& s) const
{
    Q_ASSERT(!vbox());
    Q_ASSERT(s.isNorth());
    // see note on topDistance() above
    if (score()->lineMode()) {
        return 0.0;
    }
    return staff(staffIdx)->skyline().south().minDistance(s);
}

//---------------------------------------------------------
//   firstVisibleSysStaff
//---------------------------------------------------------

int System::firstVisibleSysStaff() const
{
    int nstaves = _staves.size();
    for (int i = 0; i < nstaves; ++i) {
        if (_staves[i]->show()) {
            return i;
        }
    }
    qDebug("no sys staff");
    return -1;
}

//---------------------------------------------------------
//   lastVisibleSysStaff
//---------------------------------------------------------

int System::lastVisibleSysStaff() const
{
    int nstaves = _staves.size();
    for (int i = nstaves - 1; i >= 0; --i) {
        if (_staves[i]->show()) {
            return i;
        }
    }
    qDebug("no sys staff");
    return -1;
}

//---------------------------------------------------------
//   minTop
//    Return the minimum top margin.
//---------------------------------------------------------

qreal System::minTop() const
{
    int si = firstVisibleSysStaff();
    SysStaff* s = si < 0 ? nullptr : staff(si);
    if (s) {
        return -s->skyline().north().max();
    }
    return 0.0;
}

//---------------------------------------------------------
//   minBottom
//    Return the minimum bottom margin.
//---------------------------------------------------------

qreal System::minBottom() const
{
    if (vbox()) {
        return vbox()->bottomGap();
    }
    int si = lastVisibleSysStaff();
    SysStaff* s = si < 0 ? nullptr : staff(si);
    if (s) {
        return s->skyline().south().max() - s->bbox().height();
    }
    return 0.0;
}

//---------------------------------------------------------
//   spacerDistance
//    Return the distance needed due to spacers
//---------------------------------------------------------

qreal System::spacerDistance(bool up) const
{
    int staff = up ? firstVisibleSysStaff() : lastVisibleSysStaff();
    if (staff < 0) {
        return 0.0;
    }
    qreal dist = 0.0;
    for (MeasureBase* mb : measures()) {
        if (mb->isMeasure()) {
            Measure* m = toMeasure(mb);
            Spacer* sp = up ? m->vspacerUp(staff) : m->vspacerDown(staff);
            if (sp) {
                if (sp->spacerType() == SpacerType::FIXED) {
                    dist = sp->gap();
                    break;
                } else {
                    dist = qMax(dist, sp->gap().val());
                }
            }
        }
    }
    return dist;
}

//---------------------------------------------------------
//   upSpacer
//    Return largest upSpacer for this system. This can
//    be a downSpacer of the previous system.
//---------------------------------------------------------

Spacer* System::upSpacer(int staffIdx, Spacer* prevDownSpacer) const
{
    if (staffIdx < 0) {
        return nullptr;
    }

    if (prevDownSpacer && (prevDownSpacer->spacerType() == SpacerType::FIXED)) {
        return prevDownSpacer;
    }

    Spacer* spacer { prevDownSpacer };
    for (MeasureBase* mb : measures()) {
        if (!(mb && mb->isMeasure())) {
            continue;
        }
        Spacer* sp { toMeasure(mb)->vspacerUp(staffIdx) };
        if (sp) {
            if (!spacer || ((spacer->spacerType() == SpacerType::UP) && (sp->gap() > spacer->gap()))) {
                spacer = sp;
            }
            continue;
        }
    }
    return spacer;
}

//---------------------------------------------------------
//   downSpacer
//    Return the largest downSpacer for this system.
//---------------------------------------------------------

Spacer* System::downSpacer(int staffIdx) const
{
    if (staffIdx < 0) {
        return nullptr;
    }

    Spacer* spacer { nullptr };
    for (MeasureBase* mb : measures()) {
        if (!(mb && mb->isMeasure())) {
            continue;
        }
        Spacer* sp { toMeasure(mb)->vspacerDown(staffIdx) };
        if (sp) {
            if (sp->spacerType() == SpacerType::FIXED) {
                return sp;
            } else {
                if (!spacer || (sp->gap() > spacer->gap())) {
                    spacer = sp;
                }
            }
        }
    }
    return spacer;
}

//---------------------------------------------------------
//   firstNoteRestSegmentX
//    in System() coordinates
//    returns the position of the first note or rest,
//    or the position just after the last non-chordrest segment
//---------------------------------------------------------

qreal System::firstNoteRestSegmentX(bool leading)
{
    qreal margin = score()->spatium();
    for (const MeasureBase* mb : measures()) {
        if (mb->isMeasure()) {
            const Measure* measure = static_cast<const Measure*>(mb);
            for (const Segment* seg = measure->first(); seg; seg = seg->next()) {
                if (seg->isChordRestType()) {
                    qreal noteRestPos = seg->measure()->pos().x() + seg->pos().x();
                    if (!leading) {
                        return noteRestPos;
                    }

                    // first CR found; back up to previous segment
                    seg = seg->prevActive();
                    while (seg && seg->allElementsInvisible()) {
                        seg = seg->prevActive();
                    }
                    if (seg) {
                        // find maximum width
                        qreal width = 0.0;
                        int n = score()->nstaves();
                        for (int i = 0; i < n; ++i) {
                            if (!staff(i)->show()) {
                                continue;
                            }
                            EngravingItem* e = seg->element(i * VOICES);
                            if (e && e->addToSkyline()) {
                                width = qMax(width, e->pos().x() + e->bbox().right());
                            }
                        }
                        return qMin(seg->measure()->pos().x() + seg->pos().x() + width + margin, noteRestPos);
                    } else {
                        return margin;
                    }
                }
            }
        }
    }
    qDebug("firstNoteRestSegmentX: did not find segment");
    return margin;
}

//---------------------------------------------------------
//   lastNoteRestSegmentX
//    in System() coordinates
//    returns the position of the last note or rest,
//    or the position just before the first non-chordrest segment
//---------------------------------------------------------

qreal System::lastNoteRestSegmentX(bool trailing)
{
    qreal margin = score()->spatium() / 4;  // TODO: this can be parameterizable
    //for (const MeasureBase* mb : measures()) {
    for (auto measureBaseIter = measures().rbegin(); measureBaseIter != measures().rend(); measureBaseIter++) {
        if ((*measureBaseIter)->isMeasure()) {
            const Measure* measure = static_cast<const Measure*>(*measureBaseIter);
            for (const Segment* seg = measure->last(); seg; seg = seg->prev()) {
                if (seg->isChordRestType()) {
                    qreal noteRestPos = seg->measure()->pos().x() + seg->pos().x();
                    if (!trailing) {
                        return noteRestPos;
                    }

                    // last CR found; find next segment after this one
                    seg = seg->nextActive();
                    while (seg && seg->allElementsInvisible()) {
                        seg = seg->nextActive();
                    }
                    if (seg) {
                        return qMax(seg->measure()->pos().x() + seg->pos().x() - margin, noteRestPos);
                    } else {
                        return bbox().x() - margin;
                    }
                }
            }
        }
    }
    qDebug("lastNoteRestSegmentX: did not find segment");
    return margin;
}

//---------------------------------------------------------
//   pageBreak
//---------------------------------------------------------

bool System::pageBreak() const
{
    return ml.empty() ? false : ml.back()->pageBreak();
}

//---------------------------------------------------------
//   endTick
//---------------------------------------------------------

Fraction System::endTick() const
{
    return measures().back()->endTick();
}

//---------------------------------------------------------
//   firstSysStaffOfPart
//---------------------------------------------------------

int System::firstSysStaffOfPart(const Part* part) const
{
    int staffIdx { 0 };
    for (const Part* p : score()->parts()) {
        if (p == part) {
            return staffIdx;
        }
        staffIdx += p->nstaves();
    }
    return -1;   // Part not found.
}

//---------------------------------------------------------
//   firstVisibleSysStaffOfPart
//---------------------------------------------------------

int System::firstVisibleSysStaffOfPart(const Part* part) const
{
    int firstIdx = firstSysStaffOfPart(part);
    for (int idx = firstIdx; idx < firstIdx + part->nstaves(); ++idx) {
        if (staff(idx)->show()) {
            return idx;
        }
    }
    return -1;   // No visible staves on this part.
}

//---------------------------------------------------------
//   lastSysStaffOfPart
//---------------------------------------------------------

int System::lastSysStaffOfPart(const Part* part) const
{
    int firstIdx = firstSysStaffOfPart(part);
    if (firstIdx < 0) {
        return -1;     // Part not found.
    }
    return firstIdx + part->nstaves() - 1;
}

//---------------------------------------------------------
//   lastVisibleSysStaffOfPart
//---------------------------------------------------------

int System::lastVisibleSysStaffOfPart(const Part* part) const
{
    for (int idx = lastSysStaffOfPart(part); idx >= firstSysStaffOfPart(part); --idx) {
        if (staff(idx)->show()) {
            return idx;
        }
    }
    return -1;   // No visible staves on this part.
}
}
