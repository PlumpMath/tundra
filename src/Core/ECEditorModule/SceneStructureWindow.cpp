/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   SceneStructureWindow.cpp
    @brief  Window with tree view of contents of scene. */

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "SceneStructureWindow.h"
#include "SceneTreeWidget.h"
#include "SceneTreeWidgetItems.h"
#include "TreeWidgetUtils.h"
#include "UndoManager.h"

#include "Framework.h"
#include "Application.h"
#include "Scene/Scene.h"
#include "Entity.h"
#include "EC_Name.h"
#include "AssetReference.h"
#include "LoggingFunctions.h"
#include "ConfigAPI.h"
#include "Profiler.h"
#include "SceneAPI.h"

#include <QTreeWidgetItemIterator>
#include <QToolButton>

#include "MemoryLeakCheck.h"

namespace
{
    const ConfigData cShowGroupsSetting(ConfigAPI::FILE_FRAMEWORK, "Scene Structure Window", "Show Groups", true);
    const ConfigData cShowComponentsSetting(ConfigAPI::FILE_FRAMEWORK, "Scene Structure Window", "Show Components", true);
    const ConfigData cAttributeVisibilitySetting(ConfigAPI::FILE_FRAMEWORK, "Scene Structure Window", "Attribute Visibility", SceneStructureWindow::ShowAssetReferences);

    inline void SetTreeWidgetItemVisible(QTreeWidgetItem *item, bool visible)
    {
        // Believe it or not but even if the item was already visible and was set
        // to visible here, Qt spent up to 30 msec per item. And thats the item
        // even not being in the tree yet! Don't remove these checks as it seems
        // QTreeWidgetItem does not do them and will update lots of state inside.
        if (item->isHidden() == visible)
            item->setHidden(!visible);
        if (item->isDisabled() == visible)
            item->setDisabled(!visible);
    }
}

SceneStructureWindow::SceneStructureWindow(Framework *fw, QWidget *parent) :
    QWidget(parent),
    framework(fw),
    showGroups(true),
    showComponents(true),
    attributeVisibility(ShowAssetReferences),
    treeWidget(0),
    expandAndCollapseButton(0),
    searchField(0),
    sortingCriteria(SortById)
{
    ConfigAPI &cfg = *framework->Config();
    showGroups = cfg.DeclareSetting(cShowGroupsSetting).toBool();
    showComponents = cfg.DeclareSetting(cShowComponentsSetting).toBool();
    attributeVisibility = static_cast<AttributeVisibilityType>(cfg.DeclareSetting(cAttributeVisibilitySetting).toUInt());

    // Init main widget
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);
    setLayout(layout);
    setWindowTitle(tr("Scene Structure"));
    resize(325, 400);

    // Create child widgets
    treeWidget = new SceneTreeWidget(fw, this);
    expandAndCollapseButton = new QPushButton(tr("Expand All"), this);
    expandAndCollapseButton->setFixedHeight(22);

    searchField = new QLineEdit(this);
    searchField->setPlaceholderText(tr("Search..."));
    searchField->setFixedHeight(20);

    QLabel *sortLabel = new QLabel(tr("Sort by"), this);
    QComboBox *sortComboBox = new QComboBox(this);
    sortComboBox->addItem(tr("ID"));
    sortComboBox->addItem(tr("Name"));
    sortComboBox->setFixedHeight(20);

    groupCheckBox = new QCheckBox(tr("Groups"), this);
    groupCheckBox->setChecked(showGroups);

    componentCheckBox = new QCheckBox(tr("Components"), this);
    componentCheckBox->setChecked(showComponents);

    QLabel *attributeLabel = new QLabel(tr("Attributes"), this);
    attributeComboBox = new QComboBox(this);
    attributeComboBox->addItem(tr("None"), DoNotShowAttributes);
    attributeComboBox->addItem(tr("Assets"), ShowAssetReferences);
    attributeComboBox->addItem(tr("Dynamic"), ShowDynamicAttributes);
//    attributeComboBox->addItem(tr("All"), ShowAllAttributes); /**< @todo Disabled temporarily until the performance problems have been solved. */
    attributeComboBox->setCurrentIndex(attributeVisibility);

    undoButton_ = new QToolButton();
    undoButton_->setPopupMode(QToolButton::MenuButtonPopup);
    undoButton_->setDisabled(true);

    redoButton_ = new QToolButton();
    redoButton_->setPopupMode(QToolButton::MenuButtonPopup);
    redoButton_->setDisabled(true);

    undoButton_->setIcon(QIcon(Application::InstallationDirectory() + "data/ui/images/icon/undo-icon.png"));
    redoButton_->setIcon(QIcon(Application::InstallationDirectory() + "data/ui/images/icon/redo-icon.png"));

    // Fill layouts
    QHBoxLayout *layoutFilterAndSort = new QHBoxLayout();
    layoutFilterAndSort->setContentsMargins(0,0,0,0);
    layoutFilterAndSort->setSpacing(5);
    layoutFilterAndSort->addWidget(searchField);
    layoutFilterAndSort->addWidget(sortLabel);
    layoutFilterAndSort->addWidget(sortComboBox);
    layoutFilterAndSort->addWidget(expandAndCollapseButton);

    QHBoxLayout *layoutSettingsVisibility = new QHBoxLayout();
    layoutSettingsVisibility->setContentsMargins(0,0,0,0);
    layoutSettingsVisibility->setSpacing(5);
    layoutSettingsVisibility->addWidget(undoButton_);
    layoutSettingsVisibility->addWidget(redoButton_);
    layoutSettingsVisibility->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Fixed));
    layoutSettingsVisibility->addWidget(groupCheckBox);
    layoutSettingsVisibility->addWidget(componentCheckBox);
    layoutSettingsVisibility->addWidget(attributeLabel);
    layoutSettingsVisibility->addWidget(attributeComboBox);

    layout->addLayout(layoutFilterAndSort);
    layout->addWidget(treeWidget);
    layout->addLayout(layoutSettingsVisibility);

    // Connect to widget signals
    connect(attributeComboBox, SIGNAL(currentIndexChanged(int)), SLOT(SetAttributeVisibilityInternal(int)));
    connect(groupCheckBox, SIGNAL(toggled(bool)), SLOT(ShowGroups(bool)));
    connect(componentCheckBox, SIGNAL(toggled(bool)), SLOT(ShowComponents(bool)));
    connect(sortComboBox, SIGNAL(currentIndexChanged(int)), SLOT(Sort(int)));
    connect(searchField, SIGNAL(textEdited(const QString &)), SLOT(Search(const QString &)));
    connect(expandAndCollapseButton, SIGNAL(clicked()), SLOT(ExpandOrCollapseAll()));
    connect(treeWidget, SIGNAL(itemCollapsed(QTreeWidgetItem*)), SLOT(CheckTreeExpandStatus(QTreeWidgetItem*)));
    connect(treeWidget, SIGNAL(itemExpanded(QTreeWidgetItem*)), SLOT(CheckTreeExpandStatus(QTreeWidgetItem*)));

    connect(framework->Scene(), SIGNAL(SceneAboutToBeRemoved(Scene *, AttributeChange::Type)), SLOT(OnSceneRemoved(Scene *)));
}

