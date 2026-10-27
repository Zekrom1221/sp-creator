#include "Text.hpp"

#include <array>

using namespace geode::prelude;

namespace text {

namespace {

struct Entry {
    char const* en;
    char const* ru;
};

constexpr std::array<Entry, static_cast<size_t>(Msg::Count)> TEXTS = {{
    {"Wait until the auto part ends", "Дождись конца авто-части"},
    {"Too early: play {:.1f} s after the spawn first", "Рано: сначала сыграй {:.1f} с после спавна"},
    {"Placed {}", "Поставлен {}"},
    {"No SPs in this level yet", "В этом уровне пока нет SP"},
    {"No SP selected: switch to the one to delete", "SP не выбран: переключись на тот, который удалить"},
    {"Deleted the SP at {:.1f}%", "SP на {:.1f}% удалён"},
    {"Level start", "Старт уровня"},
    {"SP {}/{} ({:.1f}%)", "SP {}/{} ({:.1f}%)"},
    {"{} (SP {}/{}, {:.1f}%)", "{} (SP {}/{}, {:.1f}%)"},
    {"SPs loaded: {}", "Загружено SP: {}"},
    {"SPs that couldn't be loaded: {}", "Не загрузилось SP: {}"},
    {"This level changed since you placed your SPs: they weren't loaded",
        "Уровень изменился после того, как ты ставил SP: они не загружены"},
    {"SPs can't be saved in this level", "В этом уровне SP не сохраняются"},
    {"SPs are off in this level: the old ones can't be moved aside",
        "SP в этом уровне выключены: не получилось убрать старые"},
    {"Your SPs", "Твои SP"},
    {"No SPs yet. Press {} while playing to place one.", "SP пока нет. Нажми {} во время игры, чтобы поставить."},
    {"Level start", "Старт уровня"},
    {"Click again to delete", "Нажми ещё раз, чтобы удалить"},
    {"Rename SP", "Переименовать SP"},
    {"Name", "Название"},
    {"Save", "Сохранить"},
}};

bool windowsIsRussian() {
    static bool russian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
    return russian;
}

bool hasNonAscii(std::string const& str) {
    for (unsigned char c : str) {
        if (c >= 0x80) return true;
    }
    return false;
}

}

bool russian() {
    auto lang = Mod::get()->getSettingValue<std::string>("language");
    if (lang == "Russian") return true;
    if (lang == "English") return false;
    return windowsIsRussian();
}

std::string_view tr(Msg msg) {
    auto const& e = TEXTS[static_cast<size_t>(msg)];
    return russian() ? e.ru : e.en;
}

char const* font() {
    return russian() ? "spcFont.fnt"_spr : "bigFont.fnt";
}

char const* titleFont() {
    return russian() ? "spcFont.fnt"_spr : "goldFont.fnt";
}

char const* fontFor(std::string const& str, bool title) {
    // A name typed in another language than the mod's needs the mod's font too.
    if (russian() || hasNonAscii(str)) return "spcFont.fnt"_spr;
    return title ? "goldFont.fnt" : "bigFont.fnt";
}

CCLabelBMFont* label(std::string const& str, bool title) {
    auto font = fontFor(str, title);
    auto label = CCLabelBMFont::create(str.c_str(), font);
    if (title && std::string_view(font) != "goldFont.fnt") label->setColor({255, 210, 60});
    return label;
}

}
