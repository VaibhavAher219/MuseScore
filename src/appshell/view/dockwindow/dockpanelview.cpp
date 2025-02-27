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

#include "dockpanelview.h"

#include "thirdparty/KDDockWidgets/src/DockWidgetQuick.h"
#include "thirdparty/KDDockWidgets/src/private/Frame_p.h"

#include "log.h"
#include "translation.h"

#include "ui/uitypes.h"
#include "ui/view/abstractmenumodel.h"

using namespace mu::dock;
using namespace mu::uicomponents;
using namespace mu::ui;
using namespace mu::actions;

static const QString SET_DOCK_OPEN_ACTION_CODE = "dock-set-open";
static const QString TOGGLE_FLOATING_ACTION_CODE = "dock-toggle-floating";

class DockPanelView::DockPanelMenuModel : public AbstractMenuModel
{
public:
    DockPanelMenuModel(DockPanelView* panel)
        : AbstractMenuModel(panel), m_panel(panel)
    {
        listenFloatingChanged();
    }

    void load() override
    {
        TRACEFUNC;

        MenuItemList items;

        if (m_customMenuModel && m_customMenuModel->rowCount() > 0) {
            items << m_customMenuModel->items();
            items << makeSeparator();
        }

        MenuItem closeDockItem = buildMenuItem(SET_DOCK_OPEN_ACTION_CODE, mu::qtrc("dock", "Close"));
        closeDockItem.args = ActionData::make_arg2<QString, bool>(m_panel->objectName(), false);
        items << closeDockItem;

        MenuItem toggleFloatingItem = buildMenuItem(TOGGLE_FLOATING_ACTION_CODE, toggleFloatingActionTitle());
        toggleFloatingItem.args = ActionData::make_arg1<QString>(m_panel->objectName());
        items << toggleFloatingItem;

        setItems(items);
    }

    AbstractMenuModel* customMenuModel() const
    {
        return m_customMenuModel;
    }

    void setCustomMenuModel(AbstractMenuModel* model)
    {
        m_customMenuModel = model;

        if (!model) {
            return;
        }

        connect(model, &AbstractMenuModel::itemsChanged, this, [this]() {
            load();
        });

        connect(model, &AbstractMenuModel::itemChanged, this, [this](const MenuItem& item) {
            updateItem(item);
        });
    }

private:
    MenuItem buildMenuItem(const QString& actionCode, const QString& title) const
    {
        MenuItem item;
        item.id = actionCode;
        item.code = codeFromQString(actionCode);
        item.title = title;
        item.state.enabled = true;

        return item;
    }

    QString toggleFloatingActionTitle() const
    {
        return m_panel->floating() ? mu::qtrc("dock", "Dock") : mu::qtrc("dock", "Undock");
    }

    void listenFloatingChanged()
    {
        connect(m_panel, &DockPanelView::floatingChanged, this, [this]() {
            int index = itemIndex(TOGGLE_FLOATING_ACTION_CODE);

            if (index == INVALID_ITEM_INDEX) {
                return;
            }

            MenuItem& item = this->item(index);
            item.title = toggleFloatingActionTitle();

            QModelIndex modelIndex = this->index(index);
            emit dataChanged(modelIndex, modelIndex, { TitleRole });
        });
    }

    void updateItem(const MenuItem& newItem)
    {
        int index = itemIndex(newItem.id);

        if (index == INVALID_ITEM_INDEX) {
            return;
        }

        item(index) = newItem;

        QModelIndex modelIndex = this->index(index);
        emit dataChanged(modelIndex, modelIndex);
    }

    AbstractMenuModel* m_customMenuModel = nullptr;
    DockPanelView* m_panel = nullptr;
};

DockPanelView::DockPanelView(QQuickItem* parent)
    : DockBase(parent), m_menuModel(new DockPanelMenuModel(this))
{
    setLocation(Location::Left);
}

DockPanelView::~DockPanelView()
{
    KDDockWidgets::DockWidgetQuick* dockWidget = this->dockWidget();
    IF_ASSERT_FAILED(dockWidget) {
        return;
    }

    dockWidget->setProperty(DOCK_PANEL_PROPERY, QVariant::fromValue(nullptr));
    dockWidget->setProperty(CONTEXT_MENU_MODEL_PROPERTY, QVariant::fromValue(nullptr));
}

DockPanelView* DockPanelView::tabifyPanel() const
{
    return m_tabifyPanel;
}

void DockPanelView::setTabifyPanel(DockPanelView* panel)
{
    if (panel == m_tabifyPanel) {
        return;
    }

    m_tabifyPanel = panel;
    emit tabifyPanelChanged(panel);
}

DockType DockPanelView::type() const
{
    return DockType::Panel;
}

void DockPanelView::componentComplete()
{
    DockBase::componentComplete();

    KDDockWidgets::DockWidgetQuick* dockWidget = this->dockWidget();
    IF_ASSERT_FAILED(dockWidget) {
        return;
    }

    m_menuModel->load();

    dockWidget->setProperty(DOCK_PANEL_PROPERY, QVariant::fromValue(this));
    dockWidget->setProperty(CONTEXT_MENU_MODEL_PROPERTY, QVariant::fromValue(m_menuModel));

    connect(m_menuModel, &AbstractMenuModel::itemsChanged, [dockWidget, this]() {
        if (dockWidget) {
            dockWidget->setProperty(CONTEXT_MENU_MODEL_PROPERTY, QVariant::fromValue(m_menuModel));
        }
    });
}

QObject* DockPanelView::navigationSection() const
{
    return m_navigationSection;
}

void DockPanelView::setNavigationSection(QObject* newNavigation)
{
    if (m_navigationSection == newNavigation) {
        return;
    }

    m_navigationSection = newNavigation;
    emit navigationSectionChanged();
}

AbstractMenuModel* DockPanelView::contextMenuModel() const
{
    return m_menuModel->customMenuModel();
}

void DockPanelView::setContextMenuModel(AbstractMenuModel* model)
{
    if (m_menuModel->customMenuModel() == model) {
        return;
    }

    m_menuModel->setCustomMenuModel(model);
    emit contextMenuModelChanged();
}

void DockPanelView::addPanelAsTab(DockPanelView* tab)
{
    IF_ASSERT_FAILED(tab && dockWidget()) {
        return;
    }

    dockWidget()->addDockWidgetAsTab(tab->dockWidget());
    tab->setVisible(true);
}

void DockPanelView::setCurrentTabIndex(int index)
{
    IF_ASSERT_FAILED(dockWidget()) {
        return;
    }

    KDDockWidgets::Frame* frame = dockWidget()->frame();
    if (frame) {
        frame->setCurrentTabIndex(index);
    }
}