SceneStructureWindow::~SceneStructureWindow()
{
    ConfigAPI &cfg = *framework->Config();
    cfg.Write(cShowGroupsSetting, cShowGroupsSetting.key, showGroups);
    cfg.Write(cShowComponentsSetting, cShowComponentsSetting.key, showComponents);
    cfg.Write(cAttributeVisibilitySetting, cAttributeVisibilitySetting.key, attributeVisibility);

    SetShownScene(ScenePtr());
}

void SceneStructureWindow::SetShownScene(const ScenePtr &newScene)
{
    if (!scene.expired() && newScene == scene.lock())
        return;

    ScenePtr previous = ShownScene();
    if (previous)
    {
        disconnect(previous.get());
        Clear();
    }

    scene = newScene;
    treeWidget->SetScene(newScene);

    if (newScene)
    {
        // Now that treeWidget has scene, it also has UndoManager.
        UndoManager *undoMgr = treeWidget->GetUndoManager();
        undoButton_->setMenu(undoMgr->UndoMenu());
        redoButton_->setMenu(undoMgr->RedoMenu());
        connect(undoMgr, SIGNAL(CanUndoChanged(bool)), this, SLOT(SetUndoEnabled(bool)), Qt::UniqueConnection);
        connect(undoMgr, SIGNAL(CanRedoChanged(bool)), this, SLOT(SetRedoEnabled(bool)), Qt::UniqueConnection);
        connect(undoButton_, SIGNAL(clicked()), undoMgr, SLOT(Undo()), Qt::UniqueConnection);
        connect(redoButton_, SIGNAL(clicked()), undoMgr, SLOT(Redo()), Qt::UniqueConnection);

        Scene* s = ShownScene().get();
        connect(s, SIGNAL(EntityAcked(Entity *, entity_id_t)), SLOT(AckEntity(Entity *, entity_id_t)), Qt::UniqueConnection);
        connect(s, SIGNAL(EntityCreated(Entity *, AttributeChange::Type)), SLOT(AddEntity(Entity *)), Qt::UniqueConnection);
        connect(s, SIGNAL(EntityTemporaryStateToggled(Entity *, AttributeChange::Type)), SLOT(UpdateEntityTemporaryState(Entity *)), Qt::UniqueConnection);
        connect(s, SIGNAL(EntityRemoved(Entity *, AttributeChange::Type)), SLOT(RemoveEntity(Entity *)), Qt::UniqueConnection);
        connect(s, SIGNAL(ComponentAdded(Entity *, IComponent *, AttributeChange::Type)), SLOT(AddComponent(Entity *, IComponent *)), Qt::UniqueConnection);
        connect(s, SIGNAL(ComponentRemoved(Entity *, IComponent *, AttributeChange::Type)), SLOT(RemoveComponent(Entity *, IComponent *)), Qt::UniqueConnection);
        connect(s, SIGNAL(SceneCleared(Scene*)), SLOT(Clear()), Qt::UniqueConnection);
        connect(s, SIGNAL(EntityParentChanged(Entity *, Entity *, AttributeChange::Type)), SLOT(UpdateEntityParent(Entity *)), Qt::UniqueConnection);

        Populate();
    }
}

