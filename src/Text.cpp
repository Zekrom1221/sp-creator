#include "Text.hpp"

#include <algorithm>
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
    {"Your SPs of this level are for Low Detail Mode: turn it on to use them",
        "Твои SP этого уровня — для Low Detail Mode: включи его, чтобы ими пользоваться"},
    {"Your SPs of this level are for Low Detail Mode off: turn it off to use them",
        "Твои SP этого уровня — без Low Detail Mode: выключи его, чтобы ими пользоваться"},
    {"Export", "Экспорт"},
    {"Import", "Импорт"},
    {"Export SPs", "Экспорт SP"},
    {"Import SPs", "Импорт SP"},
    {"Import files only from people you trust", "Импортируй файлы только от тех, кому доверяешь"},
    {"Replace again: your {} SPs here go aside, not deleted", "Ещё раз «Заменить»: твои SP ({}) уйдут в сторону, не удалятся"},
    {"Add", "Добавить"},
    {"Replace", "Заменить"},
    {"already here", "уже есть"},
    {"near your SP {} ({:.1f}%)", "рядом с твоим SP {} ({:.1f}%)"},
    {"Nothing selected", "Ничего не выбрано"},
    {"SPs exported: {}", "Экспортировано SP: {}"},
    {"Couldn't write the file", "Не получилось записать файл"},
    {"SPs imported: {}", "Импортировано SP: {}"},
    {"SPs that couldn't be imported: {}", "Не импортировалось SP: {}"},
    {"SPs imported: {}, but {} of yours couldn't be set aside", "Импортировано SP: {}, но {} твоих не удалось убрать в сторону"},
    {"All SPs from the file are already here", "Все SP из файла уже есть"},
    {"Not an SP Creator file", "Это не файл SP Creator"},
    {"Made by a newer SP Creator: update the mod", "Файл из новой версии SP Creator: обнови мод"},
    {"The file is damaged", "Файл повреждён"},
    {"The file is too big", "Файл слишком большой"},
    {"These SPs are for another level: {}", "Эти SP для другого уровня: {}"},
    {"These SPs are for another version of this level", "Эти SP для другой версии этого уровня"},
    {"These SPs were made in GD {}", "Эти SP сделаны в GD {}"},
    {"These SPs are for Low Detail Mode: turn it on", "Эти SP для Low Detail Mode: включи его"},
    {"These SPs are for Low Detail Mode off: turn it off", "Эти SP без Low Detail Mode: выключи его"},
    {"SP Creator SPs", "SP из SP Creator"},
}};
// Entries left out would be null.
static_assert(std::ranges::all_of(TEXTS, [](Entry const& e) { return e.en && e.ru; }), "a text is missing");

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
