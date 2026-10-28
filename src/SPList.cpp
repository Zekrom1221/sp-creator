#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/file.hpp>

#include "Core.hpp"
#include "SPList.hpp"
#include "Storage.hpp"
#include "Text.hpp"
#include "Transfer.hpp"

using namespace geode::prelude;
using text::Msg;

// The SP list in the pause menu: play from an SP (or the level start), rename or delete one; export SPs
// to a file, import them from one; bring back or delete versions of the level's SPs.

namespace {

// The text field may count bytes, and a Cyrillic letter is two.
constexpr size_t MAX_NAME_CHARS = 48;
constexpr float DELETE_CONFIRM_SECONDS = 2.5f;
constexpr ccColor3B WARNING_COLOR = {255, 200, 80}, NOTE_COLOR = {190, 190, 190}, NEAR_COLOR = {255, 170, 60},
    CONFIRM_COLOR = {255, 90, 90};

std::string str(Msg msg) {
    return std::string(text::tr(msg));
}

// `size` scales all of it.
CCMenuItemSpriteExtra* textButton(std::string const& caption, geode::Function<void(CCMenuItemSpriteExtra*)> callback,
    float size = 1.f, char const* texture = "GJ_button_01.png") {
    auto spr = ButtonSprite::create(caption.c_str(), 0, 0, 0.7f, false, text::fontFor(caption, true), texture, 30.f);
    spr->setScale(size);
    return CCMenuItemExt::createSpriteExtra(spr, std::move(callback));
}

// The game has no pencil (its edit button is the editor's): the mod's, on a round button as big as the
// play one at 0.32. Lying diagonally it fills its box to the corners: at 0.75 of the size the button gives
// it, its tip stays off the button's rim.
CCMenuItemSpriteExtra* renameButton(geode::Function<void(CCMenuItemSpriteExtra*)> callback) {
    auto spr = CircleButtonSprite::createWithSprite("pencil.png"_spr, 0.75f, CircleBaseColor::Green, CircleBaseSize::Medium);
    auto play = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
    if (!spr || !play) return CCMenuItemExt::createSpriteExtraWithFrameName("GJ_editBtn_001.png", 0.32f, std::move(callback));
    spr->setScale(play->getContentHeight() * 0.32f / spr->getContentHeight());
    return CCMenuItemExt::createSpriteExtra(spr, std::move(callback));
}

// The game gives menus made while popups are open the touch priority of the top one. A list rebuilt under
// another popup would get buttons that take that popup's clicks, so lists below it wait (see refresh()).
bool onTop(CCLayer* popup) {
    auto handler = CCTouchDispatcher::get()->findHandler(popup);
    return handler && handler->getPriority() == CCTouchDispatcher::get()->getTargetPrio();
}

// A click on a delete button asks for another one, for a while: `expired` then.
void confirmFor(CCNode* popup, geode::Function<void()> expired) {
    popup->stopActionByTag(1);
    auto expire = CCSequence::create(CCDelayTime::create(DELETE_CONFIRM_SECONDS), CallFuncExt::create(std::move(expired)), nullptr);
    expire->setTag(1);
    popup->runAction(expire);
}

// goldFont has no Cyrillic letters: the mod's font, coloured like it.
template <class P>
void setGoldTitle(P* popup, std::string const& title) {
    popup->setTitle(title, text::fontFor(title, true));
    if (std::string_view(text::fontFor(title, true)) != "goldFont.fnt") popup->titleLabel()->setColor({255, 210, 60});
}

// "SP 3 (45.2%)", or "3. Name (45.2%)".
std::string caption(std::string const& name, size_t number, float percent) {
    if (name.empty()) return fmt::format("SP {} ({:.1f}%)", number, percent);
    return fmt::format("{}. {} ({:.1f}%)", number, name, percent);
}

struct CheckRow {
    std::string caption;
    std::string note;  // under the caption
    ccColor3B noteColor = {190, 190, 190};
    bool enabled = true;  // false: unticked, and stays so
};

// SPs to pick from, all ticked to begin with: for export and import.
class ChecklistPopup : public Popup {
protected:
    static constexpr float WIDTH = 380.f, HEIGHT = 280.f;
    static constexpr float LIST_WIDTH = 340.f, LIST_HEIGHT = 170.f, ROW_HEIGHT = 30.f;