void SceneStructureWindow::ShowGroups(bool show)
{
    if (scene.expired())
    {
        LogError("SceneStructureWindow::ShowGroups: Cannot create groups, Scene has expired");
        return;
    }
    // If everything uses EntityItem and EntityGroupItem APIs correctly and
    // check 'showGroups' in approptiate places, there is no need to execute if
    // 'showGroups' wont change.
    if (showGroups == show)
        return;
    showGroups = show;

    // There might be other funtionality that has disabled sorting prior
    // to this function call. Don't enable it back if we did not disable sorting.
    // This function also does not disable sorting if no actual tree changes are done.
    bool sortingIsEnabled = treeWidget->isSortingEnabled();
    bool sortingWasEnabled = sortingIsEnabled;

    QHash<EntityGroupItem*, QList<EntityItem*> > groupped;
    QList<EntityItem*> ungroupped;

    // Iterate scene once to find groupped Entities
    Scene *s = scene.lock().get();
    std::vector<shared_ptr<EC_Name> > names = s->Components<EC_Name>();
    std::vector<shared_ptr<EC_Name> >::const_iterator iter = names.begin();
    std::vector<shared_ptr<EC_Name> >::const_iterator end = names.end();
    for (;iter != end; ++iter)
    {
        const EC_Name *name = (*iter).get();
        if (!name)
            continue;
        EntityItem *item = EntityItemOfEntity(name->ParentEntity());
        if (!item)
            continue;

        const QString groupName = (name ? name->group.Get().trimmed() : "");               
        if (showGroups && !groupName.isEmpty())
        {
            EntityGroupItem *group = GetOrCreateEntityGroupItem(groupName, false);
            groupped[group].push_back(item);
        }
        else
        {
            // If groups are hidden, hide all existing ones here.
            // Only the ungroupped EntityItems are processed as we continue.
            if (!showGroups && !groupName.isEmpty())
            {
                EntityGroupItem *group = entityGroupItems[groupName];
                if (group)
                {
                    int currentRootIndex = treeWidget->indexOfTopLevelItem(group);
                    if (currentRootIndex > -1)
                    {
                        if (sortingIsEnabled)
                        {
                            sortingIsEnabled = false;
                            treeWidget->setSortingEnabled(false);
                        }
                        treeWidget->takeTopLevelItem(currentRootIndex);

                        // Remove all children so we dont have to do it later per EntityItem.
                        group->takeChildren();
                        group->ClearEntityItems(false);
                    }
                }
            }
            ungroupped.push_back(item);
        }
    }

    if (!groupped.isEmpty())
    {
        QList<QTreeWidgetItem*> unparentedGroups;
        QHash<EntityGroupItem*, QList<EntityItem*> >::const_iterator gIter = groupped.begin();
        QHash<EntityGroupItem*, QList<EntityItem*> >::const_iterator gEnd = groupped.end();
        for(; gIter != gEnd; ++gIter)
        {
            EntityGroupItem *group = gIter.key();
            const QList<EntityItem*> &children = gIter.value();
            QList<QTreeWidgetItem*> items;

            // Add children to group
            for (int ci=0, cilen=children.size(); ci<cilen; ++ci)
            {
                EntityItem *child = children[ci];
                // Already in group, no op
                if (child->parent() == group)
                    continue;

                if (sortingIsEnabled)
                {
                    sortingIsEnabled = false;
                    treeWidget->setSortingEnabled(false);
                }
                // This is very slow when going from flat view to grouped view
                // if groups have not already been created (hence group->takeChildren()
                // was not executed above). There is no batch operation for removing items.
                treeWidget->takeTopLevelItem(treeWidget->indexOfTopLevelItem(child));
                items << child;
            }

            int childCount = items.size() + group->childCount();
            int currentRootIndex = treeWidget->indexOfTopLevelItem(group);

            // Has children and is not in tree
            if (childCount > 0 && currentRootIndex < 0)
            {
                if (sortingIsEnabled)
                {
                    sortingIsEnabled = false;
                    treeWidget->setSortingEnabled(false);
                }
                // Add unparented children
                if (items.size() > 0)
                    group->addChildren(items);
                // Add EntityItem ptrs to group and update text
                group->AddEntityItems(children, false, false, true);
                // Add group to batch operation
                unparentedGroups << group;
            }
            // Has new children and is in the tree
            else if (items.size() > 0 && currentRootIndex > -1)
            {
                if (sortingIsEnabled)
                {
                    sortingIsEnabled = false;
                    treeWidget->setSortingEnabled(false);
                }
                // Add unparented children
                group->addChildren(items);
                // Add EntityItem ptrs to group and update text
                group->AddEntityItems(children, false, false, true);
            }
            // Does not have children and is in the tree
            else if (childCount <= 0 && currentRootIndex > -1)
            {
                if (sortingIsEnabled)
                {
                    sortingIsEnabled = false;
                    treeWidget->setSortingEnabled(false);
                }
                treeWidget->takeTopLevelItem(currentRootIndex);
                group->ClearEntityItems(false);
            }
        }
        // Batch add all groups to the root
        if (!unparentedGroups.isEmpty())
        {
            if (sortingIsEnabled)
            {
                sortingIsEnabled = false;
                treeWidget->setSortingEnabled(false);
            }
            treeWidget->addTopLevelItems(unparentedGroups);
        }
    }

    if (!ungroupped.isEmpty())
    {
        QList<QTreeWidgetItem*> unparentedEntities;
        for (int ugi=0, ugilen=ungroupped.size(); ugi<ugilen; ++ugi)
        {
            // Add items to batch operation if not at root.
            // We don't need to remove it from the previous group
            // parent, it was batch executed a bit above (group->takeChildren()).
            EntityItem *item = ungroupped[ugi];
            int currentRootIndex = treeWidget->indexOfTopLevelItem(item);
            if (currentRootIndex < 0)
            {
                if (sortingIsEnabled)
                {
                    sortingIsEnabled = false;
                    treeWidget->setSortingEnabled(false);
                }
                unparentedEntities << item;
            }
        }
        // Batch add all items to the root
        if (!unparentedEntities.isEmpty())
        {
            if (sortingIsEnabled)
            {
                sortingIsEnabled = false;
                treeWidget->setSortingEnabled(false);
            }
            treeWidget->addTopLevelItems(unparentedEntities);
        }
    }

    // Sort if sorting was enabled before all the above and we changed the tree.
    if (sortingWasEnabled && !treeWidget->isSortingEnabled())
        treeWidget->setSortingEnabled(true);
}

