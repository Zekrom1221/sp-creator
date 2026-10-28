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
    {"Your SPs here are for Low Detail Mode: turn it on", "Твои SP здесь — для Low Detail Mode: включи его"},
    {"Your SPs here are for Low Detail Mode off: turn it off", "Твои SP здесь — без Low Detail Mode: выключи его"},
    {"Export", "Экспорт"},
    {"Import", "Импорт"},
    {"Export SPs", "Экспорт SP"},
    {"Import SPs", "Импорт SP"},
    {"Import files only from people you trust", "Импортируй файлы только от тех, кому доверяешь"},
    {"Add", "Добавить"},
    {"Replace all", "Заменить все"},
    {"already here", "уже есть"},
    {"twice in the file", "повтор в файле"},
    {"near your SP {} ({:.1f}%)", "рядом с твоим SP {} ({:.1f}%)"},
    {"Nothing selected", "Ничего не выбрано"},
    {"The picked SPs are already here", "Выбранные SP уже есть"},
    {"SPs exported: {}", "Экспортировано SP: {}"},
    {"Couldn't write the file", "Не получилось записать файл"},
    {"SPs imported: {}", "Импортировано SP: {}"},
    {"SPs imported: {}. Yours are in Versions", "Импортировано SP: {}. Твои — в «Версиях»"},
    {"SPs imported: {}. Yours were in Versions already: moved up", "Импортировано SP: {}. Твои уже были в «Версиях»: подняты наверх"},
    {"SPs that couldn't be imported: {}", "Не импортировалось SP: {}"},
    {"SPs that couldn't be imported: {}. Yours are in Versions", "Не импортировалось SP: {}. Твои — в «Версиях»"},
    {"SPs imported: {}, but {} of yours couldn't go to Versions", "Импортировано SP: {}, но {} твоих не ушли в «Версии»"},
    {"SPs imported: {}, but {} of yours couldn't be deleted", "Импортировано SP: {}, но {} твоих не удалились"},
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
    {"Your SPs here ({}) will be replaced. Save them?", "Твои SP здесь ({}) будут заменены. Сохранить их?"},
    {"Saved ones can be brought back: SP list, Versions", "Сохранённые можно вернуть: список SP, «Версии»"},
    {"Save and replace", "Сохранить и заменить"},
    {"Delete and replace", "Удалить и заменить"},
    {"Versions", "Версии"},
    {"SP versions", "Версии SP"},
    {"Versions: {}, {} in all", "Версий: {}, всего {}"},
    {"No versions yet. Replace all can save your SPs as one.", "Версий пока нет. «Заменить все» может сохранить твои SP версией."},
    {"before Replace all", "до «Заменить все»"},
    {"before a level update", "до обновления уровня"},
    {"SPs: {}, {}", "SP: {}, {}"},
    {"for another level version: delete only", "для другой версии уровня: только удалить"},
    {"Open", "Открыть"},
    {"SPs of {}", "SP от {}"},
    {"The level has these SPs already", "Эти SP уже стоят в уровне"},
    {"Version deleted", "Версия удалена"},
    {"Couldn't delete the version", "Не получилось удалить версию"},
    {"This version can't be read", "Эту версию не получилось прочитать"},
    {"Rename version", "Переименовать версию"},
    {"Couldn't rename the version", "Не получилось переименовать версию"},
    {"SP storage", "Хранилище SP"},
    {"Levels: {}, {} in all", "Уровней: {}, всего {}"},
    {"No SPs saved yet", "Сохранённых SP пока нет"},
    {"{}: SPs {}, versions {}", "{}: SP {}, версий {}"},
    {"open now: its SPs stay", "открыт сейчас: его SP остаются"},
    {"{} in all. Versions come back in the level: SP list, Versions", "Всего {}. Вернуть версию — в уровне: список SP, «Версии»"},
    {"Current SPs: {}, {}", "Текущие SP: {}, {}"},
    {"for an older level version", "для старой версии уровня"},
    {"Select all", "Выбрать все"},
    {"Delete versions", "Удалить версии"},
    {"Delete entirely", "Удалить целиком"},
    {"Delete picked", "Удалить выбранное"},
    {"Click again to delete: {}", "Нажми ещё раз, чтобы удалить: {}"},
    {"Deleted: {}", "Удалено: {}"},
    {"Deleted: {}, but {} couldn't be", "Удалено: {}, но {} не удалилось"},
    {"Nothing to delete there", "Там нечего удалять"},
    {"{} KB", "{} КБ"},
    {"{:.1f} MB", "{:.1f} МБ"},
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

std::string size(uintmax_t bytes) {
    constexpr uintmax_t MB = 1024 * 1024;
    if (bytes < MB) return format(Msg::Kilobytes, (bytes + 1023) / 1024);
    return format(Msg::Megabytes, static_cast<double>(bytes) / MB);
}

std::string date(std::time_t time) {
    std::tm tm{};
    if (time <= 0 || localtime_s(&tm, &time) != 0) return "?";
    char out[32];
    std::strftime(out, sizeof(out), russian() ? "%d.%m.%Y %H:%M" : "%Y-%m-%d %H:%M", &tm);
    return out;
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
