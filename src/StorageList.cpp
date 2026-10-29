#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <algorithm>
#include <set>

#include "Core.hpp"
#include "Storage.hpp"
#include "StorageList.hpp"
#include "Text.hpp"
#include "UI.hpp"

using namespace geode::prelude;
using text::Msg;

// The storage manager, from a button in the mod's settings: every level's SPs and versions, the biggest
// first, to free up space. Several can be picked and deleted at once, by a second click. Versions come back
// only in their own level (SP list, Versions): their checkpoints load into it.

namespace {

using namespace ui;

std::string reason(storage::Version const& v) {
    return versionReason(v.why);
}

// What both lists have: rows with a checkbox, nothing picked to begin with, rebuilt (once on top) after
// deleting; deleting asks for a second click.
class PickList : public Popup {
protected:
    static constexpr float WIDTH = 400.f, HEIGHT = 290.f;
    static constexpr float LIST_WIDTH = 360.f, LIST_HEIGHT = 170.f, ROW_HEIGHT = 34.f;

    ScrollLayer* m_list = nullptr;
    CCLabelBMFont* m_total = nullptr;  // over the list
    CCMenu* m_buttons = nullptr;       // under it
    std::string m_armed;               // the delete button clicked once
    bool m_stale = false;              // to rebuild once on top
    bool m_built = false;

    bool initList(std::string const& title) {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        setGoldTitle(this, title);
        m_title->limitLabelWidth(WIDTH - 70.f, m_title->getScale(), 0.1f);
        m_total = text::label(" ");
        m_total->setColor(NOTE_COLOR);
        m_mainLayer->addChildAtPosition(m_total, Anchor::Top, {0.f, -44.f});

        auto bg = NineSlice::create("square02b_001.png", {0, 0, 80, 80});
        bg->setColor({0, 0, 0});
        bg->setOpacity(80);
        bg->setContentSize({LIST_WIDTH + 6.f, LIST_HEIGHT + 6.f});
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, {0.f, -6.f});
        m_list = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
        m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
        m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {-LIST_WIDTH / 2, -6.f - LIST_HEIGHT / 2});

        m_buttons = CCMenu::create();
        m_buttons->setContentSize({WIDTH - 30.f, 36.f});
        m_buttons->setLayout(RowLayout::create()->setGap(8.f)->setAutoScale(true));
        m_mainLayer->addChildAtPosition(m_buttons, Anchor::Bottom, {0.f, 26.f});
        this->schedule(schedule_selector(PickList::rebuildIfOnTop));
        return true;
    }

    void addButton(std::string const& caption, std::string const& id, geode::Function<void(CCMenuItemSpriteExtra*)> callback,
        char const* texture = "GJ_button_01.png") {
        auto btn = textButton(caption, std::move(callback), 0.7f, texture);
        btn->setID(id);
        m_buttons->addChild(btn);
        m_buttons->updateLayout();
    }

    void rebuildIfOnTop(float) {
        if (!m_stale || !onTop(this)) return;
        m_stale = false;
        this->rebuild();
    }

    virtual void rebuild() = 0;

    // A row: its checkbox, a caption with a note under it, and a button on the right (if any).
    CCNode* row(bool picked, bool enabled, geode::Function<void(bool)> toggled, std::string const& caption,
        std::string const& note, ccColor3B noteColor, CCMenuItemSpriteExtra* button, std::string const& id) {
        auto node = CCNode::create();
        node->setContentSize({LIST_WIDTH, ROW_HEIGHT});
        auto menu = CCMenu::create();
        menu->setContentSize({LIST_WIDTH, ROW_HEIGHT});
        menu->ignoreAnchorPointForPosition(false);
        auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [this, toggled = std::move(toggled)](CCMenuItemToggler* t) mutable {
            // Called before it switches.
            m_armed.clear();
            toggled(!t->isToggled());
        });
        toggler->toggle(picked);
        if (!enabled) disableToggler(toggler);
        toggler->setID(id);
        menu->addChildAtPosition(toggler, Anchor::Left, {22.f, 0.f}, false);
        float buttonWidth = 0.f;
        if (button) {
            buttonWidth = button->getScaledContentWidth();
            menu->addChildAtPosition(button, Anchor::Right, {-8.f - buttonWidth / 2, 0.f}, false);
        }
        node->addChildAtPosition(menu, Anchor::Center, {}, false);

        float width = LIST_WIDTH - 52.f - (button ? buttonWidth + 12.f : 0.f);
        bool noted = !note.empty();
        auto label = text::label(caption);
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(width, noted ? 0.42f : 0.5f, 0.1f);
        if (!enabled) label->setOpacity(150);
        node->addChildAtPosition(label, Anchor::Left, {42.f, noted ? 6.f : 0.f}, false);
        if (noted) {
            auto under = text::label(note);
            under->setAnchorPoint({0.f, 0.5f});
            under->limitLabelWidth(width, 0.32f, 0.1f);
            under->setColor(noteColor);
            node->addChildAtPosition(under, Anchor::Left, {42.f, -8.f}, false);
        }
        return node;
    }

    // In place of the rows there were: scrolled as far down as before.
    void setRows(std::vector<CCNode*> const& rows, std::string const& empty) {
        auto content = m_list->m_contentLayer;
        // How far down from the top it was scrolled.
        float down = m_built ? content->getPositionY() - (LIST_HEIGHT - content->getContentHeight()) : 0.f;
        content->removeAllChildren();
        for (auto r : rows) content->addChild(r);
        if (rows.empty()) {
            auto hint = text::label(empty);
            hint->limitLabelWidth(LIST_WIDTH - 20.f, 0.4f, 0.1f);
            auto holder = CCNode::create();
            holder->setContentSize({LIST_WIDTH, ROW_HEIGHT});
            holder->addChildAtPosition(hint, Anchor::Center, {}, false);
            content->addChild(holder);
        }
        float height = std::max(LIST_HEIGHT, (ROW_HEIGHT + 2.f) * content->getChildrenCount());
        content->setContentSize({LIST_WIDTH, height});
        content->updateLayout();
        float top = LIST_HEIGHT - content->getContentHeight();
        content->setPositionY(std::clamp(top + down, top, 0.f));
        m_built = true;
        handleTouchPriority(this);
    }

    // The first click on a delete button tells how much it deletes; another one within a while does it.
    bool confirmed(std::string const& what, uintmax_t bytes) {
        if (m_armed != what) {
            m_armed = what;
            core::notify(NotificationIcon::Warning, text::format(Msg::StorageConfirm, text::size(bytes)));
            confirmFor(this, [this] { m_armed.clear(); });
            return false;
        }
        this->stopActionByTag(1);
        m_armed.clear();
        return true;
    }

    static void reportDeleted(uintmax_t freed, int failed) {
        if (failed > 0) core::notify(NotificationIcon::Warning, text::format(Msg::StorageNotAll, text::size(freed), failed));
        else core::notify(NotificationIcon::Success, text::format(Msg::StorageDeleted, text::size(freed)));
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;

    void refresh() {
        m_stale = true;
        this->rebuildIfOnTop(0.f);
    }
};