/// @todo Optimize! Reparenting attribute items way too heavy when dealing with hundreds of entities and all attribute shown 09.09.2013
void SceneStructureWindow::ShowComponents(bool show)
{
    PROFILE(SceneStructureWindow_ShowComponents)

    if (show == showComponents)
        return;

    showComponents = show;
    treeWidget->showComponents = show;

    treeWidget->setSortingEnabled(false);

    // Set component items' visiblity and reparented attribute items.
    typedef std::multimap<QTreeWidgetItem *, QTreeWidgetItem *> ParentChildMap; // (new parent, child) mapping.
    for(ComponentItemMap::const_iterator it = componentItems.begin(); it != componentItems.end(); ++it)
    {
        ParentChildMap toBeReparented;

        ComponentItem *cItem = it->second;
        EntityItem *eItem = cItem->Parent();

        if (showComponents)
        {
            // Attribute items will be parented to component item.
            for(int i = 0; i < eItem->childCount(); ++i)
            {
                AttributeItem *aItem = dynamic_cast<AttributeItem *>(eItem->child(i));
                if (aItem)
                {
                    ComponentItem *parentCompItem = ComponentItemOfComponent(aItem->ptr.Get()->Owner());
                    assert(parentCompItem);
                    toBeReparented.insert(std::make_pair(parentCompItem, aItem));
                }
            }
        }
        else
        {
            // Transfer attribute items of component item to the entity item.
            foreach(QTreeWidgetItem *aItem, cItem->takeChildren())
                toBeReparented.insert(std::make_pair(eItem, aItem));
        }

        SetTreeWidgetItemVisible(cItem, showComponents);

        // Detach from the old parent and attach to the new.
        for(ParentChildMap::const_iterator it = toBeReparented.begin(); it != toBeReparented.end(); ++it)
        {
            if (it->second->parent())
                it->second->parent()->removeChild(it->second);
            it->first->addChild(it->second);
        }
    }

    /// @todo Make attribute visibility works correctly without this potentially quite heavy call.
    SetAttributesVisible(attributeVisibility != DoNotShowAttributes);

    Refresh();

    treeWidget->setSortingEnabled(true);
}

void SceneStructureWindow::SetAttributeVisibility(AttributeVisibilityType type)
{
    PROFILE(SceneStructureWindow_SetAttributeVisibility)

    if (attributeVisibility == type)
        return;

    attributeVisibility = type;

    if (scene.expired())
    {
        Clear();
        return;
    }

    SetAttributesVisible(attributeVisibility != DoNotShowAttributes);
}

void SceneStructureWindow::SetEntitySelected(const EntityPtr &entity, bool selected)
{
    SetEntityItemSelected(EntityItemOfEntity(entity.get()), selected);
}

void SceneStructureWindow::SetEntitiesSelected(const EntityList &entities, bool selected)
{
    for(EntityList::const_iterator it = entities.begin(); it != entities.end(); ++it)
        SetEntitySelected(*it, selected);
}

void SceneStructureWindow::ClearSelectedEntites()
{
    for(EntityItemMap::const_iterator it = entityItems.begin(); it != entityItems.end(); ++it)
        SetEntityItemSelected(it->second, false);
}

void SceneStructureWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        setWindowTitle(tr("Scene Structure"));
    else
        QWidget::changeEvent(e);
}

void SceneStructureWindow::closeEvent(QCloseEvent *e)
{
    emit AboutToClose(this);
    QWidget::closeEvent(e);
}

void SceneStructureWindow::Populate()
{
    PROFILE(SceneStructureWindow_Populate)

    ScenePtr s = ShownScene();
    if (!s)
    {
        LogWarning("Scene pointer expired. Cannot populate tree widget.");
        return;
    }

    /// @todo Would reserving size for the item maps be any help?
    /*for(Scene::const_iterator eit = s->begin(); eit != s->end(); ++eit)
    {
        entityItems[eit->second.get()] = 0;
        entityItemsById[eit->first] = 0;
        const Entity::ComponentMap &components = eit->second->Components();
        for(Entity::ComponentMap::const_iterator cit = components.begin(); cit != components.end(); ++cit)
        {
            componentItems[cit->second.get()] = 0;
            const AttributeVector& attrs = cit->second->Attributes();
            for(size_t ait = 0; ait < attrs.size(); ++ait)
                attributeItems.insert(std::make_pair(attrs[ait], (AttributeItem *)0));
        }
    }*/
    // If using unordered maps:
    /*size_t numEntities = s->Entities().size();
    size_t numComps = 0, numAttrs = 0;
    for(Scene::const_iterator eit = s->begin(); eit != s->end(); ++eit)
    {
        const Entity::ComponentMap &components = eit->second->Components();
        numComps += components.size();
        for(Entity::ComponentMap::const_iterator cit = components.begin(); cit != components.end(); ++cit)
            numAttrs += cit->second->Attributes().size();
    }
    // Emulate the missing reserve() function, http://stackoverflow.com/questions/10617829/boostunordered-map-missing-reserve-like-stdunordered-map/10618264#10618264
    entityItems.rehash(std::ceil(numEntities / entityItems.max_load_factor()));
    entityItemsById.rehash(std::ceil(numEntities/ entityItemsById.max_load_factor()));
    componentItems.rehash(std::ceil(numComps / componentItems.max_load_factor()));
    attributeItems.rehash(std::ceil(numAttrs / attributeItems.max_load_factor()));
    */

    // If we have a huge amount of entities in the scene, do not create component and attribute
    // items by default.That could freeze the applications for tens of seconds or even for minutes.
    if (s->Entities().size() > 4000)
    {
        showComponents = false;
        attributeVisibility = DoNotShowAttributes;
    }

    treeWidget->setSortingEnabled(false);

    // First add entities without updating parents, as the order isn't guaranteed
    for(Scene::iterator it = s->begin(); it != s->end(); ++it)
        AddEntity((*it).second.get(), false);

    // Now do second pass to update parents
    for(Scene::iterator it = s->begin(); it != s->end(); ++it)
    {
        if (it->second->Parent())
            UpdateEntityParent((*it).second.get());
    }

    Refresh();

    treeWidget->setSortingEnabled(true);
    SortBy(sortingCriteria, treeWidget->header()->sortIndicatorOrder());
}

