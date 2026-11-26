#pragma once

#include <Geode/Geode.hpp>

#include "Storage.hpp"
#include "Text.hpp"

// What the mod's popups share (SPList.cpp, StorageList.cpp).
namespace ui {
    using namespace geode::prelude;

    using text::fitName;
    using text::MAX_NAME_CHARS;
    constexpr float DELETE_CONFIRM_SECONDS = 2.5f;
    constexpr ccColor3B WARNING_COLOR = {255, 200, 80}, NOTE_COLOR = {190, 190, 190}, NEAR_COLOR = {255, 170, 60},
        CONFIRM_COLOR = {255, 90, 90};

    inline std::string str(text::Msg msg) {
        return std::string(text::tr(msg));
    }

    // What made a version: "before Replace all", "before deleting", "before a level update".
    inline std::string versionReason(storage::AsideFor why) {
        switch (why) {
            case storage::AsideFor::Replace: return str(text::Msg::VersionReplaced);
            case storage::AsideFor::Delete: return str(text::Msg::VersionBeforeDelete);
            case storage::AsideFor::OtherLevelVersion: break;
        }
        return str(text::Msg::VersionOld);
    }

    // `size` scales all of it.
    inline ButtonSprite* textButtonSprite(std::string const& caption, float size = 1.f, char const* texture = "GJ_button_01.png") {
        auto spr = ButtonSprite::create(caption.c_str(), 0, 0, 0.7f, false, text::fontFor(caption, true), texture, 30.f);
        spr->setScale(size);
        return spr;
    }

    inline CCMenuItemSpriteExtra* textButton(std::string const& caption, geode::Function<void(CCMenuItemSpriteExtra*)> callback,
        float size = 1.f, char const* texture = "GJ_button_01.png") {
        return CCMenuItemExt::createSpriteExtra(textButtonSprite(caption, size, texture), std::move(callback));
    }

    // The game has no pencil (its edit button is the editor's): the mod's, on a round button as big as the
    // play one at 0.32. Lying diagonally it fills its box to the corners: at 0.75 of the size the button gives
    // it, its tip stays off the button's rim.
    inline CCMenuItemSpriteExtra* renameButton(geode::Function<void(CCMenuItemSpriteExtra*)> callback) {
        auto spr = CircleButtonSprite::createWithSprite("pencil.png"_spr, 0.75f, CircleBaseColor::Green, CircleBaseSize::Medium);
        auto play = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
        if (!spr || !play) return CCMenuItemExt::createSpriteExtraWithFrameName("GJ_editBtn_001.png", 0.32f, std::move(callback));
        spr->setScale(play->getContentHeight() * 0.32f / spr->getContentHeight());
        return CCMenuItemExt::createSpriteExtra(spr, std::move(callback));
    }

    // The game gives menus made while popups are open the touch priority of the top one. A list rebuilt under
    // another popup would get buttons that take that popup's clicks, so lists below it wait (see refresh()).
    inline bool onTop(CCLayer* popup) {
        auto handler = CCTouchDispatcher::get()->findHandler(popup);
        return handler && handler->getPriority() == CCTouchDispatcher::get()->getTargetPrio();
    }

    // A click on a delete button asks for another one, for a while: `expired` then.
    inline void confirmFor(CCNode* popup, geode::Function<void()> expired) {
        popup->stopActionByTag(1);
        auto expire = CCSequence::create(CCDelayTime::create(DELETE_CONFIRM_SECONDS), CallFuncExt::create(std::move(expired)), nullptr);
        expire->setTag(1);
        popup->runAction(expire);
    }

    // A checkbox that can't be ticked looks it.
    inline void disableToggler(CCMenuItemToggler* toggler) {
        toggler->setEnabled(false);
        for (auto button : {toggler->m_onButton, toggler->m_offButton}) {
            if (auto image = typeinfo_cast<CCSprite*>(button->getNormalImage())) image->setOpacity(110);
        }
    }

    // goldFont has no Cyrillic letters: the mod's font, coloured like it.
    template <class P>
    void setGoldTitle(P* popup, std::string const& title) {
        popup->setTitle(title, text::fontFor(title, true));
        if (std::string_view(text::fontFor(title, true)) != "goldFont.fnt") popup->titleLabel()->setColor({255, 210, 60});
    }
}