// One level: its own SPs and its versions.
class LevelPopup : public PickList {
protected:
    // In m_picked, for the level's own SPs; versions go by their folder's name.
    static constexpr char const* OWN = "*";

    storage::StoredLevel m_level;
    std::set<std::string> m_picked;
    geode::Function<void()> m_changed;  // something was deleted

    bool init(storage::StoredLevel level, geode::Function<void()> changed) {
        m_level = std::move(level);
        m_changed = std::move(changed);
        if (!this->reload()) return false;
        if (!this->initList(m_level.lowDetail ? fmt::format("{} (LDM)", m_level.name) : m_level.name)) return false;
        this->addButton(str(Msg::SelectAll), "storage-level-select-all"_spr, [this](auto) { this->onSelectAll(); });
        this->addButton(str(Msg::DeletePicked), "storage-level-delete-picked"_spr, [this](auto) { this->onDelete(false); });
        this->addButton(str(Msg::DeleteEntirely), "storage-level-delete-entirely"_spr, [this](auto) { this->onDelete(true); },
            "GJ_button_06.png");
        this->fill();
        return true;
    }

    // As it's stored now; false: gone, all of it deleted.
    bool reload() {
        for (auto& l : storage::storedLevels()) {
            if (l.key != m_level.key) continue;
            m_level = std::move(l);
            return true;
        }
        return false;
    }

    static std::string nameOf(storage::Version const& v) {
        return utils::string::pathToString(v.dir.filename());
    }

    bool hasOwn() const {
        return m_level.spBytes > 0;
    }

    void rebuild() override {
        if (!this->reload()) return this->onClose(nullptr);
        this->fill();
    }