void SceneStructureWindow::Clear()
{
    PROFILE(SceneStructureWindow_Clear)
    treeWidget->setSortingEnabled(false);

    // Show component items before deleting them. Makes the removal of all items a lot cheaper operation.
    ShowComponents(true);

    /// @todo 28.08.2013 Check memory leak report for this file!

    treeWidget->clear(); // This deletes all child items

    /*
    for(AttributeItemMap::const_iterator it = attributeItems.begin(); it != attributeItems.end(); ++it)
    {
        AttributeItem *item = it->second;
        SAFE_DELETE(item);
    }
    */
    attributeItems.clear();

    /*
    for(ComponentItemMap::const_iterator it = componentItems.begin(); it != componentItems.end(); ++it)
    {
        ComponentItem *item = it->second;
        SAFE_DELETE(item);
    }
    */
    componentItems.clear();

    /*
    for(EntityItemMap::const_iterator it = entityItems.begin(); it != entityItems.end(); ++it)
    {
        EntityItem *item = it->second;
        // "Stealth" removal so that we don't crash in QTreeWidget internals; no point to do
        // it properly (EntityGroupItem::RemoveEntityItem) as we're deleting everything anyways.
        if (item && item->parent())
            item->parent()->removeChild(item);
        SAFE_DELETE(item);
    }
    */
    entityItems.clear();

    // entityItemsById holds only "weak" refs to EntityItems.
    entityItemsById.clear();

    /*
    for(EntityGroupItemMap::const_iterator it = entityGroupItems.begin(); it != entityGroupItems.end(); ++it)
    {
        EntityGroupItem *item = *it;
        SAFE_DELETE(item);
    }
    */

    entityGroupItems.clear();

    treeWidget->setSortingEnabled(true);
}

EntityGroupItem *SceneStructureWindow::GetOrCreateEntityGroupItem(const QString &name, bool addToTreeRoot)
{
    if (!showGroups)
        return 0;

    EntityGroupItem *groupItem = entityGroupItems[name];
    if (!groupItem)
    {
        groupItem = new EntityGroupItem(name);
        groupItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        entityGroupItems[name] = groupItem;

        SetTreeWidgetItemVisible(groupItem, showGroups);
        if (addToTreeRoot)
            treeWidget->addTopLevelItem(groupItem);
    }

    return groupItem;
}

void SceneStructureWindow::RemoveEntityGroupItem(const QString &name)
{
    EntityGroupItemMap::iterator it = entityGroupItems.find(name);
    if (it != entityGroupItems.end())
    {
        EntityGroupItem *gItem = *it;
        SAFE_DELETE(gItem);
        entityGroupItems.erase(it);
    }
}

void SceneStructureWindow::RemoveEntityGroupItem(EntityGroupItem *gItem)
{
    RemoveEntityGroupItem(gItem->GroupName());
}

EntityItem* SceneStructureWindow::EntityItemOfEntity(Entity *entity) const
{
    PROFILE(SceneStructureWindow_EntityItemOfEntity)

    if (!entity)
        return 0;

    EntityItemMap::const_iterator iter = entityItems.find(entity);
    if (iter != entityItems.end())
        return iter->second;

    return 0;
}

EntityItem* SceneStructureWindow::EntityItemById(entity_id_t id) const
{
    EntityItemIdMap::const_iterator it = entityItemsById.find(id);
    return it != entityItemsById.end() ? it->second : 0;
}

ComponentItem *SceneStructureWindow::ComponentItemOfComponent(IComponent *component) const
{
    PROFILE(SceneStructureWindow_ComponentItemOfComponent)

    if (!component)
        return 0;

    ComponentItemMap::const_iterator iter = componentItems.find(component);
    if (iter != componentItems.end())
        return iter->second;

    return 0;
}

std::vector<AttributeItem *> SceneStructureWindow::AttributeItemOfAttribute(IAttribute *attribute) const
{
    PROFILE(SceneStructureWindow_AttributeItemOfAttribute)

    std::vector<AttributeItem *> items;
    AttributeItemMapConstRange range = attributeItems.equal_range(attribute);
    for(AttributeItemMap::const_iterator it = range.first; it != range.second; ++it)
        items.push_back(it->second);

    return items;
}

void SceneStructureWindow::SetEntityItemSelected(EntityItem *item, bool selected)
{
    if (item)
    {
        QFont font = item->font(0);
        if (font.bold() == selected)
            return;
        font.setBold(selected);
        item->setFont(0, font);
    }
}

void SceneStructureWindow::Refresh()
{
    expandAndCollapseButton->setEnabled((!entityGroupItems.empty() && showGroups) || showComponents || attributeVisibility != DoNotShowAttributes);

    attributeComboBox->blockSignals(true);
    componentCheckBox->blockSignals(true);
    groupCheckBox->blockSignals(true);

    attributeComboBox->setCurrentIndex(attributeVisibility);
    componentCheckBox->setChecked(showComponents);
    groupCheckBox->setChecked(showGroups);

    attributeComboBox->blockSignals(false);
    componentCheckBox->blockSignals(false);
    groupCheckBox->blockSignals(false);

    // If we have an ongoing search, make sure that changes are takeng into account.
    if (!searchField->text().isEmpty())
        TreeWidgetSearch(treeWidget, 0, searchField->text());
}

