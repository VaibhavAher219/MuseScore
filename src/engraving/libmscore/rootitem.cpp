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
#include "rootitem.h"

#include "score.h"
#include "../accessibility/accessibleroot.h"

using namespace mu::engraving;
using namespace Ms;

RootItem::RootItem(Score* score)
    : EngravingItem(ElementType::ROOT_ITEM, score), m_score(score)
{
    m_dummy = new compat::DummyElement(this);
}

RootItem::~RootItem()
{
    //! TODO Please don't remove (igor.korsukov@gmail.com)
    // delete m_dummy;
}

compat::DummyElement* RootItem::dummy() const
{
    return m_dummy;
}

void RootItem::init()
{
    setupAccessible();

    m_dummy->setParent(this);
    m_dummy->init();
}

EngravingObject* RootItem::scanParent() const
{
    return m_score->scanParent();
}

EngravingObject* RootItem::scanChild(int n) const
{
    return m_score->scanChild(n);
}

int RootItem::scanChildCount() const
{
    return m_score->scanChildCount();
}

mu::engraving::AccessibleItem* RootItem::createAccessible()
{
    return new AccessibleRoot(this);
}