    void fill() {
        std::set<std::string> still;
        if (hasOwn() && !m_level.open && m_picked.contains(OWN)) still.insert(OWN);
        for (auto const& v : m_level.versions) {
            if (m_picked.contains(nameOf(v))) still.insert(nameOf(v));
        }
        m_picked = std::move(still);
        m_total->setString(text::format(Msg::StorageLevelTotal, text::size(m_level.bytes())).c_str());
        m_total->limitLabelWidth(LIST_WIDTH, 0.4f, 0.1f);

        std::vector<CCNode*> rows;
        auto id = [](std::string_view what) { return fmt::format("{}/storage-pick-{}", Mod::get()->getID(), what); };
        if (hasOwn()) {
            rows.push_back(this->row(m_picked.contains(OWN), !m_level.open, [this](bool on) { this->pick(OWN, on); },
                text::format(Msg::StorageCurrent, m_level.sps, text::size(m_level.spBytes)),
                m_level.open ? str(Msg::StorageOpenLevel) : "", NOTE_COLOR, nullptr, id("own")));
        }
        for (size_t i = 0; i < m_level.versions.size(); i++) {
            auto const& v = m_level.versions[i];
            bool named = !v.name.empty();
            auto info = text::format(Msg::VersionInfo, v.sps, text::size(v.bytes));
            auto note = named ? fmt::format("{}, {}", text::date(v.time), info) : info;
            if (!v.fits) note = fmt::format("{}, {}", note, str(Msg::StorageOtherVersion));
            auto key = nameOf(v);
            rows.push_back(this->row(m_picked.contains(key), true, [this, key](bool on) { this->pick(key, on); },
                named ? v.name : fmt::format("{}, {}", text::date(v.time), reason(v)), note, v.fits ? NOTE_COLOR : NEAR_COLOR,
                nullptr, id(fmt::format("version-{}", i))));
        }
        this->setRows(rows, str(Msg::StorageEmpty));
    }

    void pick(std::string const& key, bool on) {
        if (on) m_picked.insert(key);
        else m_picked.erase(key);
    }

    void onSelectAll() {
        size_t all = m_level.versions.size() + (hasOwn() && !m_level.open ? 1 : 0);
        bool wasAll = m_picked.size() == all;
        m_picked.clear();
        if (!wasAll) {
            if (hasOwn() && !m_level.open) m_picked.insert(OWN);
            for (auto const& v : m_level.versions) m_picked.insert(nameOf(v));
        }
        m_armed.clear();
        this->refresh();
    }