void SceneStructureWindow::AddEntity(Entity* entity, bool setParent)
{
    PROFILE(SceneStructureWindow_AddEntity)

    if (EntityItemOfEntity(entity))
        return;

    const Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;

    EntityGroupItem *groupItem = 0;
    const QString groupName = entity->Group();
    if (!groupName.isEmpty() && showGroups)
        groupItem = GetOrCreateEntityGroupItem(groupName, true);

    EntityItem *entityItem = new EntityItem(entity->shared_from_this(), groupItem);
    entityItem->setFlags(flags);

    entityItems[entity] = entityItem;
    entityItemsById[entity->Id()] = entityItem;

    if (groupItem)
        groupItem->AddEntityItem(entityItem);
    else
        treeWidget->addTopLevelItem(entityItem);

    /// @todo 05.03.2014 Create component items only if showComponents true?
    //if (showComponents)
    {
        const Entity::ComponentMap &components = entity->Components();
        for(Entity::ComponentMap::const_iterator i = components.begin(); i != components.end(); ++i)
            AddComponent(entityItem, entity, i->second.get());
    }

    if (setParent)
        UpdateEntityParent(entity);

    Refresh();
}

void SceneStructureWindow::AckEntity(Entity* entity, entity_id_t oldId)
{
    RemoveEntityById(oldId);
    AddEntity(entity);
}

void SceneStructureWindow::UpdateEntityTemporaryState(Entity *entity)
{
    EntityItem *entItem = EntityItemOfEntity(entity);
    if (!entItem)
        return;

    entItem->SetText(entity);

    for(int j = 0; j < entItem->childCount(); ++j)
    {
        ComponentItem *compItem = dynamic_cast<ComponentItem *>(entItem->child(j));
        if (compItem && compItem->Component().get() && compItem->Parent() == entItem)
            compItem->SetText(compItem->Component().get());
    }
}

void SceneStructureWindow::RemoveEntity(Entity* entity)
{
    EntityItem *item = EntityItemOfEntity(entity);
    if (item)
        RemoveEntityItem(item);
}

void SceneStructureWindow::RemoveEntityById(entity_id_t id)
{
    EntityItem *item = EntityItemById(id);
    if (item)
        RemoveEntityItem(item);
}

void SceneStructureWindow::RemoveEntityItem(EntityItem* eItem)
{
    EntityPtr entity = eItem->Entity();
    if (entity)
    {
        entityItems.erase(entity.get());

        const Entity::ComponentMap &components = entity->Components();
        for(Entity::ComponentMap::const_iterator it = components.begin(); it != components.end(); ++it)
            RemoveComponent(eItem, 0/*entity.get()*/, it->second.get());
    }

    entityItemsById.erase(eItem->Id());

    // Remove child entity items and their component items from our internal tracking, as they will be deleted
    RemoveChildEntityItems(eItem);

    EntityGroupItem *gItem = eItem->Parent();
    SAFE_DELETE(eItem);

    // Delete entity group item if this entity being deleted is the last child
    if (gItem && gItem->childCount() == 0)
        RemoveEntityGroupItem(gItem);

    Refresh();
}

void SceneStructureWindow::RemoveChildEntityItems(EntityItem* eItem)
{
    for (int i = 0; i < eItem->childCount(); ++i)
    {
        EntityItem* childItem = dynamic_cast<EntityItem*>(eItem->child(i));
        if (childItem)
        {
            EntityPtr entity = childItem->Entity();
            if (entity)
            {
                const Entity::ComponentMap &components = entity->Components();
                for(Entity::ComponentMap::const_iterator it = components.begin(); it != components.end(); ++it)
                    RemoveComponent(eItem, 0/*entity.get()*/, it->second.get());
            }

            entityItems.erase(entity.get());
            entityItemsById.erase(childItem->Id());
            RemoveChildEntityItems(childItem);
        }
    }
}

void SceneStructureWindow::AddComponent(Entity* entity, IComponent* comp)
{
    AddComponent(EntityItemOfEntity(entity), entity, comp);
}

void SceneStructureWindow::AddComponent(EntityItem *eItem, Entity *entity, IComponent *comp)
{
    PROFILE(SceneStructureWindow_AddComponent)

    if (!eItem)
        return;

    if (ComponentItemOfComponent(comp))
        return;

    ComponentItem *cItem = new ComponentItem(comp->shared_from_this(), eItem);
    componentItems[comp] = cItem;

    SetTreeWidgetItemVisible(cItem, showComponents);

    eItem->addChild(cItem);

    connect(comp, SIGNAL(ComponentNameChanged(const QString &, const QString &)), SLOT(UpdateComponentName()), Qt::UniqueConnection);

    if (comp->TypeId() == EC_Name::ComponentTypeId)
    {
        // Retrieve entity's name from Name component. Also hook up change signal so that UI keeps synch with the name.
        eItem->SetText(entity);

        connect(comp, SIGNAL(AttributeChanged(IAttribute *, AttributeChange::Type)),
            SLOT(UpdateEntityName(IAttribute *)), Qt::UniqueConnection);
    }

    if (comp->SupportsDynamicAttributes())
    {
        // Hook to changes of dynamic attributes in order to keep the UI in sync (currently only DynamicComponent has these).
        connect(comp, SIGNAL(AttributeAdded(IAttribute *)),
            SLOT(AddDynamicAttribute(IAttribute *)), Qt::UniqueConnection);
        connect(comp, SIGNAL(AttributeAboutToBeRemoved(IAttribute *)),
            SLOT(RemoveAttribute(IAttribute *)), Qt::UniqueConnection);
        connect(comp, SIGNAL(AttributeChanged(IAttribute *, AttributeChange::Type)),
            SLOT(UpdateDynamicAttribute(IAttribute *)), Qt::UniqueConnection);
    }

    if (attributeVisibility != DoNotShowAttributes)
    {
        if (showComponents)
            CreateAttributesForItem(cItem);
        else
            CreateAttributesForItem(eItem);
    }

    Refresh();
}