    std::vector<bool> m_picked;
    CCLabelBMFont* m_message = nullptr;  // over the list

    bool initList(std::string const& title, std::string const& message, ccColor3B messageColor, std::vector<CheckRow> const& rows) {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        setGoldTitle(this, title);
        m_message = text::label(message);
        m_mainLayer->addChildAtPosition(m_message, Anchor::Top, {0.f, -44.f});
        this->setMessage(message, messageColor);

        auto bg = NineSlice::create("square02b_001.png", {0, 0, 80, 80});
        bg->setColor({0, 0, 0});
        bg->setOpacity(80);
        bg->setContentSize({LIST_WIDTH + 6.f, LIST_HEIGHT + 6.f});
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, {0.f, -3.f});

        auto list = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
        list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
        m_mainLayer->addChildAtPosition(list, Anchor::Center, {-LIST_WIDTH / 2, -3.f - LIST_HEIGHT / 2});
        auto content = list->m_contentLayer;
        for (size_t i = 0; i < rows.size(); i++) {
            m_picked.push_back(rows[i].enabled);
            content->addChild(this->row(rows[i], i));
        }
        float height = std::max(LIST_HEIGHT, (ROW_HEIGHT + 2.f) * static_cast<float>(rows.size()));
        content->setContentSize({LIST_WIDTH, height});
        content->updateLayout();
        list->scrollToTop();
        handleTouchPriority(this);
        return true;
    }

    // In the mod's language, as the first one: the label keeps the font it was made with.
    void setMessage(std::string const& message, ccColor3B color) {
        m_message->setString(message.c_str());
        m_message->setColor(color);
        m_message->limitLabelWidth(LIST_WIDTH, 0.45f, 0.1f);
    }

    CCNode* row(CheckRow const& r, size_t i) {
        auto node = CCNode::create();
        node->setContentSize({LIST_WIDTH, ROW_HEIGHT});
        auto menu = CCMenu::create();
        menu->setContentSize({ROW_HEIGHT, ROW_HEIGHT});
        menu->ignoreAnchorPointForPosition(false);
        auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [this, i](CCMenuItemToggler* t) {
            // Called before it switches.
            m_picked[i] = !t->isToggled();
        });
        toggler->toggle(r.enabled);
        toggler->setEnabled(r.enabled);
        menu->addChildAtPosition(toggler, Anchor::Center, {}, false);
        node->addChildAtPosition(menu, Anchor::Left, {22.f, 0.f}, false);

        bool noted = !r.note.empty();
        auto label = text::label(r.caption);
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(LIST_WIDTH - 60.f, noted ? 0.42f : 0.5f, 0.1f);
        if (!r.enabled) label->setOpacity(120);
        node->addChildAtPosition(label, Anchor::Left, {42.f, noted ? 5.f : 0.f}, false);
        if (noted) {
            auto note = text::label(r.note);
            note->setAnchorPoint({0.f, 0.5f});
            note->limitLabelWidth(LIST_WIDTH - 60.f, 0.32f, 0.1f);
            note->setColor(r.noteColor);
            node->addChildAtPosition(note, Anchor::Left, {42.f, -8.f}, false);
        }
        return node;
    }

    bool anyPicked() const {
        return std::find(m_picked.begin(), m_picked.end(), true) != m_picked.end();
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;
};

class ExportPopup : public ChecklistPopup {
protected:
    std::vector<core::SP> m_sps;

    bool init() {
        m_sps = core::sps();
        std::vector<CheckRow> rows;
        for (size_t i = 0; i < m_sps.size(); i++) rows.push_back({caption(m_sps[i]->name, i + 1, m_sps[i]->percent)});
        if (!this->initList(str(Msg::ExportTitle), "", WARNING_COLOR, rows)) return false;
        auto exportBtn = textButton(str(Msg::Export), [this](auto) { this->onExport(); });
        exportBtn->setID("export-picked-button"_spr);
        m_buttonMenu->addChildAtPosition(exportBtn, Anchor::Bottom, {0.f, 26.f});
        return true;
    }

