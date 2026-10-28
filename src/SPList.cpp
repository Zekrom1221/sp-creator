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
// to a file, import them from one.

namespace {

// The text field may count bytes, and a Cyrillic letter is two.
constexpr size_t MAX_NAME_CHARS = 48;
constexpr float DELETE_CONFIRM_SECONDS = 2.5f;

std::string str(Msg msg) {
    return std::string(text::tr(msg));
}

CCMenuItemSpriteExtra* textButton(std::string const& caption, geode::Function<void(CCMenuItemSpriteExtra*)> callback) {
    auto spr = ButtonSprite::create(caption.c_str(), 0, 0, 0.7f, false, text::fontFor(caption, true), "GJ_button_01.png", 30.f);
    return CCMenuItemExt::createSpriteExtra(spr, std::move(callback));
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

    bool initList(std::string const& title, std::string const& message, std::vector<CheckRow> const& rows) {
        if (!Popup::init(WIDTH, HEIGHT)) return false;
        setGoldTitle(this, title);
        m_message = text::label(message);
        m_mainLayer->addChildAtPosition(m_message, Anchor::Top, {0.f, -44.f});
        this->setMessage(message, {255, 200, 80});

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
        if (!this->initList(str(Msg::ExportTitle), "", rows)) return false;
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

class ImportPopup : public ChecklistPopup {
protected:
    transfer::Pack m_pack;
    geode::Function<void()> m_done;
    bool m_confirming = false;  // Replace pressed once

    bool init(transfer::Pack pack, geode::Function<void()> done) {
        m_pack = std::move(pack);
        m_done = std::move(done);
        std::vector<CheckRow> rows;
        for (size_t i = 0; i < m_pack.items.size(); i++) {
            auto const& item = m_pack.items[i];
            CheckRow row{caption(item.name, i + 1, item.header.percent)};
            if (item.duplicate) {
                row.note = str(Msg::AlreadyHere);
                row.enabled = false;
            }
            else if (item.nearby > 0) {
                row.note = text::format(Msg::NearYours, item.nearby, item.nearbyPercent);
                row.noteColor = {255, 170, 60};
            }
            rows.push_back(std::move(row));
        }
        if (!this->initList(str(Msg::ImportTitle), str(Msg::ImportWarning), rows)) return false;
        auto add = textButton(str(Msg::Add), [this](auto) { this->finish(false); });
        auto replace = textButton(str(Msg::Replace), [this](auto) { this->onReplace(); });
        m_buttonMenu->addChildAtPosition(add, Anchor::Bottom, {-70.f, 26.f});
        m_buttonMenu->addChildAtPosition(replace, Anchor::Bottom, {70.f, 26.f});
        return true;
    }

    void onReplace() {
        // The level's SPs the file holds too stay.
        size_t aside = core::sps().size() - m_pack.inFile.size();
        if (aside > 0 && !m_confirming) {
            m_confirming = true;
            this->setMessage(text::format(Msg::ConfirmReplace, aside), {255, 90, 90});
            this->stopActionByTag(1);
            auto expire = CCSequence::create(CCDelayTime::create(DELETE_CONFIRM_SECONDS), CallFuncExt::create([this] {
                m_confirming = false;
                this->setMessage(str(Msg::ImportWarning), {255, 200, 80});
            }), nullptr);
            expire->setTag(1);
            this->runAction(expire);
            return;
        }
        this->finish(true);
    }

    void finish(bool replace) {
        if (!this->anyPicked()) return core::notify(NotificationIcon::Warning, str(Msg::NothingSelected));
        auto result = transfer::take(m_pack, m_picked, replace);
        if (result.failed > 0) core::notify(NotificationIcon::Warning, text::format(Msg::ImportFailed, result.failed));
        else if (result.notMoved > 0) {
            core::notify(NotificationIcon::Warning, text::format(Msg::NotSetAside, result.added, result.notMoved));
        }
        else core::notify(NotificationIcon::Success, text::format(Msg::Imported, result.added));
        if (m_done) m_done();
        this->onClose(nullptr);
    }

public:
    static ImportPopup* create(transfer::Pack pack, geode::Function<void()> done) {
        auto ret = new ImportPopup();
        if (ret->init(std::move(pack), std::move(done))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class RenamePopup : public Popup {
protected:
    core::SP m_sp;
    TextInput* m_input = nullptr;
    geode::Function<void()> m_done;

    bool init(core::SP sp, geode::Function<void()> done) {
        if (!Popup::init(260.f, 130.f)) return false;
        m_sp = std::move(sp);
        m_done = std::move(done);
        setGoldTitle(this, str(Msg::RenameTitle));

        m_input = TextInput::create(200.f, str(Msg::NamePlaceholder), text::font());
        // The game's own filters are ASCII only; the mod's font has Cyrillic letters too.
        m_input->setFilter(std::string(getCommonFilterAllowedChars(CommonFilter::Any)) +
            "абвгдеёжзийклмнопрстуфхцчшщъыьэюяАБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ");
        m_input->setMaxCharCount(MAX_NAME_CHARS);
        m_input->setString(m_sp->name);
        m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0.f, 3.f});

        auto save = textButton(str(Msg::Save), [this](auto) { this->onSave(); });
        m_buttonMenu->addChildAtPosition(save, Anchor::Bottom, {0.f, 24.f});
        return true;
    }

    void onSave() {
        auto name = std::string(m_input->getString());
        core::rename(m_sp, utils::string::trim(name));
        if (m_done) m_done();
        this->onClose(nullptr);
    }

public:
    CCLabelBMFont* titleLabel() { return m_title; }
    using Popup::setTitle;

    static RenamePopup* create(core::SP sp, geode::Function<void()> done) {
        auto ret = new RenamePopup();
        if (ret->init(std::move(sp), std::move(done))) {
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
        exportBtn->setID("export-button"_spr);
        importBtn->setID("import-button"_spr);
        m_buttonMenu->addChildAtPosition(exportBtn, Anchor::Bottom, {-70.f, 28.f});
        m_buttonMenu->addChildAtPosition(importBtn, Anchor::Bottom, {70.f, 28.f});
        this->rebuild();
        return true;
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
        auto const& items = pack.unwrap().items;
        if (std::all_of(items.begin(), items.end(), [](auto const& item) { return item.duplicate; })) {
            return core::notify(NotificationIcon::Info, str(Msg::AllAlreadyHere));
        }
        Ref<SPListPopup> self = this;
        if (auto popup = ImportPopup::create(std::move(pack).unwrap(), [self] { self->rebuild(); })) popup->show();
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
            auto rename = CCMenuItemExt::createSpriteExtraWithFrameName("GJ_editBtn_001.png", 0.32f, [this, sp](auto) {
                this->onRename(sp);
            });
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
        m_confirming = nullptr;
        if (auto popup = RenamePopup::create(sp, [this] { this->rebuild(); })) popup->show();
    }

    void onDelete(core::SP const& sp) {
        if (m_confirming != sp) {
            m_confirming = sp;
            this->rebuild();
            // The confirmation runs out.
            this->stopActionByTag(1);
            auto expire = CCSequence::create(CCDelayTime::create(DELETE_CONFIRM_SECONDS), CallFuncExt::create([this] {
                m_confirming = nullptr;
                this->rebuild();
            }), nullptr);
            expire->setTag(1);
            this->runAction(expire);
            return;
        }
        this->stopActionByTag(1);
        m_confirming = nullptr;
        core::remove(sp);
        this->rebuild();
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
    if (auto popup = RenamePopup::create(sp, {})) popup->show();
}

void openExport() {
    if (auto popup = ExportPopup::create()) popup->show();
}

void openImport(transfer::Pack pack) {
    if (auto popup = ImportPopup::create(std::move(pack), {})) popup->show();
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