    // Entirely: all its versions, and its own SPs but the open level's.
    void onDelete(bool entirely) {
        if (!entirely && m_picked.empty()) return core::notify(NotificationIcon::Warning, str(Msg::NothingSelected));
        bool own = hasOwn() && !m_level.open && (entirely || m_picked.contains(OWN));
        std::vector<storage::Version> versions;
        for (auto const& v : m_level.versions) {
            if (entirely || m_picked.contains(nameOf(v))) versions.push_back(v);
        }
        if (!own && versions.empty()) return core::notify(NotificationIcon::Info, str(Msg::StorageNothing));
        uintmax_t bytes = own ? m_level.spBytes : 0;
        for (auto const& v : versions) bytes += v.bytes;
        if (!this->confirmed(entirely ? "entirely" : "picked", bytes)) return;
        uintmax_t freed = 0;
        int failed = 0;
        for (auto const& v : versions) {
            if (storage::removeStoredVersion(m_level, v)) freed += v.bytes;
            else failed++;
        }
        if (own) {
            if (storage::removeLevelSPs(m_level)) freed += m_level.spBytes;
            else failed++;
        }
        reportDeleted(freed, failed);
        if (m_changed) m_changed();
        this->refresh();
    }

public:
    static LevelPopup* create(storage::StoredLevel level, geode::Function<void()> changed) {
        auto ret = new LevelPopup();
        if (ret->init(std::move(level), std::move(changed))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// All levels, the biggest first.
class StoragePopup : public PickList {
protected:
    std::vector<storage::StoredLevel> m_levels;
    std::set<std::string> m_picked;  // level keys

    bool init() {
        if (!this->initList(str(Msg::StorageTitle))) return false;
        this->addButton(str(Msg::SelectAll), "storage-select-all"_spr, [this](auto) { this->onSelectAll(); });
        this->addButton(str(Msg::DeleteVersions), "storage-delete-versions"_spr, [this](auto) { this->onDelete(false); });
        this->addButton(str(Msg::DeleteEntirely), "storage-delete-entirely"_spr, [this](auto) { this->onDelete(true); },
            "GJ_button_06.png");
        this->rebuild();
        return true;
    }

    void rebuild() override {
        m_levels = storage::storedLevels();
        std::set<std::string> still;
        uintmax_t total = 0;
        for (auto const& l : m_levels) {
            total += l.bytes();
            if (m_picked.contains(l.key)) still.insert(l.key);
        }
        m_picked = std::move(still);
        m_total->setString(text::format(Msg::StorageTotal, m_levels.size(), text::size(total)).c_str());
        m_total->limitLabelWidth(LIST_WIDTH, 0.45f, 0.1f);

        std::vector<CCNode*> rows;
        for (size_t i = 0; i < m_levels.size(); i++) {
            auto const& l = m_levels[i];
            auto key = l.key;
            auto open = textButton(str(Msg::Open), [this, key](auto) { this->onOpen(key); }, 0.6f);
            open->setID(fmt::format("{}/storage-open-{}", Mod::get()->getID(), i));
            auto note = text::format(Msg::StorageLevelInfo, text::size(l.bytes()), l.sps, l.versions.size());
            if (l.open) note = fmt::format("{}, {}", note, str(Msg::StorageOpenLevel));
            rows.push_back(this->row(m_picked.contains(key), true, [this, key](bool on) { this->pick(key, on); },
                l.lowDetail ? fmt::format("{} (LDM)", l.name) : l.name, note, NOTE_COLOR, open,
                fmt::format("{}/storage-pick-{}", Mod::get()->getID(), i)));
        }
        this->setRows(rows, str(Msg::StorageEmpty));
    }

    void pick(std::string const& key, bool on) {
        if (on) m_picked.insert(key);
        else m_picked.erase(key);
    }

    void onSelectAll() {
        bool wasAll = m_picked.size() == m_levels.size();
        m_picked.clear();
        if (!wasAll) {
            for (auto const& l : m_levels) m_picked.insert(l.key);
        }
        m_armed.clear();
        this->refresh();
    }

    // Of the picked levels: their versions; entirely, their own SPs too, but the open level's.
    void onDelete(bool entirely) {
        std::vector<storage::StoredLevel const*> picked;
        for (auto const& l : m_levels) {
            if (m_picked.contains(l.key)) picked.push_back(&l);
        }
        if (picked.empty()) return core::notify(NotificationIcon::Warning, str(Msg::NothingSelected));
        auto ownGoes = [entirely](storage::StoredLevel const& l) { return entirely && !l.open && l.spBytes > 0; };
        uintmax_t bytes = 0;
        bool any = false;
        for (auto l : picked) {
            bytes += l->versionBytes + (ownGoes(*l) ? l->spBytes : 0);
            any = any || !l->versions.empty() || ownGoes(*l);
        }
        if (!any) return core::notify(NotificationIcon::Info, str(Msg::StorageNothing));
        if (!this->confirmed(entirely ? "entirely" : "versions", bytes)) return;
        uintmax_t freed = 0;
        int failed = 0;
        for (auto l : picked) {
            for (auto const& v : l->versions) {
                if (storage::removeStoredVersion(*l, v)) freed += v.bytes;
                else failed++;
            }
            if (ownGoes(*l)) {
                if (storage::removeLevelSPs(*l)) freed += l->spBytes;
                else failed++;
            }
        }
        reportDeleted(freed, failed);
        this->refresh();
    }

    void onOpen(std::string const& key) {
        m_armed.clear();
        for (auto const& l : m_levels) {
            if (l.key != key) continue;
            Ref<StoragePopup> self = this;
            if (auto popup = LevelPopup::create(l, [self] { self->refresh(); })) popup->show();
            return;
        }
    }

public:
    static StoragePopup* create() {
        auto ret = new StoragePopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// The setting with the button: no value of its own.
class StorageSetting : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(std::string key, std::string modID, matjson::Value const& json) {
        auto ret = std::make_shared<StorageSetting>();
        auto root = checkJson(json, "StorageSetting");
        ret->init(std::move(key), std::move(modID), root);
        ret->parseNameAndDescription(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(ret));
    }

    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class StorageSettingNode : public SettingNodeV3 {
protected:
    bool init(std::shared_ptr<StorageSetting> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        auto open = textButton(str(Msg::Open), [](auto) { storagelist::open(); }, 0.6f);
        open->setID("storage-button"_spr);
        this->getButtonMenu()->addChildAtPosition(open, Anchor::Right, {-open->getScaledContentWidth() / 2 - 5.f, 0.f});
        this->getButtonMenu()->updateLayout();
        this->updateState(nullptr);
        return true;
    }

    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static StorageSettingNode* create(std::shared_ptr<StorageSetting> setting, float width) {
        auto ret = new StorageSettingNode();
        if (ret->init(std::move(setting), width)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }
};

SettingNodeV3* StorageSetting::createNode(float width) {
    return StorageSettingNode::create(std::static_pointer_cast<StorageSetting>(shared_from_this()), width);
}

}

$execute {
    (void)Mod::get()->registerCustomSettingType("storage", &StorageSetting::parse);
}

namespace storagelist {

void open() {
    if (auto popup = StoragePopup::create()) popup->show();
}

}