    void onExport() {
        std::vector<core::SP> picked;
        for (size_t i = 0; i < m_sps.size(); i++) {
            if (m_picked[i]) picked.push_back(m_sps[i]);
        }
        if (picked.empty()) return core::notify(NotificationIcon::Warning, str(Msg::NothingSelected));
        int packed = 0;
        auto data = transfer::pack(picked, packed);
        if (data.isErr()) return core::notify(NotificationIcon::Error, data.unwrapErr());
        auto level = storage::level();
        auto bytes = std::make_shared<ByteVector>(std::move(data).unwrap());
        bool opened = transfer::chooseFile(true, level ? level->name : "", [bytes, packed](std::optional<std::filesystem::path> path) {
            if (!path) return;
            if (auto written = file::writeBinarySafe(*path, *bytes); written.isErr()) {
                log::error("Export: can't write {}: {}", *path, written.unwrapErr());
                return core::notify(NotificationIcon::Error, str(Msg::ExportFailed));
            }
            log::info("Exported {} SPs to {}", packed, *path);
            core::notify(NotificationIcon::Success, text::format(Msg::Exported, packed));
        });
        // The SPs are in hand: the dialog finishes without the list.
        if (opened) this->onClose(nullptr);
    }

public:
    static ExportPopup* create() {
        auto ret = new ExportPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// "Replace all" with SPs in the level: they go, saved as a version or deleted. Closing it cancels.
class ReplacePopup : public Popup {
protected:
    static constexpr float WIDTH = 360.f, HEIGHT = 160.f;

    geode::Function<void(core::Replace)> m_then;

    bool init(size_t count, geode::Function<void(core::Replace)> then) {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        m_then = std::move(then);
        setGoldTitle(this, str(Msg::Replace));
        auto question = text::label(text::format(Msg::ReplaceQuestion, count));
        question->limitLabelWidth(WIDTH - 40.f, 0.5f, 0.1f);
        m_mainLayer->addChildAtPosition(question, Anchor::Center, {0.f, 16.f});
        auto hint = text::label(str(Msg::ReplaceHint));
        hint->limitLabelWidth(WIDTH - 40.f, 0.38f, 0.1f);
        hint->setColor(NOTE_COLOR);
        m_mainLayer->addChildAtPosition(hint, Anchor::Center, {0.f, -4.f});

        auto save = textButton(str(Msg::SaveAndReplace), [this](auto) { this->pick(core::Replace::SaveVersion); }, 0.75f);
        auto remove = textButton(str(Msg::DeleteAndReplace), [this](auto) { this->pick(core::Replace::Delete); }, 0.75f,
            "GJ_button_06.png");
        save->setID("save-and-replace-button"_spr);
        remove->setID("delete-and-replace-button"_spr);
        auto menu = CCMenu::create();
        menu->setContentSize({WIDTH - 20.f, 40.f});
        menu->setLayout(RowLayout::create()->setGap(10.f)->setAutoScale(true));
        menu->addChild(save);
        menu->addChild(remove);
        menu->updateLayout();
        m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, {0.f, 30.f});
        return true;
    }

    void pick(core::Replace how) {
        auto then = std::move(m_then);
        this->onClose(nullptr);
        if (then) then(how);
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;

    static ReplacePopup* create(size_t count, geode::Function<void(core::Replace)> then) {
        auto ret = new ReplacePopup();
        if (ret->init(count, std::move(then))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// SPs from a file, or from a version of the level's: add the picked ones, or put them in place of the
// level's.
class ImportPopup : public ChecklistPopup {
protected:
    transfer::Pack m_pack;
    geode::Function<void()> m_done;

    bool init(transfer::Pack pack, std::string const& title, std::string const& message, ccColor3B messageColor,
        geode::Function<void()> done) {
        m_pack = std::move(pack);
        m_done = std::move(done);
        std::vector<CheckRow> rows;
        for (size_t i = 0; i < m_pack.items.size(); i++) {
            auto const& item = m_pack.items[i];
            CheckRow row{caption(item.name, i + 1, item.header.percent)};
            if (item.repeated) {
                row.note = str(Msg::Repeated);
                row.enabled = false;
            }
            // Ticked all the same: replacing takes it.
            else if (item.here) row.note = str(Msg::AlreadyHere);
            else if (item.nearby > 0) {
                row.note = text::format(Msg::NearYours, item.nearby, item.nearbyPercent);
                row.noteColor = NEAR_COLOR;
            }
            rows.push_back(std::move(row));
        }
        if (!this->initList(title, message, messageColor, rows)) return false;
        auto add = textButton(str(Msg::Add), [this](auto) { this->finish(core::Replace::No); });
        auto replace = textButton(str(Msg::Replace), [this](auto) { this->onReplace(); });
        add->setID("add-button"_spr);
        replace->setID("replace-all-button"_spr);
        m_buttonMenu->addChildAtPosition(add, Anchor::Bottom, {-70.f, 26.f});
        m_buttonMenu->addChildAtPosition(replace, Anchor::Bottom, {70.f, 26.f});
        return true;
    }

    void onReplace() {
        if (!this->anyPicked()) return core::notify(NotificationIcon::Warning, str(Msg::NothingSelected));
        auto own = core::sps().size();
        if (own == 0) return this->finish(core::Replace::No);
        Ref<ImportPopup> self = this;
        if (auto popup = ReplacePopup::create(own, [self](core::Replace how) { self->finish(how); })) popup->show();
    }

    void finish(core::Replace replace) {
        if (!this->anyPicked()) return core::notify(NotificationIcon::Warning, str(Msg::NothingSelected));
        auto result = transfer::take(m_pack, m_picked, replace);
        // Adding, and all of them are here.
        if (result.added == 0 && result.failed == 0) return core::notify(NotificationIcon::Info, str(Msg::PickedAlreadyHere));
        if (result.failed > 0) {
            auto msg = result.saved ? Msg::ImportFailedSaved : Msg::ImportFailed;
            core::notify(NotificationIcon::Warning, text::format(msg, result.failed));
        }
        else if (result.notMoved > 0) {
            // In a version already, the ones left only couldn't be deleted.
            auto msg = replace == core::Replace::SaveVersion && !result.existing ? Msg::NotSetAside : Msg::NotDeleted;
            core::notify(NotificationIcon::Warning, text::format(msg, result.added, result.notMoved));
        }
        else if (result.existing) core::notify(NotificationIcon::Success, text::format(Msg::ImportedInVersion, result.added));
        else if (result.saved) core::notify(NotificationIcon::Success, text::format(Msg::ImportedSaved, result.added));
        else core::notify(NotificationIcon::Success, text::format(Msg::Imported, result.added));
        if (m_done) m_done();
        this->onClose(nullptr);
    }

public:
    static ImportPopup* create(transfer::Pack pack, std::string const& title, std::string const& message, ccColor3B messageColor,
        geode::Function<void()> done) {
        auto ret = new ImportPopup();
        if (ret->init(std::move(pack), title, message, messageColor, std::move(done))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    // From a file: someone else's SPs.
    static ImportPopup* fromFile(transfer::Pack pack, geode::Function<void()> done) {
        return create(std::move(pack), str(Msg::ImportTitle), str(Msg::ImportWarning), WARNING_COLOR, std::move(done));
    }
};

// A name for an SP or a version: `save` gets it, trimmed (empty: no name).
class RenamePopup : public Popup {
protected:
    TextInput* m_input = nullptr;
    geode::Function<void(std::string)> m_save;

    bool init(std::string const& title, std::string const& name, geode::Function<void(std::string)> save) {
        if (!Popup::init(260.f, 130.f)) return false;
        m_save = std::move(save);
        setGoldTitle(this, title);

        m_input = TextInput::create(200.f, str(Msg::NamePlaceholder), text::font());
        // The game's own filters are ASCII only; the mod's font has Cyrillic letters too.
        m_input->setFilter(std::string(getCommonFilterAllowedChars(CommonFilter::Any)) +
            "абвгдеёжзийклмнопрстуфхцчшщъыьэюяАБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ");
        m_input->setMaxCharCount(MAX_NAME_CHARS);
        m_input->setString(name);
        m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0.f, 3.f});

        auto saveBtn = textButton(str(Msg::Save), [this](auto) { this->onSave(); });
        saveBtn->setID("save-name-button"_spr);
        m_buttonMenu->addChildAtPosition(saveBtn, Anchor::Bottom, {0.f, 24.f});
        return true;
    }

    void onSave() {
        auto name = std::string(m_input->getString());
        if (m_save) m_save(utils::string::trim(name));
        this->onClose(nullptr);
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;

    static RenamePopup* create(std::string const& title, std::string const& name, geode::Function<void(std::string)> save) {
        auto ret = new RenamePopup();
        if (ret->init(title, name, std::move(save))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    static RenamePopup* forSP(core::SP sp, geode::Function<void()> done) {
        auto name = sp->name;
        return create(str(Msg::RenameTitle), name, [sp = std::move(sp), done = std::move(done)](std::string name) mutable {
            core::rename(sp, std::move(name));
            if (done) done();
        });
    }
};

// The level's versions, newest first: bring one back through the import list, or delete it.
class VersionsPopup : public Popup {
protected:
    static constexpr float WIDTH = 380.f, HEIGHT = 280.f;
    static constexpr float LIST_WIDTH = 340.f, LIST_HEIGHT = 180.f, ROW_HEIGHT = 34.f;

    ScrollLayer* m_list = nullptr;
    CCLabelBMFont* m_total = nullptr;
    std::filesystem::path m_confirming;  // delete pressed once on this version
    geode::Function<void()> m_changed;   // the level's SPs changed
    bool m_stale = false;                // to rebuild once on top

    void refresh() {
        m_stale = true;
        this->rebuildIfOnTop(0.f);
    }

    void rebuildIfOnTop(float) {
        if (!m_stale || !onTop(this)) return;
        m_stale = false;
        this->rebuild();
    }

    bool init(geode::Function<void()> changed) {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        m_changed = std::move(changed);
        setGoldTitle(this, str(Msg::VersionsTitle));
        m_total = text::label(str(Msg::VersionsTitle));
        m_total->setColor(NOTE_COLOR);
        m_mainLayer->addChildAtPosition(m_total, Anchor::Top, {0.f, -44.f});

        auto bg = NineSlice::create("square02b_001.png", {0, 0, 80, 80});
        bg->setColor({0, 0, 0});
        bg->setOpacity(80);
        bg->setContentSize({LIST_WIDTH + 6.f, LIST_HEIGHT + 6.f});
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, {0.f, -14.f});
        m_list = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
        m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
        m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {-LIST_WIDTH / 2, -14.f - LIST_HEIGHT / 2});
        this->rebuild();
        this->schedule(schedule_selector(VersionsPopup::rebuildIfOnTop));
        return true;
    }

    static std::string reason(storage::Version const& v) {
        return str(v.replaced ? Msg::VersionReplaced : Msg::VersionOld);
    }

    static std::string info(storage::Version const& v) {
        return text::format(Msg::VersionInfo, v.sps, text::size(v.bytes));
    }

    CCNode* row(storage::Version const& v, size_t i) {
        auto node = CCNode::create();
        node->setContentSize({LIST_WIDTH, ROW_HEIGHT});
        bool confirming = m_confirming == v.dir;
        // A named one has its date under the name, without the reason (nearly always "Replace all").
        bool named = !v.name.empty();
        auto title = named ? v.name : fmt::format("{}, {}", text::date(v.time), reason(v));
        auto label = text::label(confirming ? str(Msg::ConfirmDelete) : title);
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(LIST_WIDTH - 120.f, 0.42f, 0.1f);
        if (confirming) label->setColor(CONFIRM_COLOR);
        node->addChildAtPosition(label, Anchor::Left, {10.f, 6.f}, false);
        auto under = named ? fmt::format("{}, {}", text::date(v.time), info(v)) : info(v);
        auto note = text::label(v.fits ? under : fmt::format("{}, {}", under, str(Msg::VersionDeleteOnly)));
        note->setAnchorPoint({0.f, 0.5f});
        note->limitLabelWidth(LIST_WIDTH - 120.f, 0.32f, 0.1f);
        note->setColor(v.fits ? NOTE_COLOR : NEAR_COLOR);
        node->addChildAtPosition(note, Anchor::Left, {10.f, -8.f}, false);

        auto menu = CCMenu::create();
        menu->setContentSize({110.f, ROW_HEIGHT});
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({1.f, 0.5f});
        menu->setLayout(RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::End));
        if (v.fits) {
            auto open = textButton(str(Msg::Open), [this, v](auto) { this->onOpen(v); }, 0.6f);
            open->setID(fmt::format("{}/open-version-{}", Mod::get()->getID(), i));
            menu->addChild(open);
        }
        auto rename = renameButton([this, v](auto) { this->onRename(v); });
        rename->setID(fmt::format("{}/rename-version-{}", Mod::get()->getID(), i));
        menu->addChild(rename);
        auto remove = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_trashBtn_001.png", 0.55f, [this, v](auto) {
            this->onDelete(v);
        });
        remove->setID(fmt::format("{}/delete-version-{}", Mod::get()->getID(), i));
        menu->addChild(remove);
        menu->updateLayout();
        node->addChildAtPosition(menu, Anchor::Right, {-6.f, 0.f}, false);
        return node;
    }

    void rebuild() {
        auto versions = storage::versions();
        uintmax_t total = 0;
        for (auto const& v : versions) total += v.bytes;
        m_total->setString(text::format(Msg::VersionsTotal, versions.size(), text::size(total)).c_str());
        m_total->limitLabelWidth(LIST_WIDTH, 0.45f, 0.1f);

        auto content = m_list->m_contentLayer;
        content->removeAllChildren();
        for (size_t i = 0; i < versions.size(); i++) content->addChild(this->row(versions[i], i));
        if (versions.empty()) {
            auto hint = text::label(str(Msg::VersionsEmpty));
            hint->limitLabelWidth(LIST_WIDTH - 20.f, 0.4f, 0.1f);
            auto holder = CCNode::create();
            holder->setContentSize({LIST_WIDTH, ROW_HEIGHT});
            holder->addChildAtPosition(hint, Anchor::Center, {}, false);
            content->addChild(holder);
        }
        float height = std::max(LIST_HEIGHT, (ROW_HEIGHT + 2.f) * content->getChildrenCount());
        content->setContentSize({LIST_WIDTH, height});
        content->updateLayout();
        m_list->scrollToTop();
        handleTouchPriority(this);
    }

    void onOpen(storage::Version const& v) {
        m_confirming.clear();
        auto pack = transfer::openVersion(v);
        if (pack.isErr()) return core::notify(NotificationIcon::Warning, pack.unwrapErr());
        if (pack.unwrap().sameAsLevel) return core::notify(NotificationIcon::Info, str(Msg::VersionHere));
        Ref<VersionsPopup> self = this;
        bool named = !v.name.empty();
        auto title = named ? v.name : text::format(Msg::VersionTitle, text::date(v.time));
        auto message = named ? fmt::format("{}, {}, {}", text::date(v.time), reason(v), info(v)) : fmt::format("{}, {}", reason(v), info(v));
        auto popup = ImportPopup::create(std::move(pack).unwrap(), title, message, NOTE_COLOR, [self] {
                // Replacing may have made a version.
                self->refresh();
                if (self->m_changed) self->m_changed();
            });
        if (popup) popup->show();
    }

    void onRename(storage::Version const& v) {
        this->stopActionByTag(1);
        m_confirming.clear();
        this->refresh();
        Ref<VersionsPopup> self = this;
        auto popup = RenamePopup::create(str(Msg::RenameVersion), v.name, [self, v](std::string name) {
            if (!storage::renameVersion(v, name)) core::notify(NotificationIcon::Warning, str(Msg::VersionNotRenamed));
            self->refresh();
        });
        if (popup) popup->show();
    }

    void onDelete(storage::Version const& v) {
        if (m_confirming != v.dir) {
            m_confirming = v.dir;
            this->refresh();
            confirmFor(this, [this] {
                m_confirming.clear();
                this->refresh();
            });
            return;
        }
        this->stopActionByTag(1);
        m_confirming.clear();
        bool removed = storage::removeVersion(v);
        core::notify(removed ? NotificationIcon::Success : NotificationIcon::Warning,
            str(removed ? Msg::VersionDeleted : Msg::VersionNotDeleted));
        this->refresh();
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;

    static VersionsPopup* create(geode::Function<void()> changed) {
        auto ret = new VersionsPopup();
        if (ret->init(std::move(changed))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class SPListPopup : public Popup {
protected:
    static constexpr float WIDTH = 380.f, HEIGHT = 290.f;
    static constexpr float LIST_WIDTH = 340.f, LIST_HEIGHT = 180.f, ROW_HEIGHT = 32.f;

    PauseLayer* m_pause = nullptr;
    ScrollLayer* m_list = nullptr;
    core::SP m_confirming;  // delete pressed once on this SP
    bool m_stale = false;   // to rebuild once on top

public:
    void refresh() {
        m_stale = true;
        this->rebuildIfOnTop(0.f);
    }

protected:
    void rebuildIfOnTop(float) {
        if (!m_stale || !onTop(this)) return;
        m_stale = false;
        this->rebuild();
    }

    bool init(PauseLayer* pause) {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        m_pause = pause;
        setGoldTitle(this, str(Msg::ListTitle));

        auto bg = NineSlice::create("square02b_001.png", {0, 0, 80, 80});
        bg->setColor({0, 0, 0});
        bg->setOpacity(80);
        bg->setContentSize({LIST_WIDTH + 6.f, LIST_HEIGHT + 6.f});
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, {0.f, 3.f});

        m_list = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
        m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
        m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {-LIST_WIDTH / 2, 3.f - LIST_HEIGHT / 2});

        auto exportBtn = textButton(str(Msg::Export), [this](auto) { this->onExport(); });
        auto importBtn = textButton(str(Msg::Import), [this](auto) { this->onImport(); });
        auto versionsBtn = textButton(str(Msg::Versions), [this](auto) { this->onVersions(); });
        exportBtn->setID("export-button"_spr);
        importBtn->setID("import-button"_spr);
        versionsBtn->setID("versions-button"_spr);
        m_buttonMenu->addChildAtPosition(exportBtn, Anchor::Bottom, {-115.f, 28.f});
        m_buttonMenu->addChildAtPosition(importBtn, Anchor::Bottom, {0.f, 28.f});
        m_buttonMenu->addChildAtPosition(versionsBtn, Anchor::Bottom, {115.f, 28.f});
        this->rebuild();
        this->schedule(schedule_selector(SPListPopup::rebuildIfOnTop));
        return true;
    }

    void onVersions() {
        if (!storage::level()) return core::notify(NotificationIcon::Warning, str(Msg::CantSave));
        Ref<SPListPopup> self = this;
        if (auto popup = VersionsPopup::create([self] { self->refresh(); })) popup->show();
    }

    void onExport() {
        if (core::sps().empty()) return core::notify(NotificationIcon::Warning, str(Msg::NoSPs));
        if (!storage::level()) return core::notify(NotificationIcon::Warning, str(Msg::CantSave));
        if (auto popup = ExportPopup::create()) popup->show();
    }

    void onImport() {
        if (!storage::level()) return core::notify(NotificationIcon::Warning, str(Msg::CantSave));
        int session = core::session();
        Ref<SPListPopup> self = this;
        transfer::chooseFile(false, "", [self, session](std::optional<std::filesystem::path> path) {
            // Cancelled, or the level was left (or the game went on) meanwhile.
            auto pl = PlayLayer::get();
            if (!path || core::session() != session || !pl || !pl->m_isPaused) return;
            self->onFile(*path);
        });
    }

    void onFile(std::filesystem::path const& path) {
        auto pack = transfer::open(path);
        if (pack.isErr()) return core::notify(NotificationIcon::Warning, pack.unwrapErr());
        if (pack.unwrap().sameAsLevel) return core::notify(NotificationIcon::Info, str(Msg::AllAlreadyHere));
        Ref<SPListPopup> self = this;
        if (auto popup = ImportPopup::fromFile(std::move(pack).unwrap(), [self] { self->refresh(); })) popup->show();
    }

    CCNode* row(core::SP const& sp, int number) {
        auto node = CCNode::create();
        node->setContentSize({LIST_WIDTH, ROW_HEIGHT});
        bool active = core::active() == sp;
        bool confirming = sp && m_confirming == sp;

        std::string shown;
        if (confirming) shown = str(Msg::ConfirmDelete);
        else if (!sp) shown = str(Msg::ListLevelStart);
        else shown = caption(sp->name, static_cast<size_t>(number), sp->percent);
        auto label = text::label(shown);
        label->setAnchorPoint({0.f, 0.5f});
        label->limitLabelWidth(LIST_WIDTH - 120.f, 0.5f, 0.1f);
        if (confirming) label->setColor({255, 90, 90});
        else if (active) label->setColor({120, 255, 120});
        node->addChildAtPosition(label, Anchor::Left, {10.f, 0.f}, false);

        auto menu = CCMenu::create();
        menu->setContentSize({110.f, ROW_HEIGHT});
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({1.f, 0.5f});
        menu->setLayout(RowLayout::create()->setGap(6.f)->setAxisAlignment(AxisAlignment::End));
        auto play = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_playBtn2_001.png", 0.32f, [this, sp](auto) {
            this->onPlay(sp);
        });
        menu->addChild(play);
        if (sp) {
            auto rename = renameButton([this, sp](auto) { this->onRename(sp); });
            auto remove = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_trashBtn_001.png", 0.55f, [this, sp](auto) {
                this->onDelete(sp);
            });
            menu->addChild(rename);
            menu->addChild(remove);
        }
        menu->updateLayout();
        node->addChildAtPosition(menu, Anchor::Right, {-6.f, 0.f}, false);
        return node;
    }

    void rebuild() {
        auto content = m_list->m_contentLayer;
        content->removeAllChildren();
        content->addChild(this->row(nullptr, 0));
        auto const& sps = core::sps();
        for (size_t i = 0; i < sps.size(); i++) content->addChild(this->row(sps[i], static_cast<int>(i) + 1));
        if (sps.empty()) {
            auto keys = Mod::get()->getSettingValue<std::vector<Keybind>>("place-sp");
            auto hint = text::label(text::format(Msg::ListEmpty, keys.empty() ? std::string("?") : keys.front().toString()));
            hint->limitLabelWidth(LIST_WIDTH - 20.f, 0.45f, 0.1f);
            auto holder = CCNode::create();
            holder->setContentSize({LIST_WIDTH, ROW_HEIGHT});
            holder->addChildAtPosition(hint, Anchor::Center, {}, false);
            content->addChild(holder);
        }
        float height = std::max(LIST_HEIGHT, (ROW_HEIGHT + 2.f) * content->getChildrenCount());
        content->setContentSize({LIST_WIDTH, height});
        content->updateLayout();
        m_list->scrollToTop();
        handleTouchPriority(this);
    }

    void onPlay(core::SP const& sp) {
        core::prepareRestart();
        core::select(sp);
        auto pause = m_pause;
        this->onClose(nullptr);
        // Restarting resumes the game; the restart goes to the chosen SP.
        if (pause) pause->onRestart(nullptr);
    }

    void onRename(core::SP const& sp) {
        this->stopActionByTag(1);
        m_confirming = nullptr;
        this->refresh();
        Ref<SPListPopup> self = this;
        if (auto popup = RenamePopup::forSP(sp, [self] { self->refresh(); })) popup->show();
    }

    void onDelete(core::SP const& sp) {
        if (m_confirming != sp) {
            m_confirming = sp;
            this->refresh();
            confirmFor(this, [this] {
                m_confirming = nullptr;
                this->refresh();
            });
            return;
        }
        this->stopActionByTag(1);
        m_confirming = nullptr;
        core::remove(sp);
        this->refresh();
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;

    static SPListPopup* create(PauseLayer* pause) {
        auto ret = new SPListPopup();
        if (ret->init(pause)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

}

namespace splist {

void open(PauseLayer* pause) {
    if (auto popup = SPListPopup::create(pause)) popup->show();
}

void openRename(core::SP const& sp) {
    if (auto popup = RenamePopup::forSP(sp, {})) popup->show();
}

void openExport() {
    if (auto popup = ExportPopup::create()) popup->show();
}

void openImport(transfer::Pack pack) {
    // The SP list below, if it's open, shows what came in, as when the file came through its button.
    Ref<SPListPopup> list = CCScene::get()->getChildByType<SPListPopup>(0);
    if (auto popup = ImportPopup::fromFile(std::move(pack), [list] { if (list) list->refresh(); })) popup->show();
}

}

class $modify(SPCPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto pl = PlayLayer::get();
        if (!pl || pl->m_isPlatformer) return;
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) {
            log::warn("Pause menu: no right-button-menu, the SP list button is left out");
            return;
        }
        auto label = CCLabelBMFont::create("SP", "bigFont.fnt");
        auto spr = CircleButtonSprite::create(label, CircleBaseColor::Green, CircleBaseSize::Small);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this](auto) { splist::open(this); });
        btn->setID("sp-list-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }
};