void SceneStructureWindow::RemoveComponent(Entity* entity, IComponent* comp)
{
    RemoveComponent(EntityItemOfEntity(entity), entity, comp);
}

void SceneStructureWindow::RemoveComponent(EntityItem *eItem, Entity *entity, IComponent* comp)
{
    foreach(IAttribute *attr, comp->Attributes())
        RemoveAttribute(attr);

    ComponentItemMap::iterator iter = componentItems.find(comp);
    if (iter != componentItems.end())
    {
        ComponentItem *cItem = iter->second;
        if (eItem)
            eItem->removeChild(cItem);
        componentItems.erase(iter);
        SAFE_DELETE(cItem);
    }

    if (entity && eItem && comp->TypeId() == EC_Name::ComponentTypeId)
        eItem->setText(0, QString("%1").arg(entity->Id())); /**< @todo Doesn't append the extra information, but cannot call SetText as the EC_Name still exists. */
}

void SceneStructureWindow::CreateAttributesForItem(ComponentItem *cItem)
{
    PROFILE(SceneStructureWindow_CreateAttributesForItem_ComponentItem)

    if (cItem && cItem->Component())
        foreach(IAttribute *attr, cItem->Component()->Attributes())
            CreateAttributeItem(cItem, attr);
}

void SceneStructureWindow::CreateAttributesForItem(EntityItem *eItem)
{
    PROFILE(SceneStructureWindow_CreateAttributesForItem_EntityItem)

    if (eItem && eItem->Entity())
    {
        const Entity::ComponentMap &components = eItem->Entity()->Components();
        for(Entity::ComponentMap::const_iterator it = components.begin(); it != components.end(); ++it)
            foreach(IAttribute *attr, it->second->Attributes())
                CreateAttributeItem(eItem, attr);
    }
}

void SceneStructureWindow::SetAttributesVisible(bool show)
{
    PROFILE(SceneStructureWindow_SetAttributesVisible)

    treeWidget->setSortingEnabled(false);

    if (show)
    {
            if (showComponents)
                for(ComponentItemMap::const_iterator it = componentItems.begin(); it != componentItems.end(); ++it)
                    CreateAttributesForItem(it->second);  // Parent to component items.
            else
                for(EntityItemMap::const_iterator it = entityItems.begin(); it != entityItems.end(); ++it)
                    CreateAttributesForItem(it->second); // Parent to entity items.
    }
    else
    {
        for(AttributeItemMap::iterator it = attributeItems.begin(); it != attributeItems.end(); ++it)
            SetTreeWidgetItemVisible(it->second, false);
    }

    Refresh();

    treeWidget->setSortingEnabled(true);
}

void SceneStructureWindow::CreateAttributeItem(QTreeWidgetItem *parentItem, IAttribute *attr)
{
    PROFILE(SceneStructureWindow_CreateAttributeItem)

    std::vector<AttributeItem *> existingItems = AttributeItemOfAttribute(attr);

    if (!(attr && (attributeVisibility == ShowAllAttributes ||
        (attributeVisibility == ShowDynamicAttributes && attr->IsDynamic()) ||
        (attributeVisibility == ShowAssetReferences && (attr->TypeId() == cAttributeAssetReference ||
        attr->TypeId() == cAttributeAssetReferenceList)))))
    {
        // Item(s) for this attribute doesn't match the current showing criteria so hide them.
        for(size_t i = 0; i < existingItems.size(); ++i)
            SetTreeWidgetItemVisible(existingItems[i], false);
        return;
    }

    if (!existingItems.empty()) // Update existing items.
    {
        /// @todo 16.09.2013 AssetReferenceList items are not updated properly.
        for(size_t i = 0; i < existingItems.size(); ++i)
        {
            existingItems[i]->Update(attr);
            SetTreeWidgetItemVisible(existingItems[i], true);
        }
    }
    else // Create new items.
    {
        // Create special items for asset references.
        if (attr->TypeId() == cAttributeAssetReference || attr->TypeId() == cAttributeAssetReferenceList)
        {
            const bool visible = (attributeVisibility == ShowAllAttributes || attributeVisibility == ShowAssetReferences ||
                (attributeVisibility == ShowDynamicAttributes && attr->IsDynamic()));

            // If this attribute is an empty AssetReferenceList it will have a dummy item so
            // that it appears in the tree.
            AttributeItem *aItem = new AssetRefItem(attr, parentItem);
            attributeItems.insert(std::make_pair(attr, aItem));
            SetTreeWidgetItemVisible(aItem, visible);
            parentItem->addChild(aItem);

            if (attr->TypeId() == cAttributeAssetReferenceList)
            {
                // Fill ref for possible already existing first AssetReferenceList item.
                const AssetReferenceList &refs = static_cast<Attribute<AssetReferenceList> *>(attr)->Get();
                if (refs.Size() >= 1)
                {
                    aItem->index = 0;
                    aItem->Update(attr);
                }

                // Create new items for rest of the refs.
                for(int i = 1; i < refs.Size(); ++i)
                {
                    aItem = new AssetRefItem(attr, i, parentItem);
                    SetTreeWidgetItemVisible(aItem, visible);
                    parentItem->addChild(aItem);
                    attributeItems.insert(std::make_pair(attr, aItem));
                }
            }
        }
        else
        {
            AttributeItem *aItem = new AttributeItem(attr, parentItem);
            attributeItems.insert(std::make_pair(attr, aItem));
            const bool visible = (attributeVisibility == ShowAllAttributes || (attributeVisibility == ShowDynamicAttributes && attr->IsDynamic()));
            SetTreeWidgetItemVisible(aItem, visible);
            parentItem->addChild(aItem);
        }
    }
}

