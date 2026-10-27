#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "Core.hpp"
#include "SPList.hpp"
#include "Text.hpp"

using namespace geode::prelude;
using text::Msg;

// The SP list in the pause menu: play from an SP (or the level start), rename or delete one.

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
    static constexpr float WIDTH = 380.f, HEIGHT = 260.f;
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
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, {0.f, -12.f});

        m_list = ScrollLayer::create({LIST_WIDTH, LIST_HEIGHT});
        m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
        m_mainLayer->addChildAtPosition(m_list, Anchor::Center, {-LIST_WIDTH / 2, -12.f - LIST_HEIGHT / 2});
        this->rebuild();
        return true;
    }

    CCNode* row(core::SP const& sp, int number) {
        auto node = CCNode::create();
        node->setContentSize({LIST_WIDTH, ROW_HEIGHT});
        bool active = core::active() == sp;
        bool confirming = sp && m_confirming == sp;

        std::string caption;
        if (confirming) caption = str(Msg::ConfirmDelete);
        else if (!sp) caption = str(Msg::ListLevelStart);
        else if (sp->name.empty()) caption = fmt::format("SP {} ({:.1f}%)", number, sp->percent);
        else caption = fmt::format("{}. {} ({:.1f}%)", number, sp->name, sp->percent);
        auto label = text::label(caption);
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