void SceneStructureWindow::AddDynamicAttribute(IAttribute *attr)
{
    if (attributeVisibility != ShowAllAttributes && attributeVisibility != ShowDynamicAttributes)
        return;

    QTreeWidgetItem *parentItem = (showComponents ? static_cast<QTreeWidgetItem *>(ComponentItemOfComponent(attr->Owner())) :
        static_cast<QTreeWidgetItem *>(EntityItemOfEntity(attr->Owner()->ParentEntity())));

    assert(parentItem);
    if (parentItem)
        CreateAttributeItem(parentItem, attr);
}

void SceneStructureWindow::RemoveAttribute(IAttribute *attr)
{
    if (!attr)
        return;

    AttributeItemMapRange range = attributeItems.equal_range(attr);
    for(AttributeItemMap::iterator it = range.first; it != range.second; ++it)
    {
        AttributeItem *item = it->second;
        SAFE_DELETE(item);
    }

    attributeItems.erase(range.first, range.second);
}

void SceneStructureWindow::UpdateDynamicAttribute(IAttribute *attr)
{
    std::vector<AttributeItem *> items = AttributeItemOfAttribute(attr);
    for(size_t i = 0; i < items.size(); ++i)
        items[i]->Update(attr);
}

void SceneStructureWindow::UpdateEntityName(IAttribute *attr)
{
    // We are only interested in EC_Name name and group attributes.
    EC_Name *nameComp = qobject_cast<EC_Name *>(sender());
    if (!nameComp || !nameComp->ParentEntity() || attr->Id() == "description")
        return;

    Entity *entity = nameComp->ParentEntity();
    EntityItem *item = EntityItemOfEntity(entity);
    if (item)
    {
        if (showGroups && nameComp->group.ValueChanged())
        {
            EntityGroupItem *oldGroup = item->Parent(), *newGroup = 0;
            const QString newGroupName = nameComp->group.Get().trimmed();
            if (!newGroupName.isEmpty())
                newGroup = GetOrCreateEntityGroupItem(newGroupName, true);

            // This should not happen as group attribute wont trigger a change
            // if the value did not actually change. But we end up here with new
            // and old groups being the same when server/network is involved in
            // a mass Entity import where groups are set.
            if (oldGroup != newGroup)
            {
                if (oldGroup)
                {
                    oldGroup->RemoveEntityItem(item);
                    if (oldGroup->childCount() == 0)
                        RemoveEntityGroupItem(oldGroup);
                }

                if (newGroup)
                    newGroup->AddEntityItem(item);
            }
        }
        if (nameComp->name.ValueChanged())
            item->SetText(entity);
    }
}

void SceneStructureWindow::UpdateComponentName()
{
    IComponent *comp = qobject_cast<IComponent *>(sender());
    ComponentItem *cItem = (comp ? ComponentItemOfComponent(comp) : 0);
    if (cItem)
        cItem->SetText(comp);
}

void SceneStructureWindow::UpdateEntityParent(Entity* entity)
{
    EntityItem* eItem = EntityItemOfEntity(entity);
    if (!eItem)
        return;
    /// \todo Will not currently work with groups
    if (eItem->Parent())
        return;

    EntityPtr parentEntity = entity->Parent();
    if (parentEntity)
    {
        EntityItem* peItem = EntityItemOfEntity(parentEntity.get());
        if (!peItem)
            return;
        if (eItem->parent() == 0)
            treeWidget->takeTopLevelItem(treeWidget->indexOfTopLevelItem(eItem));
        else
            eItem->parent()->takeChild(eItem->parent()->indexOfChild(eItem));

        peItem->addChild(eItem);
    }
    else
    {
        if (eItem->parent())
        {
            eItem->parent()->takeChild(eItem->parent()->indexOfChild(eItem));
            treeWidget->addTopLevelItem(eItem);
        }
    }
}

void SceneStructureWindow::Sort(int column)
{
    sortingCriteria = (SortCriteria)column;
    SortBy(sortingCriteria, treeWidget->header()->sortIndicatorOrder());
}

void SceneStructureWindow::SortBy(SortCriteria criteria, Qt::SortOrder order)
{
    treeWidget->sortItems((int)criteria, order);
}

void SceneStructureWindow::Search(const QString &filter)
{
    TreeWidgetSearch(treeWidget, 0, filter);
}

void SceneStructureWindow::ExpandOrCollapseAll()
{
    treeWidget->blockSignals(true);
    bool treeExpanded = TreeWidgetExpandOrCollapseAll(treeWidget);
    treeWidget->blockSignals(false);
    expandAndCollapseButton->setText(treeExpanded ? tr("Collapse All") : tr("Expand All"));
}

void SceneStructureWindow::CheckTreeExpandStatus(QTreeWidgetItem * /*item*/)
{
    expandAndCollapseButton->setText(TreeWidgetIsAnyExpanded(treeWidget) ? tr("Collapse All") : tr("Expand All"));
}

void SceneStructureWindow::SetUndoEnabled(bool canUndo)
{
    undoButton_->setEnabled(canUndo);
}

void SceneStructureWindow::SetRedoEnabled(bool canRedo)
{
    redoButton_->setEnabled(canRedo);
}

void SceneStructureWindow::OnSceneRemoved(Scene *removedScene)
{
    if (removedScene == ShownScene().get())
        SetShownScene(ScenePtr());
}
