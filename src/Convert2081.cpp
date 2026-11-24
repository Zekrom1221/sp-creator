// Generated (from PersistenceAPI v1.2.0's load code: the order it wrote a checkpoint's fields in; and a table of those
// fields from GD 2.2081 to 2.2091, made of both versions' bindings and checked on SP files of 2.2081). Edits go to
// the generator, not here.
//
// SP files of GD 2.2081 (format 2) hold their checkpoint as PersistenceAPI (sabe.persistenceapi by Sabe, GPLv3)
// wrote it: each field of each class as it was in memory, one after the other, containers as a u32 count and their
// items, level objects as their place among the level's objects (-1 none, -2 and -3 the players). Here every
// field goes into its field of this game's checkpoint: the same field under its 2.2091 name (936, and 40 by their
// place between such), 14 whose type changed in the bindings only (their bytes) or for real (2 floats now doubles),
// 7 gone in 2.2091 read past. New fields of 2.2091 stay as the game makes them: 0, as in its own checkpoints of a
// level (checked on 98).
#include "Convert2081.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace convert2081 {

namespace {

struct In {
    std::span<uint8_t const> bytes;
    size_t at = 0;
    Objects const& objects;

    size_t left() const {
        return bytes.size() - at;
    }
    void raw(void* to, size_t size) {
        if (size > left()) throw std::out_of_range("past the end of the checkpoint");
        std::memcpy(to, bytes.data() + at, size);
        at += size;
    }
    void skip(size_t size) {
        if (size > left()) throw std::out_of_range("past the end of the checkpoint");
        at += size;
    }
    template <class T>
    T get() {
        T value;
        raw(&value, sizeof(value));
        return value;
    }
    // A container's count, each item at least `itemSize` bytes.
    uint32_t count(size_t itemSize) {
        auto n = get<uint32_t>();
        if (static_cast<uint64_t>(n) * itemSize > left()) {
            throw std::out_of_range(fmt::format("{} items of {} bytes, {} bytes left", n, itemSize, left()));
        }
        return n;
    }
    template <class T>
    T* object() {
        int index = get<int32_t>();
        auto obj = objects.at(index);
        if (!obj) return nullptr;
        auto typed = typeinfo_cast<T*>(obj);
        if (!typed) throw std::runtime_error(fmt::format("object {} (ID {}) isn't of the kind referred to", index, obj->m_objectID));
        return typed;
    }
};

template <class T> struct IsVector : std::false_type {};
template <class T, class A> struct IsVector<std::vector<T, A>> : std::true_type {};
template <class T> struct IsMap : std::false_type {};
template <class K, class V, class C, class A> struct IsMap<std::map<K, V, C, A>> : std::true_type {};
template <class K, class V, class H, class E, class A> struct IsMap<std::unordered_map<K, V, H, E, A>> : std::true_type {};
template <class T> struct IsSet : std::false_type {};
template <class K, class C, class A> struct IsSet<std::set<K, C, A>> : std::true_type {};
template <class K, class H, class E, class A> struct IsSet<std::unordered_set<K, H, E, A>> : std::true_type {};

template <class T> struct IsPair : std::false_type {};
template <class A, class B> struct IsPair<std::pair<A, B>> : std::true_type {};

// A value PersistenceAPI copied as it was in memory: as many bytes as it has. Points and pairs aren't trivially
// copyable here (constructors of their own), so they're read by their parts.
template <class T>
constexpr bool isPlainPair() {
    if constexpr (IsPair<T>::value) {
        using A = typename T::first_type;
        using B = typename T::second_type;
        return (std::is_trivially_copyable_v<A> || std::is_enum_v<A>) && (std::is_trivially_copyable_v<B> || std::is_enum_v<B>);
    }
    else return false;
}
template <class T>
constexpr bool isPlain = (std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>) || std::is_same_v<T, CCPoint> ||
    std::is_same_v<T, CCSize> || isPlainPair<T>();

// Classes PersistenceAPI wrote field by field (a load() of their own here): at least a byte each in a container, as
// the Python reader counts them; the rest as many bytes as they have.
template <class T>
constexpr bool hasLoad = false;

template <class T>
size_t plainSize() {
    if constexpr (isPlain<T> && !hasLoad<T>) return sizeof(T);
    else return 1;
}

template <class T>
T bitsAs(auto const& value) {
    static_assert(sizeof(T) == sizeof(value));
    return std::bit_cast<T>(value);
}

void skipCCObject(In& in);
void skipCCNode(In& in);

template <>
constexpr bool hasLoad<AdvancedFollowInstance> = true;
void load(In& in, AdvancedFollowInstance& o);
#ifdef SPC_SMOKE
matjson::Value dump(AdvancedFollowInstance const& o);
#endif
template <>
constexpr bool hasLoad<CAState> = true;
void load(In& in, CAState& o);
#ifdef SPC_SMOKE
matjson::Value dump(CAState const& o);
#endif
template <>
constexpr bool hasLoad<CollisionTriggerAction> = true;
void load(In& in, CollisionTriggerAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(CollisionTriggerAction const& o);
#endif
template <>
constexpr bool hasLoad<CountTriggerAction> = true;
void load(In& in, CountTriggerAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(CountTriggerAction const& o);
#endif
template <>
constexpr bool hasLoad<DynamicObjectAction> = true;
void load(In& in, DynamicObjectAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(DynamicObjectAction const& o);
#endif
template <>
constexpr bool hasLoad<EffectManagerState> = true;
void load(In& in, EffectManagerState& o);
#ifdef SPC_SMOKE
matjson::Value dump(EffectManagerState const& o);
#endif
template <>
constexpr bool hasLoad<EnterEffectAnimValue> = true;
void load(In& in, EnterEffectAnimValue& o);
#ifdef SPC_SMOKE
matjson::Value dump(EnterEffectAnimValue const& o);
#endif
template <>
constexpr bool hasLoad<EnterEffectInstance> = true;
void load(In& in, EnterEffectInstance& o);
#ifdef SPC_SMOKE
matjson::Value dump(EnterEffectInstance const& o);
#endif
template <>
constexpr bool hasLoad<EventTriggerInstance> = true;
void load(In& in, EventTriggerInstance& o);
#ifdef SPC_SMOKE
matjson::Value dump(EventTriggerInstance const& o);
#endif
template <>
constexpr bool hasLoad<FMODAudioState> = true;
void load(In& in, FMODAudioState& o);
#ifdef SPC_SMOKE
matjson::Value dump(FMODAudioState const& o);
#endif
template <>
constexpr bool hasLoad<FMODQueuedMusic> = true;
void load(In& in, FMODQueuedMusic& o);
#ifdef SPC_SMOKE
matjson::Value dump(FMODQueuedMusic const& o);
#endif
template <>
constexpr bool hasLoad<FMODSoundState> = true;
void load(In& in, FMODSoundState& o);
#ifdef SPC_SMOKE
matjson::Value dump(FMODSoundState const& o);
#endif
template <>
constexpr bool hasLoad<FMODSoundTween> = true;
void load(In& in, FMODSoundTween& o);
#ifdef SPC_SMOKE
matjson::Value dump(FMODSoundTween const& o);
#endif
template <>
constexpr bool hasLoad<GJGameState> = true;
void load(In& in, GJGameState& o);
#ifdef SPC_SMOKE
matjson::Value dump(GJGameState const& o);
#endif
template <>
constexpr bool hasLoad<GJShaderState> = true;
void load(In& in, GJShaderState& o);
#ifdef SPC_SMOKE
matjson::Value dump(GJShaderState const& o);
#endif
template <>
constexpr bool hasLoad<GJValueTween> = true;
void load(In& in, GJValueTween& o);
#ifdef SPC_SMOKE
matjson::Value dump(GJValueTween const& o);
#endif
template <>
constexpr bool hasLoad<GameObjectPhysics> = true;
void load(In& in, GameObjectPhysics& o);
#ifdef SPC_SMOKE
matjson::Value dump(GameObjectPhysics const& o);
#endif
template <>
constexpr bool hasLoad<GroupCommandObject2> = true;
void load(In& in, GroupCommandObject2& o);
#ifdef SPC_SMOKE
matjson::Value dump(GroupCommandObject2 const& o);
#endif
template <>
constexpr bool hasLoad<KeyframeObject> = true;
void load(In& in, KeyframeObject& o);
#ifdef SPC_SMOKE
matjson::Value dump(KeyframeObject const& o);
#endif
template <>
constexpr bool hasLoad<OpacityEffectAction> = true;
void load(In& in, OpacityEffectAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(OpacityEffectAction const& o);
#endif
template <>
constexpr bool hasLoad<PlayerCheckpoint> = true;
void load(In& in, PlayerCheckpoint& o);
#ifdef SPC_SMOKE
matjson::Value dump(PlayerCheckpoint const& o);
#endif
template <>
constexpr bool hasLoad<PulseEffectAction> = true;
void load(In& in, PulseEffectAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(PulseEffectAction const& o);
#endif
template <>
constexpr bool hasLoad<SFXStateContainer> = true;
void load(In& in, SFXStateContainer& o);
#ifdef SPC_SMOKE
matjson::Value dump(SFXStateContainer const& o);
#endif
template <>
constexpr bool hasLoad<SFXTriggerInstance> = true;
void load(In& in, SFXTriggerInstance& o);
#ifdef SPC_SMOKE
matjson::Value dump(SFXTriggerInstance const& o);
#endif
template <>
constexpr bool hasLoad<SFXTriggerState> = true;
void load(In& in, SFXTriggerState& o);
#ifdef SPC_SMOKE
matjson::Value dump(SFXTriggerState const& o);
#endif
template <>
constexpr bool hasLoad<SavedActiveObjectState> = true;
void load(In& in, SavedActiveObjectState& o);
#ifdef SPC_SMOKE
matjson::Value dump(SavedActiveObjectState const& o);
#endif
template <>
constexpr bool hasLoad<SavedObjectStateRef> = true;
void load(In& in, SavedObjectStateRef& o);
#ifdef SPC_SMOKE
matjson::Value dump(SavedObjectStateRef const& o);
#endif
template <>
constexpr bool hasLoad<SavedSpecialObjectState> = true;
void load(In& in, SavedSpecialObjectState& o);
#ifdef SPC_SMOKE
matjson::Value dump(SavedSpecialObjectState const& o);
#endif
template <>
constexpr bool hasLoad<SequenceTriggerState> = true;
void load(In& in, SequenceTriggerState& o);
#ifdef SPC_SMOKE
matjson::Value dump(SequenceTriggerState const& o);
#endif
template <>
constexpr bool hasLoad<SongChannelState> = true;
void load(In& in, SongChannelState& o);
#ifdef SPC_SMOKE
matjson::Value dump(SongChannelState const& o);
#endif
template <>
constexpr bool hasLoad<SongTriggerState> = true;
void load(In& in, SongTriggerState& o);
#ifdef SPC_SMOKE
matjson::Value dump(SongTriggerState const& o);
#endif
template <>
constexpr bool hasLoad<SoundStateContainer> = true;
void load(In& in, SoundStateContainer& o);
#ifdef SPC_SMOKE
matjson::Value dump(SoundStateContainer const& o);
#endif
template <>
constexpr bool hasLoad<SpawnTriggerAction> = true;
void load(In& in, SpawnTriggerAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(SpawnTriggerAction const& o);
#endif
template <>
constexpr bool hasLoad<TimerItem> = true;
void load(In& in, TimerItem& o);
#ifdef SPC_SMOKE
matjson::Value dump(TimerItem const& o);
#endif
template <>
constexpr bool hasLoad<TimerTriggerAction> = true;
void load(In& in, TimerTriggerAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(TimerTriggerAction const& o);
#endif
template <>
constexpr bool hasLoad<ToggleTriggerAction> = true;
void load(In& in, ToggleTriggerAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(ToggleTriggerAction const& o);
#endif
template <>
constexpr bool hasLoad<TouchToggleAction> = true;
void load(In& in, TouchToggleAction& o);
#ifdef SPC_SMOKE
matjson::Value dump(TouchToggleAction const& o);
#endif
template <>
constexpr bool hasLoad<tk_spline> = true;
void load(In& in, tk_spline& o);
#ifdef SPC_SMOKE
matjson::Value dump(tk_spline const& o);
#endif

template <class T>
void load(In& in, T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        auto n = in.count(1);
        value.resize(n);
        in.raw(value.data(), n);
    }
    else if constexpr (IsVector<T>::value) {
        using Item = typename T::value_type;
        auto n = in.count(plainSize<Item>());
        value.clear();
        value.resize(n);
        for (auto& item : value) load(in, item);
    }
    else if constexpr (IsMap<T>::value) {
        using Key = typename T::key_type;
        static_assert(isPlain<Key>);
        auto n = in.count(sizeof(Key));
        value.clear();
        for (uint32_t i = 0; i < n; i++) {
            Key key{};
            load(in, key);
            // Made in place: some can't be assigned (GameObjectPhysics: unions of points).
            load(in, value[key]);
        }
    }
    else if constexpr (IsSet<T>::value) {
        using Key = typename T::key_type;
        static_assert(isPlain<Key>);
        auto n = in.count(sizeof(Key));
        value.clear();
        for (uint32_t i = 0; i < n; i++) value.insert(in.get<Key>());
    }
    else if constexpr (std::is_pointer_v<T>) {
        value = in.object<std::remove_pointer_t<T>>();
    }
    else if constexpr (std::is_same_v<T, CCPoint>) {
        value.x = in.get<float>();
        value.y = in.get<float>();
    }
    else if constexpr (std::is_same_v<T, CCSize>) {
        value.width = in.get<float>();
        value.height = in.get<float>();
    }
    else if constexpr (IsPair<T>::value) {
        // As it was in memory: the second part where its alignment puts it, the pair padded to its size.
        using A = typename T::first_type;
        using B = typename T::second_type;
        constexpr size_t secondAt = (sizeof(A) + alignof(B) - 1) / alignof(B) * alignof(B);
        static_assert(sizeof(T) >= secondAt + sizeof(B));
        load(in, value.first);
        in.skip(secondAt - sizeof(A));
        load(in, value.second);
        in.skip(sizeof(T) - secondAt - sizeof(B));
    }
    else {
        static_assert(std::is_trivially_copyable_v<T>, "a class without a load() of its own");
        in.raw(&value, sizeof(value));
    }
}

// PACCArray::load<GameObject> / <GradientTriggerObject>: the objects at their places, none left out but empty ones.
template <class T>
CCArray* loadArray(In& in, CCArray* had) {
    auto n = in.count(4);
    auto array = CCArray::create();
    for (uint32_t i = 0; i < n; i++) {
        if (auto obj = in.object<T>()) array->addObject(obj);
    }
    array->retain();
    if (had) had->release();
    return array;
}

#ifdef SPC_SMOKE
Objects const* g_dumping = nullptr;

matjson::Value number(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    return value;
}

template <class T>
matjson::Value dump(T const& value) {
    if constexpr (std::is_same_v<T, bool>) {
        uint8_t byte;
        std::memcpy(&byte, &value, 1);
        return byte > 1 ? matjson::Value(static_cast<int>(byte)) : matjson::Value(byte == 1);
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return number(static_cast<double>(value));
    }
    else if constexpr (std::is_enum_v<T>) {
        return static_cast<std::underlying_type_t<T>>(value);
    }
    else if constexpr (std::is_integral_v<T>) {
        return value;
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        return value;
    }
    else if constexpr (std::is_same_v<T, CCPoint> || std::is_same_v<T, CCSize>) {
        float a, b;
        std::memcpy(&a, &value, 4);
        std::memcpy(&b, reinterpret_cast<char const*>(&value) + 4, 4);
        return matjson::Value(std::vector<matjson::Value>{number(a), number(b)});
    }
    else if constexpr (std::is_same_v<T, CCAffineTransform>) {
        float f[6];
        std::memcpy(f, &value, sizeof(f));
        std::vector<matjson::Value> out;
        for (float x : f) out.push_back(number(x));
        return out;
    }
    else if constexpr (std::is_same_v<T, ccColor3B>) {
        return matjson::Value(std::vector<matjson::Value>{value.r, value.g, value.b});
    }
    else if constexpr (std::is_same_v<T, ccHSVValue>) {
        auto d = matjson::Value::object();
        d.set("h", number(value.h));
        d.set("s", number(value.s));
        d.set("v", number(value.v));
        d.set("absS", dump(value.absoluteSaturation));
        d.set("absV", dump(value.absoluteBrightness));
        static_assert(sizeof(ccHSVValue) == 16);
        uint8_t pad[2];
        std::memcpy(pad, reinterpret_cast<char const*>(&value) + 14, 2);
        d.set("pad", fmt::format("{:02x}{:02x}", pad[0], pad[1]));
        return d;
    }
    else if constexpr (IsPair<T>::value) {
        return matjson::Value(std::vector<matjson::Value>{dump(value.first), dump(value.second)});
    }
    else if constexpr (IsVector<T>::value) {
        std::vector<matjson::Value> out;
        for (auto const& item : value) out.push_back(dump(item));
        return out;
    }
    else if constexpr (IsMap<T>::value) {
        std::vector<std::pair<typename T::key_type, typename T::mapped_type const*>> items;
        for (auto const& [k, v] : value) items.push_back({k, &v});
        std::sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::vector<matjson::Value> out;
        for (auto const& [k, v] : items) out.push_back(matjson::Value(std::vector<matjson::Value>{dump(k), dump(*v)}));
        return out;
    }
    else if constexpr (IsSet<T>::value) {
        std::vector<typename T::key_type> keys(value.begin(), value.end());
        std::sort(keys.begin(), keys.end());
        std::vector<matjson::Value> out;
        for (auto const& k : keys) out.push_back(dump(k));
        return out;
    }
    else if constexpr (std::is_pointer_v<T>) {
        auto d = matjson::Value::object();
        d.set("object", g_dumping->placeOf(value));
        return d;
    }
    else {
        static_assert(sizeof(T) == 0, "no dump for this type");
    }
}

matjson::Value dumpArray(CCArray* array) {
    std::vector<matjson::Value> out;
    if (array) {
        for (auto obj : CCArrayExt<GameObject*>(array)) out.push_back(g_dumping->placeOf(obj));
    }
    return out;
}
#endif

void skipCCObject(In& in) {
    in.skip(4);  // m_eObjType
    in.skip(4);  // m_nZOrder
    in.skip(4);  // m_uOrderOfArrival
}

void skipCCNode(In& in) {
    skipCCObject(in);
    in.skip(4);  // m_fRotationX
    in.skip(4);  // m_fRotationY
    in.skip(4);  // m_fScaleX
    in.skip(4);  // m_fScaleY
    in.skip(4);  // m_fVertexZ
    in.skip(8);  // m_obPosition
    in.skip(4);  // m_fSkewX
    in.skip(4);  // m_fSkewY
    in.skip(8);  // m_obAnchorPointInPoints
    in.skip(8);  // m_obAnchorPoint
    in.skip(8);  // m_obContentSize
    in.skip(24);  // m_sAdditionalTransform
    in.skip(24);  // m_sTransform
    in.skip(24);  // m_sInverse
    in.skip(1);  // m_bRunning
    in.skip(1);  // m_bTransformDirty
    in.skip(1);  // m_bInverseDirty
    in.skip(1);  // m_bAdditionalTransformDirty
    in.skip(1);  // m_bVisible
    in.skip(1);  // m_bIgnoreAnchorPointForPosition
    in.skip(1);  // m_bReorderChildDirty
    in.skip(1);  // m_bUseChildIndex
    in.skip(1);  // m_bUnkBool2
}

void load(In& in, AdvancedFollowInstance& o) {
    load(in, o.m_gameObject);
    load(in, o.m_group);
    load(in, o.m_objectKey);
    load(in, o.m_controlId);
    load(in, o.m_otherObjectKey);
    load(in, o.m_relatedToGJGameStateUnkUint7);
    load(in, o.m_finished);
    load(in, o.m_doStart);
    load(in, o.m_started);
    load(in, o.m_processed);
}

#ifdef SPC_SMOKE
matjson::Value dump(AdvancedFollowInstance const& o) {
    auto d = matjson::Value::object();
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_group", dump(o.m_group));
    d.set("m_objectKey", dump(o.m_objectKey));
    d.set("m_controlId", dump(o.m_controlId));
    d.set("m_otherObjectKey", dump(o.m_otherObjectKey));
    d.set("m_relatedToGJGameStateUnkUint7", dump(o.m_relatedToGJGameStateUnkUint7));
    d.set("m_finished", dump(o.m_finished));
    d.set("m_doStart", dump(o.m_doStart));
    d.set("m_started", dump(o.m_started));
    d.set("m_processed", dump(o.m_processed));
    return d;
}
#endif

void load(In& in, CAState& o) {
    load(in, o.m_fromColor);
    load(in, o.m_toColor);
    load(in, o.m_color);
    load(in, o.m_paused);
    load(in, o.m_blending);
    load(in, o.m_copyOpacity);
    load(in, o.m_legacyHSV);
    load(in, o.m_playerColor);
    load(in, o.m_colorID);
    load(in, o.m_copyID);
    load(in, o.m_uniqueID);
    load(in, o.m_duration);
    load(in, o.m_fromOpacity);
    load(in, o.m_toOpacity);
    load(in, o.m_deltaTime);
    load(in, o.m_currentOpacity);
    load(in, o.m_copyHSV);
}

#ifdef SPC_SMOKE
matjson::Value dump(CAState const& o) {
    auto d = matjson::Value::object();
    d.set("m_fromColor", dump(o.m_fromColor));
    d.set("m_toColor", dump(o.m_toColor));
    d.set("m_color", dump(o.m_color));
    d.set("m_paused", dump(o.m_paused));
    d.set("m_blending", dump(o.m_blending));
    d.set("m_copyOpacity", dump(o.m_copyOpacity));
    d.set("m_legacyHSV", dump(o.m_legacyHSV));
    d.set("m_playerColor", dump(o.m_playerColor));
    d.set("m_colorID", dump(o.m_colorID));
    d.set("m_copyID", dump(o.m_copyID));
    d.set("m_uniqueID", dump(o.m_uniqueID));
    d.set("m_duration", dump(o.m_duration));
    d.set("m_fromOpacity", dump(o.m_fromOpacity));
    d.set("m_toOpacity", dump(o.m_toOpacity));
    d.set("m_deltaTime", dump(o.m_deltaTime));
    d.set("m_currentOpacity", dump(o.m_currentOpacity));
    d.set("m_copyHSV", dump(o.m_copyHSV));
    return d;
}
#endif

void load(In& in, CollisionTriggerAction& o) {
    load(in, o.m_disabled);
    load(in, o.m_blockAID);
    load(in, o.m_blockBID);
    load(in, o.m_targetGroupID);
    load(in, o.m_triggerOnExit);
    load(in, o.m_activateGroup);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(CollisionTriggerAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_blockAID", dump(o.m_blockAID));
    d.set("m_blockBID", dump(o.m_blockBID));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_triggerOnExit", dump(o.m_triggerOnExit));
    d.set("m_activateGroup", dump(o.m_activateGroup));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, CountTriggerAction& o) {
    load(in, o.m_disabled);
    load(in, o.m_previousCount);
    load(in, o.m_targetCount);
    load(in, o.m_targetGroupID);
    load(in, o.m_activateGroup);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_itemID);
    load(in, o.m_multiActivate);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(CountTriggerAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_previousCount", dump(o.m_previousCount));
    d.set("m_targetCount", dump(o.m_targetCount));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_activateGroup", dump(o.m_activateGroup));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_itemID", dump(o.m_itemID));
    d.set("m_multiActivate", dump(o.m_multiActivate));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, DynamicObjectAction& o) {
    load(in, o.m_trigger);
    load(in, o.m_centerObject);
    load(in, o.m_aimTargetObject);
    load(in, o.m_targetObject);
    load(in, o.m_minXObject);
    load(in, o.m_minYObject);
    load(in, o.m_maxXObject);
    load(in, o.m_maxYObject);
    load(in, o.m_elapsed);
    load(in, o.m_easedElapsed);
    load(in, o.m_easedProgress);
    load(in, o.m_justStarted);
    load(in, o.m_stopped);
    load(in, o.m_paused);
    load(in, o.m_pausePending);
    load(in, o.m_controlID);
    load(in, o.m_targetGroupID);
    load(in, o.m_centerGroupID);
}

#ifdef SPC_SMOKE
matjson::Value dump(DynamicObjectAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_gameObject1", dump(o.m_trigger));
    d.set("m_gameObject2", dump(o.m_centerObject));
    d.set("m_gameObject3", dump(o.m_aimTargetObject));
    d.set("m_gameObject4", dump(o.m_targetObject));
    d.set("m_gameObject5", dump(o.m_minXObject));
    d.set("m_gameObject6", dump(o.m_minYObject));
    d.set("m_gameObject7", dump(o.m_maxXObject));
    d.set("m_gameObject8", dump(o.m_maxYObject));
    d.set("m_unkFloat1", dump(o.m_elapsed));
    d.set("m_unkFloat2", dump(o.m_easedElapsed));
    d.set("m_unkFloat3", dump(o.m_easedProgress));
    d.set("m_unkBool1", dump(o.m_justStarted));
    d.set("m_unkBool2", dump(o.m_stopped));
    d.set("m_unkBool3", dump(o.m_paused));
    d.set("m_unkBool4", dump(o.m_pausePending));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_centerGroupID", dump(o.m_centerGroupID));
    return d;
}
#endif

void load(In& in, EffectManagerState& o) {
    load(in, o.m_colorActions);
    load(in, o.m_pulseActions);
    load(in, o.m_unorderedMapInt_vectorPulseEffectAction);
    load(in, o.m_unorderedMapInt_vectorCountTriggerAction);
    load(in, o.m_unorderedSet_int1);
    load(in, o.m_mapInt_Int);
    load(in, o.m_unorderedMapInt_OpacityEffectAction);
    load(in, o.m_vectorTouchToggleAction);
    load(in, o.m_vectorCollisionTriggerAction);
    load(in, o.m_vectorToggleTriggerAction);
    load(in, o.m_vectorSpawnTriggerAction);
    load(in, o.m_itemCountMap);
    load(in, o.m_unorderedMapInt_bool);
    load(in, o.m_vectorGroupCommandObject2);
    load(in, o.m_unorderedMapInt_pair_double_double);
    load(in, o.m_unorderedSet_int2);
    load(in, o.m_timerItemMap);
    load(in, o.m_unorderedMapInt_vectorTimerTriggerAction);
}

#ifdef SPC_SMOKE
matjson::Value dump(EffectManagerState const& o) {
    auto d = matjson::Value::object();
    d.set("m_unkVecCAState", dump(o.m_colorActions));
    d.set("m_unkVecPulseEffectAction", dump(o.m_pulseActions));
    d.set("m_unorderedMapInt_vectorPulseEffectAction", dump(o.m_unorderedMapInt_vectorPulseEffectAction));
    d.set("m_unorderedMapInt_vectorCountTriggerAction", dump(o.m_unorderedMapInt_vectorCountTriggerAction));
    d.set("m_unorderedSet_int1", dump(o.m_unorderedSet_int1));
    d.set("m_mapInt_Int", dump(o.m_mapInt_Int));
    d.set("m_unorderedMapInt_OpacityEffectAction", dump(o.m_unorderedMapInt_OpacityEffectAction));
    d.set("m_vectorTouchToggleAction", dump(o.m_vectorTouchToggleAction));
    d.set("m_vectorCollisionTriggerAction", dump(o.m_vectorCollisionTriggerAction));
    d.set("m_vectorToggleTriggerAction", dump(o.m_vectorToggleTriggerAction));
    d.set("m_vectorSpawnTriggerAction", dump(o.m_vectorSpawnTriggerAction));
    d.set("m_itemCountMap", dump(o.m_itemCountMap));
    d.set("m_unorderedMapInt_bool", dump(o.m_unorderedMapInt_bool));
    d.set("m_vectorGroupCommandObject2", dump(o.m_vectorGroupCommandObject2));
    d.set("m_unorderedMapInt_pair_double_double", dump(o.m_unorderedMapInt_pair_double_double));
    d.set("m_unorderedSet_int2", dump(o.m_unorderedSet_int2));
    d.set("m_timerItemMap", dump(o.m_timerItemMap));
    d.set("m_unorderedMapInt_vectorTimerTriggerAction", dump(o.m_unorderedMapInt_vectorTimerTriggerAction));
    return d;
}
#endif

void load(In& in, EnterEffectAnimValue& o) {
    load(in, o.m_key);
    load(in, o.m_value);
    load(in, o.m_distance);
    load(in, o.m_duration);
    load(in, o.m_elapsed);
    load(in, o.m_easingType);
    load(in, o.m_easingRate);
    load(in, o.m_easingBuffer);
}

#ifdef SPC_SMOKE
matjson::Value dump(EnterEffectAnimValue const& o) {
    auto d = matjson::Value::object();
    d.set("m_key", dump(o.m_key));
    d.set("m_value", dump(o.m_value));
    d.set("m_distance", dump(o.m_distance));
    d.set("m_duration", dump(o.m_duration));
    d.set("m_elapsed", dump(o.m_elapsed));
    d.set("m_easingType", dump(o.m_easingType));
    d.set("m_easingRate", dump(o.m_easingRate));
    d.set("m_easingBuffer", dump(o.m_easingBuffer));
    return d;
}
#endif

void load(In& in, EnterEffectInstance& o) {
    load(in, o.m_enterEffectAnimMap);
    load(in, o.m_length);
    load(in, o.m_lengthVariance);
    load(in, o.m_offset);
    load(in, o.m_offsetVariance);
    load(in, o.m_offsetY);
    load(in, o.m_offsetYVariance);
    load(in, o.m_modFront);
    load(in, o.m_modBack);
    load(in, o.m_deadzone);
    load(in, o.m_moveDistance);
    load(in, o.m_moveDistanceVariance);
    load(in, o.m_moveAngle);
    load(in, o.m_moveAngleVariance);
    load(in, o.m_moveX);
    load(in, o.m_moveXVariance);
    load(in, o.m_moveY);
    load(in, o.m_moveYVariance);
    load(in, o.m_relativeFade);
    load(in, o.m_scaleX);
    load(in, o.m_scaleXVariance);
    load(in, o.m_scaleY);
    load(in, o.m_scaleYVariance);
    load(in, o.m_rotation);
    load(in, o.m_rotationVariance);
    load(in, o.m_tint);
    load(in, o.m_unusedKey285);
    load(in, o.m_toOpacity);
    load(in, o.m_fromOpacity);
    load(in, o.m_hsvValue);
    load(in, o.m_hue);
    load(in, o.m_saturation);
    load(in, o.m_value);
    load(in, o.m_gameObject);
    load(in, o.m_reversed);
    load(in, o.m_targetID);
    load(in, o.m_centerID);
    load(in, o.m_startOrder);
    load(in, o.m_easeIndex);
    load(in, o.m_paused);
    load(in, o.m_paused2);
    load(in, o.m_unkBool4);
    load(in, o.m_targetGroupIndex);
    load(in, o.m_easeIndices);
    load(in, o.m_controlID);
}

#ifdef SPC_SMOKE
matjson::Value dump(EnterEffectInstance const& o) {
    auto d = matjson::Value::object();
    d.set("m_enterEffectAnimMap", dump(o.m_enterEffectAnimMap));
    d.set("m_length", dump(o.m_length));
    d.set("m_lengthVariance", dump(o.m_lengthVariance));
    d.set("m_offset", dump(o.m_offset));
    d.set("m_offsetVariance", dump(o.m_offsetVariance));
    d.set("m_offsetY", dump(o.m_offsetY));
    d.set("m_offsetYVariance", dump(o.m_offsetYVariance));
    d.set("m_modFront", dump(o.m_modFront));
    d.set("m_modBack", dump(o.m_modBack));
    d.set("m_deadzone", dump(o.m_deadzone));
    d.set("m_moveDistance", dump(o.m_moveDistance));
    d.set("m_moveDistanceVariance", dump(o.m_moveDistanceVariance));
    d.set("m_moveAngle", dump(o.m_moveAngle));
    d.set("m_moveAngleVariance", dump(o.m_moveAngleVariance));
    d.set("m_moveX", dump(o.m_moveX));
    d.set("m_moveXVariance", dump(o.m_moveXVariance));
    d.set("m_moveY", dump(o.m_moveY));
    d.set("m_moveYVariance", dump(o.m_moveYVariance));
    d.set("m_relativeFade", dump(o.m_relativeFade));
    d.set("m_scaleX", dump(o.m_scaleX));
    d.set("m_scaleXVariance", dump(o.m_scaleXVariance));
    d.set("m_scaleY", dump(o.m_scaleY));
    d.set("m_scaleYVariance", dump(o.m_scaleYVariance));
    d.set("m_rotation", dump(o.m_rotation));
    d.set("m_rotationVariance", dump(o.m_rotationVariance));
    d.set("m_tint", dump(o.m_tint));
    d.set("m_unk074", dump(o.m_unusedKey285));
    d.set("m_toOpacity", dump(o.m_toOpacity));
    d.set("m_fromOpacity", dump(o.m_fromOpacity));
    d.set("m_hsvValue", dump(o.m_hsvValue));
    d.set("m_hue", dump(o.m_hue));
    d.set("m_saturation", dump(o.m_saturation));
    d.set("m_value", dump(o.m_value));
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_reversed", dump(o.m_reversed));
    d.set("m_targetID", dump(o.m_targetID));
    d.set("m_centerID", dump(o.m_centerID));
    d.set("m_unkFloat3", dump(o.m_startOrder));
    d.set("m_easeIndex", dump(o.m_easeIndex));
    d.set("m_paused", dump(o.m_paused));
    d.set("m_paused2", dump(o.m_paused2));
    d.set("m_unkBool4", dump(o.m_unkBool4));
    d.set("m_targetGroupIndex", dump(o.m_targetGroupIndex));
    d.set("m_easeIndices", dump(o.m_easeIndices));
    d.set("m_controlID", dump(o.m_controlID));
    return d;
}
#endif

void load(In& in, EventTriggerInstance& o) {
    load(in, o.m_targetID);
    load(in, o.m_uniqueID);
    load(in, o.m_controlID);
    load(in, o.m_inactive);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(EventTriggerInstance const& o) {
    auto d = matjson::Value::object();
    d.set("m_targetID", dump(o.m_targetID));
    d.set("m_uniqueID", dump(o.m_uniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_inactive", dump(o.m_inactive));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, FMODAudioState& o) {
    load(in, o.m_interval);
    load(in, o.m_elapsed);
    load(in, o.m_tweensForEffectChannels);
    load(in, o.m_tweensForEffectGroups);
    load(in, o.m_tweensForMusicChannels);
    load(in, o.m_volumeForEffectChannels);
    load(in, o.m_volumeModForEffectChannels);
    load(in, o.m_pitchForEffectChannels);
    load(in, o.m_volumeForEffectGroups);
    load(in, o.m_volumeModForEffectGroups);
    load(in, o.m_pitchForEffectGroups);
    load(in, o.m_volumeForMusicChannels);
    load(in, o.m_volumeModForMusicChannels);
    load(in, o.m_pitchForMusicChannels);
    load(in, o.m_intervalForEffects);
    load(in, o.m_queuedMusicForChannels1);
    load(in, o.m_queuedMusicForChannels2);
    load(in, o.m_soundStateForChannels);
    load(in, o.m_unkUint64_1);
}

#ifdef SPC_SMOKE
matjson::Value dump(FMODAudioState const& o) {
    auto d = matjson::Value::object();
    d.set("m_interval", dump(o.m_interval));
    d.set("m_elapsed", dump(o.m_elapsed));
    d.set("m_tweensForEffectChannels", dump(o.m_tweensForEffectChannels));
    d.set("m_tweensForEffectGroups", dump(o.m_tweensForEffectGroups));
    d.set("m_tweensForMusicChannels", dump(o.m_tweensForMusicChannels));
    d.set("m_volumeForEffectChannels", dump(o.m_volumeForEffectChannels));
    d.set("m_volumeModForEffectChannels", dump(o.m_volumeModForEffectChannels));
    d.set("m_pitchForEffectChannels", dump(o.m_pitchForEffectChannels));
    d.set("m_volumeForEffectGroups", dump(o.m_volumeForEffectGroups));
    d.set("m_volumeModForEffectGroups", dump(o.m_volumeModForEffectGroups));
    d.set("m_pitchForEffectGroups", dump(o.m_pitchForEffectGroups));
    d.set("m_volumeForMusicChannels", dump(o.m_volumeForMusicChannels));
    d.set("m_volumeModForMusicChannels", dump(o.m_volumeModForMusicChannels));
    d.set("m_pitchForMusicChannels", dump(o.m_pitchForMusicChannels));
    d.set("m_intervalForEffects", dump(o.m_intervalForEffects));
    d.set("m_queuedMusicForChannels1", dump(o.m_queuedMusicForChannels1));
    d.set("m_queuedMusicForChannels2", dump(o.m_queuedMusicForChannels2));
    d.set("m_soundStateForChannels", dump(o.m_soundStateForChannels));
    d.set("m_unkUint64_1", dump(o.m_unkUint64_1));
    return d;
}
#endif

void load(In& in, FMODQueuedMusic& o) {
    load(in, o.m_filePath);
    load(in, o.m_pitch);
    load(in, o.m_unkFloat2);
    load(in, o.m_volume);
    load(in, o.m_start);
    load(in, o.m_end);
    load(in, o.m_fadeIn);
    load(in, o.m_fadeOut);
    load(in, o.m_loop);
    load(in, o.m_musicID);
    load(in, o.m_stopMusic);
    load(in, o.m_channelID);
    load(in, o.m_noPrepare);
    load(in, o.m_queuedStatus);
    [[maybe_unused]] auto l_dummy = in.get<int>();
    load(in, o.m_dontReset);
}

#ifdef SPC_SMOKE
matjson::Value dump(FMODQueuedMusic const& o) {
    auto d = matjson::Value::object();
    d.set("m_filePath", dump(o.m_filePath));
    d.set("m_pitch", dump(o.m_pitch));
    d.set("m_unkFloat2", dump(o.m_unkFloat2));
    d.set("m_volume", dump(o.m_volume));
    d.set("m_start", dump(o.m_start));
    d.set("m_end", dump(o.m_end));
    d.set("m_fadeIn", dump(o.m_fadeIn));
    d.set("m_fadeOut", dump(o.m_fadeOut));
    d.set("m_loop", dump(o.m_loop));
    d.set("m_musicID", dump(o.m_musicID));
    d.set("m_stopMusic", dump(o.m_stopMusic));
    d.set("m_channelID", dump(o.m_channelID));
    d.set("m_noPrepare", dump(o.m_noPrepare));
    d.set("m_queuedStatus", dump(o.m_queuedStatus));
    d.set("m_dontReset", dump(o.m_dontReset));
    return d;
}
#endif

void load(In& in, FMODSoundState& o) {
    load(in, o.m_filePath);
    load(in, o.m_speed);
    load(in, o.m_unkFloat1);
    load(in, o.m_volume);
    load(in, o.m_shouldLoop);
    load(in, o.m_channelID);
    load(in, o.m_soundStateContainer);
    load(in, o.m_uniqueID);
    load(in, o.m_sfxGroup);
    load(in, o.m_pitch);
    load(in, o.m_fastFourierTransform);
    load(in, o.m_reverb);
    load(in, o.m_effectID);
    load(in, o.m_isMusic);
    load(in, o.m_musicID);
    load(in, o.m_unkBool2);
}

#ifdef SPC_SMOKE
matjson::Value dump(FMODSoundState const& o) {
    auto d = matjson::Value::object();
    d.set("m_filePath", dump(o.m_filePath));
    d.set("m_speed", dump(o.m_speed));
    d.set("m_unkFloat1", dump(o.m_unkFloat1));
    d.set("m_volume", dump(o.m_volume));
    d.set("m_shouldLoop", dump(o.m_shouldLoop));
    d.set("m_channelID", dump(o.m_channelID));
    d.set("m_soundStateContainer", dump(o.m_soundStateContainer));
    d.set("m_uniqueID", dump(o.m_uniqueID));
    d.set("m_sfxGroup", dump(o.m_sfxGroup));
    d.set("m_pitch", dump(o.m_pitch));
    d.set("m_fastFourierTransform", dump(o.m_fastFourierTransform));
    d.set("m_reverb", dump(o.m_reverb));
    d.set("m_effectID", dump(o.m_effectID));
    d.set("m_isMusic", dump(o.m_isMusic));
    d.set("m_musicID", dump(o.m_musicID));
    d.set("m_unkBool2", dump(o.m_unkBool2));
    return d;
}
#endif

void load(In& in, FMODSoundTween& o) {
    load(in, o.m_interval);
    load(in, o.m_duration);
    load(in, o.m_start);
    load(in, o.m_end);
    load(in, o.m_value);
    load(in, o.m_finished);
    in.skip(7);  // padding PersistenceAPI copied as it was in memory
}

#ifdef SPC_SMOKE
matjson::Value dump(FMODSoundTween const& o) {
    auto d = matjson::Value::object();
    d.set("m_interval", dump(o.m_interval));
    d.set("m_duration", dump(o.m_duration));
    d.set("m_start", dump(o.m_start));
    d.set("m_end", dump(o.m_end));
    d.set("m_value", dump(o.m_value));
    d.set("m_finished", dump(o.m_finished));
    return d;
}
#endif

void load(In& in, GJGameState& o) {
    load(in, o.m_cameraZoom);
    load(in, o.m_targetCameraZoom);
    load(in, o.m_cameraOffset);
    load(in, o.m_cameraOffsetTarget);
    load(in, o.m_lastCameraTarget);
    load(in, o.m_lastCameraOffset);
    load(in, o.m_cameraDeltaPosition);
    load(in, o.m_staticCameraStart);
    load(in, o.m_staticCameraPosition);
    load(in, o.m_staticCameraTarget);
    load(in, o.m_staticExitOffset);
    load(in, o.m_appliedStaticExitOffset);
    load(in, o.m_staticStartVelocity);
    load(in, o.m_staticCameraDuration);
    load(in, o.m_staticTransitionProgress);
    load(in, o.m_lastStaticTransitionProgress);
    load(in, o.m_staticFollowEasing);
    load(in, o.m_staticFollow);
    load(in, o.m_staticFollowGroup);
    load(in, o.m_staticCameraActive);
    load(in, o.m_staticExitPending);
    load(in, o.m_staticExitDuration);
    load(in, o.m_staticExitEasingRate);
    load(in, o.m_staticExitEasing);
    load(in, o.m_staticSmoothVelocity);
    load(in, o.m_staticSmoothVelocityMod);
    load(in, o.m_staticExitStartOffset);
    load(in, o.m_unkPoint25);
    load(in, o.m_internalCameraOffset);
    load(in, o.m_internalCameraOffsetTarget);
    load(in, o.m_lastInternalCameraOffsetTarget);
    load(in, o.m_unkPoint29);
    load(in, o.m_dualStaticCamera);
    load(in, o.m_gameplayOffsetX);
    load(in, o.m_gameplayOffsetXDontZoom);
    load(in, o.m_gameplayOffsetY);
    load(in, o.m_gameplayOffsetYDontZoom);
    load(in, o.m_snapInternalCameraOffset);
    load(in, o.m_middleGroundOffsetY);
    static_assert(sizeof(o.m_middleGroundOffsetYTarget) == 4);
    in.raw(&o.m_middleGroundOffsetYTarget, 4);  // m_unkInt3 (int): copy bytes: already a float in 2.2081 (bindings 2.2081 called it int)
    static_assert(sizeof(o.m_middleGroundOffsetYDuration) == 4);
    in.raw(&o.m_middleGroundOffsetYDuration, 4);  // m_unkInt4 (int): copy bytes: already a float in 2.2081 (bindings 2.2081 called it int)
    load(in, o.m_cameraMoveXActive);
    load(in, o.m_cameraMoveYActive);
    load(in, o.m_cameraPadding);
    load(in, o.m_cameraEasing);
    load(in, o.m_unkInt5);
    load(in, o.m_unkInt6);
    load(in, o.m_unkInt7);
    load(in, o.m_unkInt8);
    load(in, o.m_unkInt9);
    static_assert(sizeof(o.m_flyCameraY) == 4);
    in.raw(&o.m_flyCameraY, 4);  // m_unkInt10 (int): copy bytes: already a float in 2.2081 (bindings 2.2081 called it int)
    static_assert(sizeof(o.m_lastModeHeight) == 4);
    in.raw(&o.m_lastModeHeight, 4);  // m_unkInt11: int to PersistenceAPI, float since
    load(in, o.m_lastCameraZoom);
    load(in, o.m_zoomDuration);
    load(in, o.m_portalY);
    load(in, o.m_portalYDuringZoom);
    load(in, o.m_gravityRelated);
    static_assert(sizeof(o.m_cameraCenterY) == 4);
    in.raw(&o.m_cameraCenterY, 4);  // m_unkInt12 (int): copy bytes: already a float in 2.2081 (bindings 2.2081 called it int)
    load(in, o.m_cameraMoveYTarget);
    static_assert(sizeof(o.m_flyCameraTargetY) == 4);
    in.raw(&o.m_flyCameraTargetY, 4);  // m_unkInt14 (int): copy bytes: already a float in 2.2081 (bindings 2.2081 called it int)
    static_assert(sizeof(o.m_unkInt15) == 4);
    in.raw(&o.m_unkInt15, 4);  // m_unkInt15 (int): copy bytes: always 0 in 2.2081 (either type)
    load(in, o.m_flyGroundActive);
    load(in, o.m_isFreeMode);
    load(in, o.m_disableCameraGridSnap);
    load(in, o.m_backgroundSpeedX);
    load(in, o.m_backgroundSpeedY);
    load(in, o.m_middlegroundSpeedX);
    load(in, o.m_middlegroundSpeedY);
    load(in, o.m_cameraAngle);
    load(in, o.m_targetCameraAngle);
    load(in, o.m_playerStreakBlend);
    load(in, o.m_timeWarp);
    load(in, o.m_timeWarpRelated);
    load(in, o.m_currentChannel);
    load(in, o.m_rotateChannel);
    load(in, o.m_spawnChannelRelated0);
    load(in, o.m_spawnChannelRelated1);
    load(in, o.m_totalTime);
    load(in, o.m_levelTime);
    load(in, o.m_bestTime);
    load(in, o.m_commandIndex);
    load(in, o.m_unkUint3);
    load(in, o.m_currentProgress);
    load(in, o.m_stepCommandIndex);
    static_assert(sizeof(o.m_positionHistoryTime) == 4);
    in.raw(&o.m_positionHistoryTime, 4);  // m_unkUint5 (int): copy bytes: signedness only
    static_assert(sizeof(o.m_positionHistoryClock) == 4);
    in.raw(&o.m_positionHistoryClock, 4);  // m_unkUint6 (int): copy bytes: signedness only
    static_assert(sizeof(o.m_areaEffectCounter) == 4);
    in.raw(&o.m_areaEffectCounter, 4);  // m_unkUint7 (int): copy bytes: signedness only
    load(in, o.m_lastActivatedPortal1);
    load(in, o.m_lastActivatedPortal2);
    load(in, o.m_cameraPosition);
    load(in, o.m_isFlipped);
    load(in, o.m_levelFlipProgress);
    load(in, o.m_inSecondHalfOfFlipAnimation);
    load(in, o.m_isFlipping);
    load(in, o.m_isDualMode);
    load(in, o.m_dualAnimationProgress);
    load(in, o.m_tweenActions);
    load(in, o.m_cameraEdgeValue0);
    load(in, o.m_cameraEdgeValue1);
    load(in, o.m_cameraEdgeValue2);
    load(in, o.m_cameraEdgeValue3);
    load(in, o.m_gameObjectPhysics);
    load(in, o.m_positionHistory);
    static_assert(sizeof(o.m_unkUint10) == 4);
    in.raw(&o.m_unkUint10, 4);  // m_unkUint10 (float): copy bytes: always 0.0 in 2.2081 (either type)
    load(in, o.m_positionHistoryIndex);
    load(in, o.m_cameraHistorySlot);
    load(in, o.m_cameraStepDiff);
    load(in, o.m_backgroundWidth);
    load(in, o.m_timeModRelated);
    load(in, o.m_timeModRelated2);
    load(in, o.m_activatedObjectIDs);
    load(in, o.m_teleportCameraEaseTimer);
    load(in, o.m_lastCameraEase);
    load(in, o.m_cameraLastPosition);
    load(in, o.m_player1GoingSideways);
    load(in, o.m_player1GoingLeft);
    load(in, o.m_audioOnDeath);
    load(in, o.m_respawnTime);
    load(in, o.m_noDeathSFX);
    load(in, o.m_cameraShakeEnabled);
    load(in, o.m_cameraShakeDuration);
    load(in, o.m_cameraShakeStrength);
    load(in, o.m_cameraShakeInterval);
    load(in, o.m_lastShakeTime);
    load(in, o.m_cameraShakeOffset);
    load(in, o.m_dualRelated);
    load(in, o.m_stateObjects);
    load(in, o.m_gameEventTriggers);
    load(in, o.m_gameEventCounts);
    load(in, o.m_enterEffectInstanceVectors);
    load(in, o.m_exitEffectInstanceVectors);
    load(in, o.m_enterChannelMap);
    load(in, o.m_exitChannelMap);
    load(in, o.m_moveEffectInstances);
    load(in, o.m_rotateEffectInstances);
    load(in, o.m_scaleEffectInstances);
    load(in, o.m_fadeEffectInstances);
    load(in, o.m_tintEffectInstances);
    load(in, o.m_unsortedAreaEffects);
    load(in, o.m_advancedFollowNeedsSort);
    load(in, o.m_advanceFollowInstances);
    load(in, o.m_dynamicMoveActions);
    load(in, o.m_dynamicRotateActions);
    load(in, o.m_instantExitStaticCameraX);
    load(in, o.m_instantExitStaticCameraY);
    load(in, o.m_maxPortalY);
    load(in, o.m_sfxTriggerChannels);
    load(in, o.m_proximityVolumeRelated);
    load(in, o.m_songChannelStates);
    load(in, o.m_songTriggerStateVectors);
    load(in, o.m_sfxTriggerStates);
    load(in, o.m_mainSongStopped);
    load(in, o.m_background);
    load(in, o.m_ground);
    load(in, o.m_middleground);
    load(in, o.m_unlinkDualGravity);
    load(in, o.m_points);
    load(in, o.m_hasObtainedPoints);
    load(in, o.m_pauseCounter);
    load(in, o.m_pauseBufferTimer);
}

#ifdef SPC_SMOKE
matjson::Value dump(GJGameState const& o) {
    auto d = matjson::Value::object();
    d.set("m_cameraZoom", dump(o.m_cameraZoom));
    d.set("m_targetCameraZoom", dump(o.m_targetCameraZoom));
    d.set("m_cameraOffset", dump(o.m_cameraOffset));
    d.set("m_unkPoint1", dump(o.m_cameraOffsetTarget));
    d.set("m_unkPoint2", dump(o.m_lastCameraTarget));
    d.set("m_unkPoint3", dump(o.m_lastCameraOffset));
    d.set("m_unkPoint4", dump(o.m_cameraDeltaPosition));
    d.set("m_unkPoint5", dump(o.m_staticCameraStart));
    d.set("m_unkPoint6", dump(o.m_staticCameraPosition));
    d.set("m_unkPoint7", dump(o.m_staticCameraTarget));
    d.set("m_unkPoint8", dump(o.m_staticExitOffset));
    d.set("m_unkPoint9", dump(o.m_appliedStaticExitOffset));
    d.set("m_unkPoint10", dump(o.m_staticStartVelocity));
    d.set("m_unkPoint11", dump(o.m_staticCameraDuration));
    d.set("m_unkPoint12", dump(o.m_staticTransitionProgress));
    d.set("m_unkPoint13", dump(o.m_lastStaticTransitionProgress));
    d.set("m_unkPoint14", dump(o.m_staticFollowEasing));
    d.set("m_unkPoint15", dump(o.m_staticFollow));
    d.set("m_unkPoint16", dump(o.m_staticFollowGroup));
    d.set("m_unkPoint17", dump(o.m_staticCameraActive));
    d.set("m_unkPoint18", dump(o.m_staticExitPending));
    d.set("m_unkPoint19", dump(o.m_staticExitDuration));
    d.set("m_unkPoint20", dump(o.m_staticExitEasingRate));
    d.set("m_unkPoint21", dump(o.m_staticExitEasing));
    d.set("m_unkPoint22", dump(o.m_staticSmoothVelocity));
    d.set("m_unkPoint23", dump(o.m_staticSmoothVelocityMod));
    d.set("m_unkPoint24", dump(o.m_staticExitStartOffset));
    d.set("m_unkPoint25", dump(o.m_unkPoint25));
    d.set("m_unkPoint26", dump(o.m_internalCameraOffset));
    d.set("m_unkPoint27", dump(o.m_internalCameraOffsetTarget));
    d.set("m_unkPoint28", dump(o.m_lastInternalCameraOffsetTarget));
    d.set("m_unkPoint29", dump(o.m_unkPoint29));
    d.set("m_unkBool1", dump(o.m_dualStaticCamera));
    d.set("m_unkInt1", dump(o.m_gameplayOffsetX));
    d.set("m_unkBool2", dump(o.m_gameplayOffsetXDontZoom));
    d.set("m_unkInt2", dump(o.m_gameplayOffsetY));
    d.set("m_unkBool3", dump(o.m_gameplayOffsetYDontZoom));
    d.set("m_unkPoint30", dump(o.m_snapInternalCameraOffset));
    d.set("m_middleGroundOffsetY", dump(o.m_middleGroundOffsetY));
    d.set("m_unkInt3", dump(bitsAs<int>(o.m_middleGroundOffsetYTarget)));
    d.set("m_unkInt4", dump(bitsAs<int>(o.m_middleGroundOffsetYDuration)));
    d.set("m_unkBool4", dump(o.m_cameraMoveXActive));
    d.set("m_unkBool5", dump(o.m_cameraMoveYActive));
    d.set("m_unkFloat2", dump(o.m_cameraPadding));
    d.set("m_unkFloat3", dump(o.m_cameraEasing));
    d.set("m_unkInt5", dump(o.m_unkInt5));
    d.set("m_unkInt6", dump(o.m_unkInt6));
    d.set("m_unkInt7", dump(o.m_unkInt7));
    d.set("m_unkInt8", dump(o.m_unkInt8));
    d.set("m_unkInt9", dump(o.m_unkInt9));
    d.set("m_unkInt10", dump(bitsAs<int>(o.m_flyCameraY)));
    d.set("m_unkInt11", dump(bitsAs<int>(o.m_lastModeHeight)));
    d.set("m_unkFloat4", dump(o.m_lastCameraZoom));
    d.set("m_unkUint1", dump(o.m_zoomDuration));
    d.set("m_portalY", dump(o.m_portalY));
    d.set("m_unkBool6", dump(o.m_portalYDuringZoom));
    d.set("m_gravityRelated", dump(o.m_gravityRelated));
    d.set("m_unkInt12", dump(bitsAs<int>(o.m_cameraCenterY)));
    d.set("m_unkInt13", dump(o.m_cameraMoveYTarget));
    d.set("m_unkInt14", dump(bitsAs<int>(o.m_flyCameraTargetY)));
    d.set("m_unkInt15", dump(bitsAs<int>(o.m_unkInt15)));
    d.set("m_unkBool7", dump(o.m_flyGroundActive));
    d.set("m_unkBool8", dump(o.m_isFreeMode));
    d.set("m_unkBool9", dump(o.m_disableCameraGridSnap));
    d.set("m_unkFloat5", dump(o.m_backgroundSpeedX));
    d.set("m_unkFloat6", dump(o.m_backgroundSpeedY));
    d.set("m_unkFloat7", dump(o.m_middlegroundSpeedX));
    d.set("m_unkFloat8", dump(o.m_middlegroundSpeedY));
    d.set("m_cameraAngle", dump(o.m_cameraAngle));
    d.set("m_targetCameraAngle", dump(o.m_targetCameraAngle));
    d.set("m_playerStreakBlend", dump(o.m_playerStreakBlend));
    d.set("m_timeWarp", dump(o.m_timeWarp));
    d.set("m_timeWarpRelated", dump(o.m_timeWarpRelated));
    d.set("m_currentChannel", dump(o.m_currentChannel));
    d.set("m_rotateChannel", dump(o.m_rotateChannel));
    d.set("m_spawnChannelRelated0", dump(o.m_spawnChannelRelated0));
    d.set("m_spawnChannelRelated1", dump(o.m_spawnChannelRelated1));
    d.set("m_totalTime", dump(o.m_totalTime));
    d.set("m_levelTime", dump(o.m_levelTime));
    d.set("m_unkDouble3", dump(o.m_bestTime));
    d.set("m_commandIndex", dump(o.m_commandIndex));
    d.set("m_unkUint3", dump(o.m_unkUint3));
    d.set("m_currentProgress", dump(o.m_currentProgress));
    d.set("m_unkUint4", dump(o.m_stepCommandIndex));
    d.set("m_unkUint5", dump(bitsAs<int>(o.m_positionHistoryTime)));
    d.set("m_unkUint6", dump(bitsAs<int>(o.m_positionHistoryClock)));
    d.set("m_unkUint7", dump(bitsAs<int>(o.m_areaEffectCounter)));
    d.set("m_lastActivatedPortal1", dump(o.m_lastActivatedPortal1));
    d.set("m_lastActivatedPortal2", dump(o.m_lastActivatedPortal2));
    d.set("m_cameraPosition", dump(o.m_cameraPosition));
    d.set("m_unkBool10", dump(o.m_isFlipped));
    d.set("m_levelFlipping", dump(o.m_levelFlipProgress));
    d.set("m_unkBool11", dump(o.m_inSecondHalfOfFlipAnimation));
    d.set("m_unkBool12", dump(o.m_isFlipping));
    d.set("m_isDualMode", dump(o.m_isDualMode));
    d.set("m_unkFloat9", dump(o.m_dualAnimationProgress));
    d.set("m_tweenActions", dump(o.m_tweenActions));
    d.set("m_cameraEdgeValue0", dump(o.m_cameraEdgeValue0));
    d.set("m_cameraEdgeValue1", dump(o.m_cameraEdgeValue1));
    d.set("m_cameraEdgeValue2", dump(o.m_cameraEdgeValue2));
    d.set("m_cameraEdgeValue3", dump(o.m_cameraEdgeValue3));
    d.set("m_gameObjectPhysics", dump(o.m_gameObjectPhysics));
    d.set("m_unkVecFloat1", dump(o.m_positionHistory));
    d.set("m_unkUint10", dump(bitsAs<float>(o.m_unkUint10)));
    d.set("m_unkUint11", dump(o.m_positionHistoryIndex));
    d.set("m_unkUint12", dump(o.m_cameraHistorySlot));
    d.set("m_cameraStepDiff", dump(o.m_cameraStepDiff));
    d.set("m_unkFloat10", dump(o.m_backgroundWidth));
    d.set("m_timeModRelated", dump(o.m_timeModRelated));
    d.set("m_timeModRelated2", dump(o.m_timeModRelated2));
    d.set("m_activatedObjectIDs", dump(o.m_activatedObjectIDs));
    d.set("m_unkUint13", dump(o.m_teleportCameraEaseTimer));
    d.set("m_unkPoint32", dump(o.m_lastCameraEase));
    d.set("m_cameraPosition2", dump(o.m_cameraLastPosition));
    d.set("m_unkBool20", dump(o.m_player1GoingSideways));
    d.set("m_unkBool21", dump(o.m_player1GoingLeft));
    d.set("m_unkBool22", dump(o.m_audioOnDeath));
    d.set("m_unkUint14", dump(o.m_respawnTime));
    d.set("m_unkBool26", dump(o.m_noDeathSFX));
    d.set("m_cameraShakeEnabled", dump(o.m_cameraShakeEnabled));
    d.set("m_cameraShakeFactor", dump(o.m_cameraShakeDuration));
    d.set("m_unkUint15", dump(o.m_cameraShakeStrength));
    d.set("m_unkUint16", dump(o.m_cameraShakeInterval));
    d.set("m_unkUint64_1", dump(o.m_lastShakeTime));
    d.set("m_unkPoint34", dump(o.m_cameraShakeOffset));
    d.set("m_dualRelated", dump(o.m_dualRelated));
    d.set("m_stateObjects", dump(o.m_stateObjects));
    d.set("m_unkMapPairGJGameEventIntVectorEventTriggerInstance", dump(o.m_gameEventTriggers));
    d.set("m_unkMapPairGJGameEventIntInt", dump(o.m_gameEventCounts));
    d.set("m_enterEffectInstanceVectors", dump(o.m_enterEffectInstanceVectors));
    d.set("m_exitEffectInstanceVectors", dump(o.m_exitEffectInstanceVectors));
    d.set("m_enterChannelMap", dump(o.m_enterChannelMap));
    d.set("m_exitChannelMap", dump(o.m_exitChannelMap));
    d.set("m_moveEffectInstances", dump(o.m_moveEffectInstances));
    d.set("m_rotateEffectInstances", dump(o.m_rotateEffectInstances));
    d.set("m_scaleEffectInstances", dump(o.m_scaleEffectInstances));
    d.set("m_fadeEffectInstances", dump(o.m_fadeEffectInstances));
    d.set("m_tintEffectInstances", dump(o.m_tintEffectInstances));
    d.set("m_unsortedAreaEffects", dump(o.m_unsortedAreaEffects));
    d.set("m_unkBool27", dump(o.m_advancedFollowNeedsSort));
    d.set("m_advanceFollowInstances", dump(o.m_advanceFollowInstances));
    d.set("m_dynamicMoveActions", dump(o.m_dynamicMoveActions));
    d.set("m_dynamicRotateActions", dump(o.m_dynamicRotateActions));
    d.set("m_unkBool28", dump(o.m_instantExitStaticCameraX));
    d.set("m_unkBool29", dump(o.m_instantExitStaticCameraY));
    d.set("m_unkUint17", dump(o.m_maxPortalY));
    d.set("m_unkUMap8", dump(o.m_sfxTriggerChannels));
    d.set("m_proximityVolumeRelated", dump(o.m_proximityVolumeRelated));
    d.set("m_songChannelStates", dump(o.m_songChannelStates));
    d.set("m_songTriggerStateVectors", dump(o.m_songTriggerStateVectors));
    d.set("m_sfxTriggerStates", dump(o.m_sfxTriggerStates));
    d.set("m_unkBool30", dump(o.m_mainSongStopped));
    d.set("m_background", dump(o.m_background));
    d.set("m_ground", dump(o.m_ground));
    d.set("m_middleground", dump(o.m_middleground));
    d.set("m_unkBool31", dump(o.m_unlinkDualGravity));
    d.set("m_points", dump(o.m_points));
    d.set("m_unkBool32", dump(o.m_hasObtainedPoints));
    d.set("m_pauseCounter", dump(o.m_pauseCounter));
    d.set("m_pauseBufferTimer", dump(o.m_pauseBufferTimer));
    return d;
}
#endif

void load(In& in, GJShaderState& o) {
    load(in, o.m_tweenActions);
    load(in, o.m_tweenTimes);
    load(in, o.m_time);
    load(in, o.m_prevTime);
    load(in, o.m_startTime);
    load(in, o.m_textureScaleX);
    load(in, o.m_textureScaleY);
    load(in, o.m_blurRefColor);
    load(in, o.m_blurIntensity);
    load(in, o.m_blurRefChannel);
    load(in, o.m_blurOnlyEmpty);
    load(in, o.m_shockWaveStartTime);
    load(in, o.m_shockWaveTime);
    load(in, o.m_shockWaveTimeOffset);
    load(in, o.m_shockWaveSpeed);
    load(in, o.m_shockWaveThickness);
    load(in, o.m_shockWaveStrength);
    load(in, o.m_shockWaveWidth);
    load(in, o.m_shockWaveFadeIn);
    load(in, o.m_shockWaveFadeOut);
    load(in, o.m_shockWaveInvert);
    load(in, o.m_shockWaveInner);
    load(in, o.m_shockWaveOuter);
    load(in, o.m_shockWaveTargetID);
    load(in, o.m_shockWaveTarget);
    load(in, o.m_shockWaveCenterMoving);
    load(in, o.m_shockWaveRelative);
    load(in, o.m_shockWaveMaxSize);
    load(in, o.m_shockWaveScreenOffset);
    load(in, o.m_shockWaveCenterDirty);
    load(in, o.m_shockWaveCenter);
    load(in, o.m_shockLineStartTime);
    load(in, o.m_shockLineTime);
    load(in, o.m_shockLineTimeOffset);
    load(in, o.m_shockLineSpeed);
    load(in, o.m_shockLineAxis);
    load(in, o.m_shockLineDirection);
    load(in, o.m_shockLineDual);
    load(in, o.m_shockLineInvert);
    load(in, o.m_shockLineScreenOffset);
    load(in, o.m_shockLineStrength);
    load(in, o.m_shockLineWidth);
    load(in, o.m_shockLineFadeIn);
    load(in, o.m_shockLineFadeOut);
    load(in, o.m_shockLineThickness);
    load(in, o.m_shockLineTargetID);
    load(in, o.m_shockLineTarget);
    load(in, o.m_shockLineCenterMoving);
    load(in, o.m_shockLineRelative);
    load(in, o.m_shockLineMaxSize);
    load(in, o.m_shockLineScreenOffset2);
    load(in, o.m_shockLineCenterDirty);
    load(in, o.m_shockLineCenter);
    load(in, o.m_glitchStrength);
    load(in, o.m_glitchSpeed);
    load(in, o.m_glitchSliceHeight);
    load(in, o.m_glitchMaxSliceXOff);
    load(in, o.m_glitchMaxColXOff);
    load(in, o.m_glitchMaxColYOff);
    load(in, o.m_glitchRelative);
    load(in, o.m_chromaticTargetX);
    load(in, o.m_chromaticTargetY);
    load(in, o.m_chromaticRelative);
    load(in, o.m_cGStartTime);
    load(in, o.m_cGTime);
    load(in, o.m_cGSpeed);
    load(in, o.m_cGRGBOffset);
    load(in, o.m_cGStrength);
    load(in, o.m_cGSegmentHeight);
    load(in, o.m_cGLineThickness);
    load(in, o.m_cGLineStrength);
    load(in, o.m_cGEnable);
    load(in, o.m_cGActive);
    load(in, o.m_cGRelative);
    load(in, o.m_cGRelativePos);
    load(in, o.m_pixelateTargetX);
    load(in, o.m_pixelateTargetY);
    load(in, o.m_pixelateSnapGrid);
    load(in, o.m_pixelatePixelating);
    load(in, o.m_pixelateRelative);
    load(in, o.m_pixelateHardEdges);
    load(in, o.m_lensCircleSize);
    load(in, o.m_lensCircleFade);
    load(in, o.m_lensCircleStrength);
    load(in, o.m_lensCircleTargetID);
    load(in, o.m_lensCircleTintChannel);
    load(in, o.m_lensCircleRelative);
    load(in, o.m_lensCircleAdditive);
    load(in, o.m_lensCircleTint);
    load(in, o.m_lensCircleCenter);
    load(in, o.m_lensCircleScreenOffset);
    load(in, o.m_radialBlurSize);
    load(in, o.m_radialBlurFade);
    load(in, o.m_radialBlurTarget);
    load(in, o.m_radialBlurTargetID);
    load(in, o.m_radialBlurCenter);
    load(in, o.m_radialBlurScreenOffset);
    load(in, o.m_motionBlurTargetX);
    load(in, o.m_motionBlurTargetY);
    load(in, o.m_motionBlurSpeedX);
    load(in, o.m_motionBlurSpeedY);
    load(in, o.m_motionBlurFollowEaseX);
    load(in, o.m_motionBlurFollowEaseY);
    load(in, o.m_motionBlurTargetIDX);
    load(in, o.m_motionBlurTargetIDY);
    load(in, o.m_motionBlurDual);
    load(in, o.m_motionBlurRelative);
    load(in, o.m_bulgeValue);
    load(in, o.m_bulgeRelative);
    load(in, o.m_bulgeTargetID);
    load(in, o.m_bulgeRadius);
    load(in, o.m_bulgeCenter);
    load(in, o.m_bulgeScreenOffset);
    load(in, o.m_pinchTargetX);
    load(in, o.m_pinchTargetY);
    load(in, o.m_pinchTargetEnabledX);
    load(in, o.m_pinchTargetEnabledY);
    load(in, o.m_pinchTargetIDX);
    load(in, o.m_pinchTargetIDY);
    load(in, o.m_pinchCenter);
    load(in, o.m_pinchScreenOffset);
    load(in, o.m_pinchRelative);
    load(in, o.m_pinchRadius);
    load(in, o.m_pinchModifier);
    load(in, o.m_grayscaleValue);
    load(in, o.m_grayscaleUseLum);
    load(in, o.m_grayscaleTintChannel);
    load(in, o.m_grayscaleTint);
    load(in, o.m_sepiaValue);
    load(in, o.m_invertColorEditRGB);
    load(in, o.m_invertColorR);
    load(in, o.m_invertColorG);
    load(in, o.m_invertColorB);
    load(in, o.m_invertColorClampRGB);
    load(in, o.m_hueShiftDegrees);
    load(in, o.m_colorChangeCR);
    load(in, o.m_colorChangeCG);
    load(in, o.m_colorChangeCB);
    load(in, o.m_colorChangeBR);
    load(in, o.m_colorChangeBG);
    load(in, o.m_colorChangeBB);
    o.m_splitTargetRows = in.get<float>();  // m_splitTargetRows: float -> double (value converted)
    o.m_splitTargetCols = in.get<float>();  // m_splitTargetCols: float -> double (value converted)
    load(in, o.m_splitActive);
    load(in, o.m_minBlendingLayer);
    load(in, o.m_maxBlendingLayer);
    load(in, o.m_zLayerDirty);
    load(in, o.m_noPlayerParticles);
    load(in, o.m_usesShaders);
}

#ifdef SPC_SMOKE
matjson::Value dump(GJShaderState const& o) {
    auto d = matjson::Value::object();
    d.set("m_tweenActions", dump(o.m_tweenActions));
    d.set("m_tweenTimes", dump(o.m_tweenTimes));
    d.set("m_time", dump(o.m_time));
    d.set("m_prevTime", dump(o.m_prevTime));
    d.set("m_startTime", dump(o.m_startTime));
    d.set("m_textureScaleX", dump(o.m_textureScaleX));
    d.set("m_textureScaleY", dump(o.m_textureScaleY));
    d.set("m_blurRefColor", dump(o.m_blurRefColor));
    d.set("m_blurIntensity", dump(o.m_blurIntensity));
    d.set("m_blurRefChannel", dump(o.m_blurRefChannel));
    d.set("m_blurOnlyEmpty", dump(o.m_blurOnlyEmpty));
    d.set("m_shockWaveStartTime", dump(o.m_shockWaveStartTime));
    d.set("m_shockWaveTime", dump(o.m_shockWaveTime));
    d.set("m_shockWaveTimeOffset", dump(o.m_shockWaveTimeOffset));
    d.set("m_shockWaveSpeed", dump(o.m_shockWaveSpeed));
    d.set("m_shockWaveThickness", dump(o.m_shockWaveThickness));
    d.set("m_shockWaveStrength", dump(o.m_shockWaveStrength));
    d.set("m_shockWaveWidth", dump(o.m_shockWaveWidth));
    d.set("m_shockWaveFadeIn", dump(o.m_shockWaveFadeIn));
    d.set("m_shockWaveFadeOut", dump(o.m_shockWaveFadeOut));
    d.set("m_shockWaveInvert", dump(o.m_shockWaveInvert));
    d.set("m_shockWaveInner", dump(o.m_shockWaveInner));
    d.set("m_shockWaveOuter", dump(o.m_shockWaveOuter));
    d.set("m_shockWaveTargetID", dump(o.m_shockWaveTargetID));
    d.set("m_shockWaveTarget", dump(o.m_shockWaveTarget));
    d.set("m_shockWaveCenterMoving", dump(o.m_shockWaveCenterMoving));
    d.set("m_shockWaveRelative", dump(o.m_shockWaveRelative));
    d.set("m_shockWaveMaxSize", dump(o.m_shockWaveMaxSize));
    d.set("m_shockWaveScreenOffset", dump(o.m_shockWaveScreenOffset));
    d.set("m_shockWaveCenterDirty", dump(o.m_shockWaveCenterDirty));
    d.set("m_shockWaveCenter", dump(o.m_shockWaveCenter));
    d.set("m_shockLineStartTime", dump(o.m_shockLineStartTime));
    d.set("m_shockLineTime", dump(o.m_shockLineTime));
    d.set("m_shockLineTimeOffset", dump(o.m_shockLineTimeOffset));
    d.set("m_shockLineSpeed", dump(o.m_shockLineSpeed));
    d.set("m_shockLineAxis", dump(o.m_shockLineAxis));
    d.set("m_shockLineDirection", dump(o.m_shockLineDirection));
    d.set("m_shockLineDual", dump(o.m_shockLineDual));
    d.set("m_shockLineInvert", dump(o.m_shockLineInvert));
    d.set("m_shockLineScreenOffset", dump(o.m_shockLineScreenOffset));
    d.set("m_shockLineStrength", dump(o.m_shockLineStrength));
    d.set("m_shockLineWidth", dump(o.m_shockLineWidth));
    d.set("m_shockLineFadeIn", dump(o.m_shockLineFadeIn));
    d.set("m_shockLineFadeOut", dump(o.m_shockLineFadeOut));
    d.set("m_shockLineThickness", dump(o.m_shockLineThickness));
    d.set("m_shockLineTargetID", dump(o.m_shockLineTargetID));
    d.set("m_shockLineTarget", dump(o.m_shockLineTarget));
    d.set("m_shockLineCenterMoving", dump(o.m_shockLineCenterMoving));
    d.set("m_shockLineRelative", dump(o.m_shockLineRelative));
    d.set("m_shockLineMaxSize", dump(o.m_shockLineMaxSize));
    d.set("m_shockLineScreenOffset2", dump(o.m_shockLineScreenOffset2));
    d.set("m_shockLineCenterDirty", dump(o.m_shockLineCenterDirty));
    d.set("m_shockLineCenter", dump(o.m_shockLineCenter));
    d.set("m_glitchStrength", dump(o.m_glitchStrength));
    d.set("m_glitchSpeed", dump(o.m_glitchSpeed));
    d.set("m_glitchSliceHeight", dump(o.m_glitchSliceHeight));
    d.set("m_glitchMaxSliceXOff", dump(o.m_glitchMaxSliceXOff));
    d.set("m_glitchMaxColXOff", dump(o.m_glitchMaxColXOff));
    d.set("m_glitchMaxColYOff", dump(o.m_glitchMaxColYOff));
    d.set("m_glitchRelative", dump(o.m_glitchRelative));
    d.set("m_chromaticTargetX", dump(o.m_chromaticTargetX));
    d.set("m_chromaticTargetY", dump(o.m_chromaticTargetY));
    d.set("m_chromaticRelative", dump(o.m_chromaticRelative));
    d.set("m_cGStartTime", dump(o.m_cGStartTime));
    d.set("m_cGTime", dump(o.m_cGTime));
    d.set("m_cGSpeed", dump(o.m_cGSpeed));
    d.set("m_cGRGBOffset", dump(o.m_cGRGBOffset));
    d.set("m_cGStrength", dump(o.m_cGStrength));
    d.set("m_cGSegmentHeight", dump(o.m_cGSegmentHeight));
    d.set("m_cGLineThickness", dump(o.m_cGLineThickness));
    d.set("m_cGLineStrength", dump(o.m_cGLineStrength));
    d.set("m_cGEnable", dump(o.m_cGEnable));
    d.set("m_cGActive", dump(o.m_cGActive));
    d.set("m_cGRelative", dump(o.m_cGRelative));
    d.set("m_cGRelativePos", dump(o.m_cGRelativePos));
    d.set("m_pixelateTargetX", dump(o.m_pixelateTargetX));
    d.set("m_pixelateTargetY", dump(o.m_pixelateTargetY));
    d.set("m_pixelateSnapGrid", dump(o.m_pixelateSnapGrid));
    d.set("m_pixelatePixelating", dump(o.m_pixelatePixelating));
    d.set("m_pixelateRelative", dump(o.m_pixelateRelative));
    d.set("m_pixelateHardEdges", dump(o.m_pixelateHardEdges));
    d.set("m_lensCircleSize", dump(o.m_lensCircleSize));
    d.set("m_lensCircleFade", dump(o.m_lensCircleFade));
    d.set("m_lensCircleStrength", dump(o.m_lensCircleStrength));
    d.set("m_lensCircleTargetID", dump(o.m_lensCircleTargetID));
    d.set("m_lensCircleTintChannel", dump(o.m_lensCircleTintChannel));
    d.set("m_lensCircleRelative", dump(o.m_lensCircleRelative));
    d.set("m_lensCircleAdditive", dump(o.m_lensCircleAdditive));
    d.set("m_lensCircleTint", dump(o.m_lensCircleTint));
    d.set("m_lensCircleCenter", dump(o.m_lensCircleCenter));
    d.set("m_lensCircleScreenOffset", dump(o.m_lensCircleScreenOffset));
    d.set("m_radialBlurSize", dump(o.m_radialBlurSize));
    d.set("m_radialBlurFade", dump(o.m_radialBlurFade));
    d.set("m_radialBlurTarget", dump(o.m_radialBlurTarget));
    d.set("m_radialBlurTargetID", dump(o.m_radialBlurTargetID));
    d.set("m_radialBlurCenter", dump(o.m_radialBlurCenter));
    d.set("m_radialBlurScreenOffset", dump(o.m_radialBlurScreenOffset));
    d.set("m_motionBlurTargetX", dump(o.m_motionBlurTargetX));
    d.set("m_motionBlurTargetY", dump(o.m_motionBlurTargetY));
    d.set("m_motionBlurSpeedX", dump(o.m_motionBlurSpeedX));
    d.set("m_motionBlurSpeedY", dump(o.m_motionBlurSpeedY));
    d.set("m_motionBlurFollowEaseX", dump(o.m_motionBlurFollowEaseX));
    d.set("m_motionBlurFollowEaseY", dump(o.m_motionBlurFollowEaseY));
    d.set("m_motionBlurTargetIDX", dump(o.m_motionBlurTargetIDX));
    d.set("m_motionBlurTargetIDY", dump(o.m_motionBlurTargetIDY));
    d.set("m_motionBlurDual", dump(o.m_motionBlurDual));
    d.set("m_motionBlurRelative", dump(o.m_motionBlurRelative));
    d.set("m_bulgeValue", dump(o.m_bulgeValue));
    d.set("m_bulgeRelative", dump(o.m_bulgeRelative));
    d.set("m_bulgeTargetID", dump(o.m_bulgeTargetID));
    d.set("m_bulgeRadius", dump(o.m_bulgeRadius));
    d.set("m_bulgeCenter", dump(o.m_bulgeCenter));
    d.set("m_bulgeScreenOffset", dump(o.m_bulgeScreenOffset));
    d.set("m_pinchTargetX", dump(o.m_pinchTargetX));
    d.set("m_pinchTargetY", dump(o.m_pinchTargetY));
    d.set("m_pinchTargetEnabledX", dump(o.m_pinchTargetEnabledX));
    d.set("m_pinchTargetEnabledY", dump(o.m_pinchTargetEnabledY));
    d.set("m_pinchTargetIDX", dump(o.m_pinchTargetIDX));
    d.set("m_pinchTargetIDY", dump(o.m_pinchTargetIDY));
    d.set("m_pinchCenter", dump(o.m_pinchCenter));
    d.set("m_pinchScreenOffset", dump(o.m_pinchScreenOffset));
    d.set("m_pinchRelative", dump(o.m_pinchRelative));
    d.set("m_pinchRadius", dump(o.m_pinchRadius));
    d.set("m_pinchModifier", dump(o.m_pinchModifier));
    d.set("m_grayscaleValue", dump(o.m_grayscaleValue));
    d.set("m_grayscaleUseLum", dump(o.m_grayscaleUseLum));
    d.set("m_grayscaleTintChannel", dump(o.m_grayscaleTintChannel));
    d.set("m_grayscaleTint", dump(o.m_grayscaleTint));
    d.set("m_sepiaValue", dump(o.m_sepiaValue));
    d.set("m_invertColorEditRGB", dump(o.m_invertColorEditRGB));
    d.set("m_invertColorR", dump(o.m_invertColorR));
    d.set("m_invertColorG", dump(o.m_invertColorG));
    d.set("m_invertColorB", dump(o.m_invertColorB));
    d.set("m_invertColorClampRGB", dump(o.m_invertColorClampRGB));
    d.set("m_hueShiftDegrees", dump(o.m_hueShiftDegrees));
    d.set("m_colorChangeCR", dump(o.m_colorChangeCR));
    d.set("m_colorChangeCG", dump(o.m_colorChangeCG));
    d.set("m_colorChangeCB", dump(o.m_colorChangeCB));
    d.set("m_colorChangeBR", dump(o.m_colorChangeBR));
    d.set("m_colorChangeBG", dump(o.m_colorChangeBG));
    d.set("m_colorChangeBB", dump(o.m_colorChangeBB));
    d.set("m_splitTargetRows", dump(static_cast<float>(o.m_splitTargetRows)));
    d.set("m_splitTargetCols", dump(static_cast<float>(o.m_splitTargetCols)));
    d.set("m_splitActive", dump(o.m_splitActive));
    d.set("m_minBlendingLayer", dump(o.m_minBlendingLayer));
    d.set("m_maxBlendingLayer", dump(o.m_maxBlendingLayer));
    d.set("m_zLayerDirty", dump(o.m_zLayerDirty));
    d.set("m_noPlayerParticles", dump(o.m_noPlayerParticles));
    d.set("m_usesShaders", dump(o.m_usesShaders));
    return d;
}
#endif

void load(In& in, GJValueTween& o) {
    load(in, o.m_fromValue);
    load(in, o.m_toValue);
    load(in, o.m_duration);
    load(in, o.m_deltaTime);
    load(in, o.m_currentValue);
    load(in, o.m_easingType);
    load(in, o.m_easingRate);
    load(in, o.m_finished);
    load(in, o.m_disabled);
    load(in, o.m_uniqueID);
    load(in, o.m_controlID);
}

#ifdef SPC_SMOKE
matjson::Value dump(GJValueTween const& o) {
    auto d = matjson::Value::object();
    d.set("m_fromValue", dump(o.m_fromValue));
    d.set("m_toValue", dump(o.m_toValue));
    d.set("m_duration", dump(o.m_duration));
    d.set("m_deltaTime", dump(o.m_deltaTime));
    d.set("m_currentValue", dump(o.m_currentValue));
    d.set("m_easingType", dump(o.m_easingType));
    d.set("m_easingRate", dump(o.m_easingRate));
    d.set("m_finished", dump(o.m_finished));
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_uniqueID", dump(o.m_uniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    return d;
}
#endif

void load(In& in, GameObjectPhysics& o) {
    load(in, o.m_gameObject);
    load(in, o.m_velocity);
    load(in, o.m_direction);
    load(in, o.m_targetAngle);
    load(in, o.m_turnSpeed);
    load(in, o.m_unkInt1);
    load(in, o.m_lastUpdateStep);
    load(in, o.m_exclusiveStep);
}

#ifdef SPC_SMOKE
matjson::Value dump(GameObjectPhysics const& o) {
    auto d = matjson::Value::object();
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_unkPoint1", dump(o.m_velocity));
    d.set("m_unkPoint2", dump(o.m_direction));
    d.set("m_unkFloat1", dump(o.m_targetAngle));
    d.set("m_unkFloat2", dump(o.m_turnSpeed));
    d.set("m_unkInt1", dump(o.m_unkInt1));
    d.set("m_unkInt2", dump(o.m_lastUpdateStep));
    d.set("m_unkInt3", dump(o.m_exclusiveStep));
    return d;
}
#endif

void load(In& in, GroupCommandObject2& o) {
    load(in, o.m_groupCommandUniqueID);
    load(in, o.m_moveOffset);
    load(in, o.m_easingType);
    load(in, o.m_easingRate);
    load(in, o.m_duration);
    load(in, o.m_deltaTime);
    load(in, o.m_targetGroupID);
    load(in, o.m_centerGroupID);
    load(in, o.m_currentXOffset);
    load(in, o.m_currentYOffset);
    load(in, o.m_deltaX);
    load(in, o.m_deltaY);
    load(in, o.m_oldDeltaX);
    load(in, o.m_oldDeltaY);
    load(in, o.m_lockedCurrentXOffset);
    load(in, o.m_lockedCurrentYOffset);
    load(in, o.m_finished);
    load(in, o.m_disabled);
    load(in, o.m_finishRelated);
    load(in, o.m_lockToPlayerX);
    load(in, o.m_lockToPlayerY);
    load(in, o.m_lockToCameraX);
    load(in, o.m_lockToCameraY);
    load(in, o.m_lockedInX);
    load(in, o.m_lockedInY);
    load(in, o.m_moveModX);
    load(in, o.m_moveModY);
    load(in, o.m_currentRotateOrTransformValue);
    load(in, o.m_currentRotateOrTransformDelta);
    load(in, o.m_someInterpValue1RelatedOne);
    load(in, o.m_someInterpValue2RelatedOne);
    load(in, o.m_rotationOffset);
    load(in, o.m_lockObjectRotation);
    load(in, o.m_targetPlayer);
    load(in, o.m_followXMod);
    load(in, o.m_followYMod);
    load(in, o.m_commandType);
    load(in, o.m_someInterpValue1);
    load(in, o.m_someInterpValue2);
    load(in, o.m_keyframeRelated);
    load(in, o.m_targetScaleX);
    load(in, o.m_targetScaleY);
    load(in, o.m_transformTriggerProperty450);
    load(in, o.m_transformTriggerProperty451);
    load(in, o.m_someInterpValue1RelatedZero);
    load(in, o.m_someInterpValue2RelatedZero);
    load(in, o.m_onlyMove);
    load(in, o.m_transformRelatedFalse);
    load(in, o.m_relativeRotation);
    load(in, o.m_someInterpValue1Related);
    load(in, o.m_someInterpValue2Related);
    load(in, o.m_followYSpeed);
    load(in, o.m_followYDelay);
    load(in, o.m_followYOffset);
    load(in, o.m_followYMaxSpeed);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_deltaX_3);
    load(in, o.m_deltaY_3);
    load(in, o.m_oldDeltaX_3);
    load(in, o.m_oldDeltaY_3);
    load(in, o.m_Delta_3_Related);
    load(in, o.m_unkDoubleMaybeUnused);
    load(in, o.m_actionType1);
    load(in, o.m_actionType2);
    load(in, o.m_actionValue1);
    load(in, o.m_actionValue2);
    load(in, o.m_someInterpValue1RelatedFalse);
    load(in, o.m_deltaTimeInFloat);
    load(in, o.m_alreadyUpdated);
    load(in, o.m_doUpdate);
    load(in, o.m_keyframes);
    load(in, o.m_splineRelated);
    load(in, o.m_gameObject);
    load(in, o.m_gameObjectRotation);
    load(in, o.m_remapKeys);
    load(in, o.m_someInterpValue2RelatedTrue);
    load(in, o.m_stepStamp);
}

#ifdef SPC_SMOKE
matjson::Value dump(GroupCommandObject2 const& o) {
    auto d = matjson::Value::object();
    d.set("m_groupCommandUniqueID", dump(o.m_groupCommandUniqueID));
    d.set("m_moveOffset", dump(o.m_moveOffset));
    d.set("m_easingType", dump(o.m_easingType));
    d.set("m_easingRate", dump(o.m_easingRate));
    d.set("m_duration", dump(o.m_duration));
    d.set("m_deltaTime", dump(o.m_deltaTime));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_centerGroupID", dump(o.m_centerGroupID));
    d.set("m_currentXOffset", dump(o.m_currentXOffset));
    d.set("m_currentYOffset", dump(o.m_currentYOffset));
    d.set("m_deltaX", dump(o.m_deltaX));
    d.set("m_deltaY", dump(o.m_deltaY));
    d.set("m_oldDeltaX", dump(o.m_oldDeltaX));
    d.set("m_oldDeltaY", dump(o.m_oldDeltaY));
    d.set("m_lockedCurrentXOffset", dump(o.m_lockedCurrentXOffset));
    d.set("m_lockedCurrentYOffset", dump(o.m_lockedCurrentYOffset));
    d.set("m_finished", dump(o.m_finished));
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_finishRelated", dump(o.m_finishRelated));
    d.set("m_lockToPlayerX", dump(o.m_lockToPlayerX));
    d.set("m_lockToPlayerY", dump(o.m_lockToPlayerY));
    d.set("m_lockToCameraX", dump(o.m_lockToCameraX));
    d.set("m_lockToCameraY", dump(o.m_lockToCameraY));
    d.set("m_lockedInX", dump(o.m_lockedInX));
    d.set("m_lockedInY", dump(o.m_lockedInY));
    d.set("m_moveModX", dump(o.m_moveModX));
    d.set("m_moveModY", dump(o.m_moveModY));
    d.set("m_currentRotateOrTransformValue", dump(o.m_currentRotateOrTransformValue));
    d.set("m_currentRotateOrTransformDelta", dump(o.m_currentRotateOrTransformDelta));
    d.set("m_someInterpValue1RelatedOne", dump(o.m_someInterpValue1RelatedOne));
    d.set("m_someInterpValue2RelatedOne", dump(o.m_someInterpValue2RelatedOne));
    d.set("m_rotationOffset", dump(o.m_rotationOffset));
    d.set("m_lockObjectRotation", dump(o.m_lockObjectRotation));
    d.set("m_targetPlayer", dump(o.m_targetPlayer));
    d.set("m_followXMod", dump(o.m_followXMod));
    d.set("m_followYMod", dump(o.m_followYMod));
    d.set("m_commandType", dump(o.m_commandType));
    d.set("m_someInterpValue1", dump(o.m_someInterpValue1));
    d.set("m_someInterpValue2", dump(o.m_someInterpValue2));
    d.set("m_keyframeRelated", dump(o.m_keyframeRelated));
    d.set("m_targetScaleX", dump(o.m_targetScaleX));
    d.set("m_targetScaleY", dump(o.m_targetScaleY));
    d.set("m_transformTriggerProperty450", dump(o.m_transformTriggerProperty450));
    d.set("m_transformTriggerProperty451", dump(o.m_transformTriggerProperty451));
    d.set("m_someInterpValue1RelatedZero", dump(o.m_someInterpValue1RelatedZero));
    d.set("m_someInterpValue2RelatedZero", dump(o.m_someInterpValue2RelatedZero));
    d.set("m_onlyMove", dump(o.m_onlyMove));
    d.set("m_transformRelatedFalse", dump(o.m_transformRelatedFalse));
    d.set("m_relativeRotation", dump(o.m_relativeRotation));
    d.set("m_someInterpValue1Related", dump(o.m_someInterpValue1Related));
    d.set("m_someInterpValue2Related", dump(o.m_someInterpValue2Related));
    d.set("m_followYSpeed", dump(o.m_followYSpeed));
    d.set("m_followYDelay", dump(o.m_followYDelay));
    d.set("m_followYOffset", dump(o.m_followYOffset));
    d.set("m_followYMaxSpeed", dump(o.m_followYMaxSpeed));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_deltaX_3", dump(o.m_deltaX_3));
    d.set("m_deltaY_3", dump(o.m_deltaY_3));
    d.set("m_oldDeltaX_3", dump(o.m_oldDeltaX_3));
    d.set("m_oldDeltaY_3", dump(o.m_oldDeltaY_3));
    d.set("m_Delta_3_Related", dump(o.m_Delta_3_Related));
    d.set("m_unkDoubleMaybeUnused", dump(o.m_unkDoubleMaybeUnused));
    d.set("m_actionType1", dump(o.m_actionType1));
    d.set("m_actionType2", dump(o.m_actionType2));
    d.set("m_actionValue1", dump(o.m_actionValue1));
    d.set("m_actionValue2", dump(o.m_actionValue2));
    d.set("m_someInterpValue1RelatedFalse", dump(o.m_someInterpValue1RelatedFalse));
    d.set("m_deltaTimeInFloat", dump(o.m_deltaTimeInFloat));
    d.set("m_alreadyUpdated", dump(o.m_alreadyUpdated));
    d.set("m_doUpdate", dump(o.m_doUpdate));
    d.set("m_keyframes", dump(o.m_keyframes));
    d.set("m_splineRelated", dump(o.m_splineRelated));
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_gameObjectRotation", dump(o.m_gameObjectRotation));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    d.set("m_someInterpValue2RelatedTrue", dump(o.m_someInterpValue2RelatedTrue));
    d.set("m_unkInt204", dump(o.m_stepStamp));
    return d;
}
#endif

void load(In& in, KeyframeObject& o) {
    load(in, o.m_duration);
    load(in, o.m_easingType);
    load(in, o.m_easingRate);
    load(in, o.m_refOnly);
    load(in, o.m_spawnGroup);
    load(in, o.m_spawnDelay);
    load(in, o.m_proximity);
    load(in, o.m_spawned);
    load(in, o.m_curve);
    load(in, o.m_spline1);
    load(in, o.m_spline2);
    load(in, o.m_curveLength);
    load(in, o.m_refRunDuration);
    load(in, o.m_timeMode);
    load(in, o.m_keyframeObject);
    load(in, o.m_positionDelta);
    load(in, o.m_position);
    load(in, o.m_scaleX);
    load(in, o.m_scaleY);
    load(in, o.m_rotation);
    load(in, o.m_unk1b0);
    load(in, o.m_opacity);
}

#ifdef SPC_SMOKE
matjson::Value dump(KeyframeObject const& o) {
    auto d = matjson::Value::object();
    d.set("m_unk000", dump(o.m_duration));
    d.set("m_unk008", dump(o.m_easingType));
    d.set("m_unk00c", dump(o.m_easingRate));
    d.set("m_unk010", dump(o.m_refOnly));
    d.set("m_unk014", dump(o.m_spawnGroup));
    d.set("m_unk018", dump(o.m_spawnDelay));
    d.set("m_unk01c", dump(o.m_proximity));
    d.set("m_unk01d", dump(o.m_spawned));
    d.set("m_unk01e", dump(o.m_curve));
    d.set("m_spline1", dump(o.m_spline1));
    d.set("m_spline2", dump(o.m_spline2));
    d.set("m_unk170", dump(o.m_curveLength));
    d.set("m_unk178", dump(o.m_refRunDuration));
    d.set("m_unk180", dump(o.m_timeMode));
    d.set("m_unk184", dump(o.m_keyframeObject));
    d.set("m_unk188", dump(o.m_positionDelta));
    d.set("m_unk190", dump(o.m_position));
    d.set("m_unk198", dump(o.m_scaleX));
    d.set("m_unk1a0", dump(o.m_scaleY));
    d.set("m_unk1a8", dump(o.m_rotation));
    d.set("m_unk1b0", dump(o.m_unk1b0));
    d.set("m_unk1b8", dump(o.m_opacity));
    return d;
}
#endif

void load(In& in, OpacityEffectAction& o) {
    load(in, o.m_duration);
    load(in, o.m_fromValue);
    load(in, o.m_toValue);
    load(in, o.m_finished);
    load(in, o.m_disabled);
    load(in, o.m_deltaTime);
    load(in, o.m_targetGroupID);
    load(in, o.m_currentValue);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_deltaTimeRelated);
    load(in, o.m_durationRelated);
}

#ifdef SPC_SMOKE
matjson::Value dump(OpacityEffectAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_duration", dump(o.m_duration));
    d.set("m_fromValue", dump(o.m_fromValue));
    d.set("m_toValue", dump(o.m_toValue));
    d.set("m_finished", dump(o.m_finished));
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_deltaTime", dump(o.m_deltaTime));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_currentValue", dump(o.m_currentValue));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_deltaTimeRelated", dump(o.m_deltaTimeRelated));
    d.set("m_durationRelated", dump(o.m_durationRelated));
    return d;
}
#endif

void load(In& in, PlayerCheckpoint& o) {
    skipCCNode(in);  // its CCNode part: not kept
    load(in, o.m_position);
    load(in, o.m_lastPosition);
    load(in, o.m_yVelocityUnrounded);
    load(in, o.m_isUpsideDown);
    load(in, o.m_isSideways);
    load(in, o.m_isShip);
    load(in, o.m_isBall);
    load(in, o.m_isBird);
    load(in, o.m_isSwing);
    load(in, o.m_isDart);
    load(in, o.m_isRobot);
    load(in, o.m_isSpider);
    load(in, o.m_isOnGround);
    load(in, o.m_ghostType);
    load(in, o.m_isMini);
    load(in, o.m_playerSpeed);
    load(in, o.m_isHidden);
    load(in, o.m_isGoingLeft);
    load(in, o.m_maybeReverseSpeed);
    load(in, o.m_jumpBuffered);
    load(in, o.m_isDashing);
    load(in, o.m_dashStartTimeold);
    load(in, o.m_dashX);
    load(in, o.m_dashY);
    load(in, o.m_dashAngle);
    load(in, o.m_dashStartTime);
    load(in, o.m_dashRing);
    load(in, o.m_platformerCheckpoint);
    load(in, o.m_lastFlipTime);
    load(in, o.m_gravityMod);
    load(in, o.m_objectSnappedTo);
    load(in, o.m_snapDistance);
    load(in, o.m_accelerationOrSpeed);
    load(in, o.m_decreaseBoostSlide);
    load(in, o.m_followRelated);
    load(in, o.m_playerFollowFloats);
    load(in, o.m_followSampleTimer);
    load(in, o.m_isOnSlope);
    load(in, o.m_wasOnSlope);
    load(in, o.m_slopeVelocity);
    load(in, o.m_rotation);
    load(in, o.m_wasTeleported);
    load(in, o.m_fixGravityBug);
    load(in, o.m_reverseSync);
    load(in, o.m_yVelocityBeforeSlope);
    load(in, o.m_slopeStartTime);
    load(in, o.m_justPlacedStreak);
    load(in, o.m_lastCollisionBottom);
    load(in, o.m_lastCollisionTop);
    load(in, o.m_lastCollisionLeft);
    load(in, o.m_lastCollisionRight);
    load(in, o.m_prevCollisionBottom);
    load(in, o.m_prevCollisionTop);
    load(in, o.m_currentSlope2);
    load(in, o.m_preLastGroundObject);
    load(in, o.m_slopeAngle);
    load(in, o.m_slopeSlidingMaybeRotated);
    load(in, o.m_quickCheckpointMode);
    load(in, o.m_collidedObject);
    load(in, o.m_lastGroundObject);
    load(in, o.m_collidingWithLeft);
    load(in, o.m_collidingWithRight);
    load(in, o.m_scaleXRelated2);
    load(in, o.m_groundYVelocity);
    load(in, o.m_yVelocityRelated);
    load(in, o.m_scaleXRelated3);
    load(in, o.m_scaleXRelated4);
    load(in, o.m_scaleXRelated5);
    load(in, o.m_isCollidingWithSlope);
    load(in, o.m_isBallRotating);
    load(in, o.m_noPotentialSlope);
    load(in, o.m_currentPotentialSlope);
    load(in, o.m_currentSlope);
    load(in, o.m_slopeCenterOffset);
    load(in, o.m_collidingWithSlopeId);
    load(in, o.m_slopeIsDownhill);
    load(in, o.m_slopeAngleRadians);
    load(in, o.m_rotationSpeed);
    load(in, o.m_rotateSpeed);
    load(in, o.m_isRotating);
    load(in, o.m_ballSpinning);
    load(in, o.m_speedMultiplier);
    load(in, o.m_yStart);
    load(in, o.m_gravity);
    load(in, o.m_trailingParticleLife);
    load(in, o.m_gameModeChangedTime);
    load(in, o.m_ringJumpedSincePress);
    load(in, o.m_hasStartedFalling);
    load(in, o.m_shouldTryPlacingCheckpoint);
    load(in, o.m_playEffects);
    load(in, o.m_landedSinceFlip);
    load(in, o.m_lastSpiderFlipTime);
    load(in, o.m_unkBool5);
    load(in, o.m_usedRingThisStep);
    load(in, o.m_usedRings);
    load(in, o.m_specialGroundHit);
    load(in, o.m_landParticlesAngle);
    load(in, o.m_landParticlesGravityY);
    load(in, o.m_slopeRotation);
    load(in, o.m_currentSlopeYVelocity);
    load(in, o.m_lastSlopeSnapY);
    load(in, o.m_lastSlopeSnapOffset);
    load(in, o.m_lastSlopeSnapAtFarEdge);
    load(in, o.m_lastSlopeSnapAtNearEdge);
    load(in, o.m_isAccelerating);
    load(in, o.m_isCurrentSlopeTop);
    load(in, o.m_collidedTopMinY);
    load(in, o.m_collidedBottomMaxY);
    load(in, o.m_collidedLeftMaxX);
    load(in, o.m_collidedRightMinX);
    load(in, o.m_wasJumpBuffered);
    load(in, o.m_wasRobotJump);
    static_assert(sizeof(o.m_stateJumpBuffered) == 1);
    in.raw(&o.m_stateJumpBuffered, 1);  // m_stateJumpBuffered (unsigned char): copy bytes: 0/1 in 2.2081
    load(in, o.m_statePreJump);
    load(in, o.m_touchedRing);
    load(in, o.m_touchedCustomRing);
    load(in, o.m_touchedGravityPortal);
    load(in, o.m_brokeBlockSincePress);
    load(in, o.m_touchedPad);
    load(in, o.m_yVelocity);
    load(in, o.m_fallSpeed);
    load(in, o.m_slopeResnapped);
    load(in, o.m_reverseCooldown);
    load(in, o.m_reverseSyncStep);
    load(in, o.m_ballRollSpeed);
    load(in, o.m_shipRotation);
    load(in, o.m_lastPortalPos);
    load(in, o.m_unkUnused3);
    load(in, o.m_touchingGround);
    load(in, o.m_lastLandTime);
    load(in, o.m_decelerationRamp);
    load(in, o.m_isRising);
    load(in, o.m_lastGroundXDeltaTime);
    load(in, o.m_isLocked);
    load(in, o.m_controlsDisabled);
    load(in, o.m_lastGroundedPos);
    [[maybe_unused]] auto l_hasTouchingRingsArray = in.get<uint8_t>();
    if (l_hasTouchingRingsArray != 0) {
        o.m_touchingRings = loadArray<GameObject>(in, o.m_touchingRings);
    }
    load(in, o.m_touchedRings);
    load(in, o.m_lastActivatedPortal);
    load(in, o.m_totalTime);
    load(in, o.m_diagonalYStep);
    load(in, o.m_defaultMiniIcon);
    load(in, o.m_swapColors);
    load(in, o.m_switchDashFireColor);
    load(in, o.m_stateOnGround);
    in.skip(1);  // m_stateUnk (unsigned char): write 0 (float): always 0 in 2.2081
    o.m_unusedFloatA80 = 0;
    in.skip(1);  // m_stateNoStickX: gone in 2.2091 (drop: always 0 in 2.2081)
    in.skip(1);  // m_stateNoStickY: gone in 2.2091 (drop: always 0 in 2.2081)
    in.skip(1);  // m_stateUnk2: gone in 2.2091 (drop: always 0 in 2.2081)
    load(in, o.m_stateBoostX);
    load(in, o.m_stateBoostY);
    load(in, o.m_stateNoWallPush);
    load(in, o.m_stateScale);
    load(in, o.m_platformerXVelocity);
    load(in, o.m_holdingRight);
    load(in, o.m_holdingLeft);
    load(in, o.m_leftPressedFirst);
    load(in, o.m_platformXMove);
    load(in, o.m_hasPlatformMomentum);
    load(in, o.m_platformMomentum);
    load(in, o.m_forcedSlopeSlide);
    load(in, o.m_isSliding);
    load(in, o.m_slopeSpeedBonus);
    load(in, o.m_isOnIce);
    load(in, o.m_platformRideTime);
    load(in, o.m_wasTouchingGround);
    load(in, o.m_slopeJumpCount);
    load(in, o.m_slopeJumpResetTime);
    load(in, o.m_changedDirectionsTime);
    load(in, o.m_slopeEndTime);
    load(in, o.m_isMoving);
    load(in, o.m_forcedSlideLeft);
    load(in, o.m_forcedSlideRight);
    load(in, o.m_lastSlideLeft);
    load(in, o.m_slideSlopeAngle);
    load(in, o.m_unkUnused2);
    load(in, o.m_stateNoAutoJump);
    load(in, o.m_stateDartSlide);
    load(in, o.m_stateHitHead);
    load(in, o.m_stateFlipGravity);
    load(in, o.m_stateForce);
    load(in, o.m_stateForceVector);
    load(in, o.m_affectedByForces);
    load(in, o.m_touchedForceIds);
    load(in, o.m_fallStartY);
}

#ifdef SPC_SMOKE
matjson::Value dump(PlayerCheckpoint const& o) {
    auto d = matjson::Value::object();
    d.set("m_position", dump(o.m_position));
    d.set("m_lastPosition", dump(o.m_lastPosition));
    d.set("m_yVelocityUnrounded", dump(o.m_yVelocityUnrounded));
    d.set("m_isUpsideDown", dump(o.m_isUpsideDown));
    d.set("m_isSideways", dump(o.m_isSideways));
    d.set("m_isShip", dump(o.m_isShip));
    d.set("m_isBall", dump(o.m_isBall));
    d.set("m_isBird", dump(o.m_isBird));
    d.set("m_isSwing", dump(o.m_isSwing));
    d.set("m_isDart", dump(o.m_isDart));
    d.set("m_isRobot", dump(o.m_isRobot));
    d.set("m_isSpider", dump(o.m_isSpider));
    d.set("m_isOnGround", dump(o.m_isOnGround));
    d.set("m_ghostType", dump(o.m_ghostType));
    d.set("m_isMini", dump(o.m_isMini));
    d.set("m_playerSpeed", dump(o.m_playerSpeed));
    d.set("m_isHidden", dump(o.m_isHidden));
    d.set("m_isGoingLeft", dump(o.m_isGoingLeft));
    d.set("m_maybeReverseSpeed", dump(o.m_maybeReverseSpeed));
    d.set("m_jumpBuffered", dump(o.m_jumpBuffered));
    d.set("m_isDashing", dump(o.m_isDashing));
    d.set("m_dashStartTimeold", dump(o.m_dashStartTimeold));
    d.set("m_dashX", dump(o.m_dashX));
    d.set("m_dashY", dump(o.m_dashY));
    d.set("m_dashAngle", dump(o.m_dashAngle));
    d.set("m_dashStartTime", dump(o.m_dashStartTime));
    d.set("m_dashRing", dump(o.m_dashRing));
    d.set("m_platformerCheckpoint", dump(o.m_platformerCheckpoint));
    d.set("m_lastFlipTime", dump(o.m_lastFlipTime));
    d.set("m_gravityMod", dump(o.m_gravityMod));
    d.set("m_objectSnappedTo", dump(o.m_objectSnappedTo));
    d.set("m_snapDistance", dump(o.m_snapDistance));
    d.set("m_accelerationOrSpeed", dump(o.m_accelerationOrSpeed));
    d.set("m_decreaseBoostSlide", dump(o.m_decreaseBoostSlide));
    d.set("m_followRelated", dump(o.m_followRelated));
    d.set("m_playerFollowFloats", dump(o.m_playerFollowFloats));
    d.set("m_unk838", dump(o.m_followSampleTimer));
    d.set("m_isOnSlope", dump(o.m_isOnSlope));
    d.set("m_wasOnSlope", dump(o.m_wasOnSlope));
    d.set("m_slopeVelocity", dump(o.m_slopeVelocity));
    d.set("m_rotation", dump(o.m_rotation));
    d.set("m_wasTeleported", dump(o.m_wasTeleported));
    d.set("m_fixGravityBug", dump(o.m_fixGravityBug));
    d.set("m_reverseSync", dump(o.m_reverseSync));
    d.set("m_yVelocityBeforeSlope", dump(o.m_yVelocityBeforeSlope));
    d.set("m_slopeStartTime", dump(o.m_slopeStartTime));
    d.set("m_justPlacedStreak", dump(o.m_justPlacedStreak));
    d.set("m_lastCollisionBottom", dump(o.m_lastCollisionBottom));
    d.set("m_lastCollisionTop", dump(o.m_lastCollisionTop));
    d.set("m_lastCollisionLeft", dump(o.m_lastCollisionLeft));
    d.set("m_lastCollisionRight", dump(o.m_lastCollisionRight));
    d.set("m_unk50C", dump(o.m_prevCollisionBottom));
    d.set("m_unk510", dump(o.m_prevCollisionTop));
    d.set("m_currentSlope2", dump(o.m_currentSlope2));
    d.set("m_preLastGroundObject", dump(o.m_preLastGroundObject));
    d.set("m_slopeAngle", dump(o.m_slopeAngle));
    d.set("m_slopeSlidingMaybeRotated", dump(o.m_slopeSlidingMaybeRotated));
    d.set("m_quickCheckpointMode", dump(o.m_quickCheckpointMode));
    d.set("m_collidedObject", dump(o.m_collidedObject));
    d.set("m_lastGroundObject", dump(o.m_lastGroundObject));
    d.set("m_collidingWithLeft", dump(o.m_collidingWithLeft));
    d.set("m_collidingWithRight", dump(o.m_collidingWithRight));
    d.set("m_scaleXRelated2", dump(o.m_scaleXRelated2));
    d.set("m_groundYVelocity", dump(o.m_groundYVelocity));
    d.set("m_yVelocityRelated", dump(o.m_yVelocityRelated));
    d.set("m_scaleXRelated3", dump(o.m_scaleXRelated3));
    d.set("m_scaleXRelated4", dump(o.m_scaleXRelated4));
    d.set("m_scaleXRelated5", dump(o.m_scaleXRelated5));
    d.set("m_isCollidingWithSlope", dump(o.m_isCollidingWithSlope));
    d.set("m_isBallRotating", dump(o.m_isBallRotating));
    d.set("m_unk669", dump(o.m_noPotentialSlope));
    d.set("m_currentPotentialSlope", dump(o.m_currentPotentialSlope));
    d.set("m_currentSlope", dump(o.m_currentSlope));
    d.set("unk_584", dump(o.m_slopeCenterOffset));
    d.set("m_collidingWithSlopeId", dump(o.m_collidingWithSlopeId));
    d.set("m_slopeFlipGravityRelated", dump(o.m_slopeIsDownhill));
    d.set("m_slopeAngleRadians", dump(o.m_slopeAngleRadians));
    d.set("m_rotationSpeed", dump(o.m_rotationSpeed));
    d.set("m_rotateSpeed", dump(o.m_rotateSpeed));
    d.set("m_isRotating", dump(o.m_isRotating));
    d.set("m_isBallRotating2", dump(o.m_ballSpinning));
    d.set("m_speedMultiplier", dump(o.m_speedMultiplier));
    d.set("m_yStart", dump(o.m_yStart));
    d.set("m_gravity", dump(o.m_gravity));
    d.set("m_trailingParticleLife", dump(o.m_trailingParticleLife));
    d.set("m_gameModeChangedTime", dump(o.m_gameModeChangedTime));
    d.set("m_padRingRelated", dump(o.m_ringJumpedSincePress));
    d.set("m_maybeIsFalling", dump(o.m_hasStartedFalling));
    d.set("m_shouldTryPlacingCheckpoint", dump(o.m_shouldTryPlacingCheckpoint));
    d.set("m_playEffects", dump(o.m_playEffects));
    d.set("m_isOnGround3", dump(o.m_landedSinceFlip));
    d.set("m_lastSpiderFlipTime", dump(o.m_lastSpiderFlipTime));
    d.set("m_unkBool5", dump(o.m_unkBool5));
    d.set("m_ringJumpRelated", dump(o.m_usedRingThisStep));
    d.set("m_ringRelatedSet", dump(o.m_usedRings));
    d.set("m_maybeSpriteRelated", dump(o.m_specialGroundHit));
    d.set("m_landParticlesAngle", dump(o.m_landParticlesAngle));
    d.set("m_landParticleRelatedY", dump(o.m_landParticlesGravityY));
    d.set("m_slopeRotation", dump(o.m_slopeRotation));
    d.set("m_currentSlopeYVelocity", dump(o.m_currentSlopeYVelocity));
    d.set("m_unk3d0", dump(o.m_lastSlopeSnapY));
    d.set("m_blackOrbRelated", dump(o.m_lastSlopeSnapOffset));
    d.set("m_unk3e0", dump(o.m_lastSlopeSnapAtFarEdge));
    d.set("m_unk3e1", dump(o.m_lastSlopeSnapAtNearEdge));
    d.set("m_isAccelerating", dump(o.m_isAccelerating));
    d.set("m_isCurrentSlopeTop", dump(o.m_isCurrentSlopeTop));
    d.set("m_collidedTopMinY", dump(o.m_collidedTopMinY));
    d.set("m_collidedBottomMaxY", dump(o.m_collidedBottomMaxY));
    d.set("m_collidedLeftMaxX", dump(o.m_collidedLeftMaxX));
    d.set("m_collidedRightMinX", dump(o.m_collidedRightMinX));
    d.set("m_wasJumpBuffered", dump(o.m_wasJumpBuffered));
    d.set("m_wasRobotJump", dump(o.m_wasRobotJump));
    d.set("m_stateJumpBuffered", dump(bitsAs<uint8_t>(o.m_stateJumpBuffered)));
    d.set("m_stateRingJump2", dump(o.m_statePreJump));
    d.set("m_touchedRing", dump(o.m_touchedRing));
    d.set("m_touchedCustomRing", dump(o.m_touchedCustomRing));
    d.set("m_touchedGravityPortal", dump(o.m_touchedGravityPortal));
    d.set("m_maybeTouchedBreakableBlock", dump(o.m_brokeBlockSincePress));
    d.set("m_touchedPad", dump(o.m_touchedPad));
    d.set("m_yVelocity", dump(o.m_yVelocity));
    d.set("m_fallSpeed", dump(o.m_fallSpeed));
    d.set("m_maybeUpsideDownSlope", dump(o.m_slopeResnapped));
    d.set("m_reverseRelated", dump(o.m_reverseCooldown));
    d.set("m_maybeReverseAcceleration", dump(o.m_reverseSyncStep));
    d.set("m_xVelocityRelated2", dump(o.m_ballRollSpeed));
    d.set("m_shipRotation", dump(o.m_shipRotation));
    d.set("m_lastPortalPos", dump(o.m_lastPortalPos));
    d.set("m_unkUnused3", dump(o.m_unkUnused3));
    d.set("m_isOnGround2", dump(o.m_touchingGround));
    d.set("m_lastLandTime", dump(o.m_lastLandTime));
    d.set("m_platformerVelocityRelated", dump(o.m_decelerationRamp));
    d.set("m_maybeIsBoosted", dump(o.m_isRising));
    d.set("m_scaleXRelatedTime", dump(o.m_lastGroundXDeltaTime));
    d.set("m_isLocked", dump(o.m_isLocked));
    d.set("m_controlsDisabled", dump(o.m_controlsDisabled));
    d.set("m_lastGroundedPos", dump(o.m_lastGroundedPos));
    d.set("l_hasTouchingRingsArray", o.m_touchingRings != nullptr);
    if (o.m_touchingRings) {
        d.set("m_touchingRings", dumpArray(o.m_touchingRings));
    }
    d.set("m_touchedRings", dump(o.m_touchedRings));
    d.set("m_lastActivatedPortal", dump(o.m_lastActivatedPortal));
    d.set("m_totalTime", dump(o.m_totalTime));
    d.set("m_yVelocityRelated3", dump(o.m_diagonalYStep));
    d.set("m_defaultMiniIcon", dump(o.m_defaultMiniIcon));
    d.set("m_swapColors", dump(o.m_swapColors));
    d.set("m_switchDashFireColor", dump(o.m_switchDashFireColor));
    d.set("m_stateOnGround", dump(o.m_stateOnGround));
    d.set("m_stateBoostX", dump(o.m_stateBoostX));
    d.set("m_stateBoostY", dump(o.m_stateBoostY));
    d.set("m_maybeStateForce2", dump(o.m_stateNoWallPush));
    d.set("m_stateScale", dump(o.m_stateScale));
    d.set("m_platformerXVelocity", dump(o.m_platformerXVelocity));
    d.set("m_holdingRight", dump(o.m_holdingRight));
    d.set("m_holdingLeft", dump(o.m_holdingLeft));
    d.set("m_leftPressedFirst", dump(o.m_leftPressedFirst));
    d.set("m_scaleXRelated", dump(o.m_platformXMove));
    d.set("m_maybeHasStopped", dump(o.m_hasPlatformMomentum));
    d.set("m_xVelocityRelated", dump(o.m_platformMomentum));
    d.set("m_maybeGoingCorrectSlopeDirection", dump(o.m_forcedSlopeSlide));
    d.set("m_isSliding", dump(o.m_isSliding));
    d.set("m_maybeSlopeForce", dump(o.m_slopeSpeedBonus));
    d.set("m_isOnIce", dump(o.m_isOnIce));
    d.set("m_physDeltaRelated", dump(o.m_platformRideTime));
    d.set("m_isOnGround4", dump(o.m_wasTouchingGround));
    d.set("m_maybeSlidingTime", dump(o.m_slopeJumpCount));
    d.set("m_maybeSlidingStartTime", dump(o.m_slopeJumpResetTime));
    d.set("m_changedDirectionsTime", dump(o.m_changedDirectionsTime));
    d.set("m_slopeEndTime", dump(o.m_slopeEndTime));
    d.set("m_isMoving", dump(o.m_isMoving));
    d.set("m_platformerMovingLeft", dump(o.m_forcedSlideLeft));
    d.set("m_platformerMovingRight", dump(o.m_forcedSlideRight));
    d.set("m_isSlidingRight", dump(o.m_lastSlideLeft));
    d.set("m_maybeChangedDirectionAngle", dump(o.m_slideSlopeAngle));
    d.set("m_unkUnused2", dump(o.m_unkUnused2));
    d.set("m_stateNoAutoJump", dump(o.m_stateNoAutoJump));
    d.set("m_stateDartSlide", dump(o.m_stateDartSlide));
    d.set("m_stateHitHead", dump(o.m_stateHitHead));
    d.set("m_stateFlipGravity", dump(o.m_stateFlipGravity));
    d.set("m_stateForce", dump(o.m_stateForce));
    d.set("m_stateForceVector", dump(o.m_stateForceVector));
    d.set("m_affectedByForces", dump(o.m_affectedByForces));
    d.set("m_jumpPadRelated", dump(o.m_touchedForceIds));
    d.set("m_fallStartY", dump(o.m_fallStartY));
    return d;
}
#endif

void load(In& in, PulseEffectAction& o) {
    load(in, o.m_disabled);
    load(in, o.m_fadeInTime);
    load(in, o.m_holdTime);
    load(in, o.m_fadeOutTime);
    load(in, o.m_deltaTime);
    load(in, o.m_targetGroupID);
    load(in, o.m_currentValue);
    load(in, o.m_color);
    load(in, o.m_pulseEffectType);
    load(in, o.m_hsv);
    load(in, o.m_colorIndex);
    load(in, o.m_mainOnly);
    load(in, o.m_detailOnly);
    load(in, o.m_isDynamicHsv);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_startTime);
}

#ifdef SPC_SMOKE
matjson::Value dump(PulseEffectAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_fadeInTime", dump(o.m_fadeInTime));
    d.set("m_holdTime", dump(o.m_holdTime));
    d.set("m_fadeOutTime", dump(o.m_fadeOutTime));
    d.set("m_deltaTime", dump(o.m_deltaTime));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_currentValue", dump(o.m_currentValue));
    d.set("m_color", dump(o.m_color));
    d.set("m_pulseEffectType", dump(o.m_pulseEffectType));
    d.set("m_hsv", dump(o.m_hsv));
    d.set("m_colorIndex", dump(o.m_colorIndex));
    d.set("m_mainOnly", dump(o.m_mainOnly));
    d.set("m_detailOnly", dump(o.m_detailOnly));
    d.set("m_isDynamicHsv", dump(o.m_isDynamicHsv));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_startTime", dump(o.m_startTime));
    return d;
}
#endif

void load(In& in, SFXStateContainer& o) {
    load(in, o.m_startTime);
    load(in, o.m_duration);
    load(in, o.m_target);
    load(in, o.m_unusedFrom);
    load(in, o.m_active);
}

#ifdef SPC_SMOKE
matjson::Value dump(SFXStateContainer const& o) {
    auto d = matjson::Value::object();
    d.set("m_unkDouble1", dump(o.m_startTime));
    d.set("m_unkDouble2", dump(o.m_duration));
    d.set("m_unkFloat1", dump(o.m_target));
    d.set("m_unkFloat2", dump(o.m_unusedFrom));
    d.set("m_unkBool", dump(o.m_active));
    return d;
}
#endif

void load(In& in, SFXTriggerInstance& o) {
    load(in, o.m_groupID1);
    load(in, o.m_groupID2);
    load(in, o.m_controlID);
    load(in, o.m_sfxTriggerGameObject);
}

#ifdef SPC_SMOKE
matjson::Value dump(SFXTriggerInstance const& o) {
    auto d = matjson::Value::object();
    d.set("m_groupID1", dump(o.m_groupID1));
    d.set("m_groupID2", dump(o.m_groupID2));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_sfxTriggerGameObject", dump(o.m_sfxTriggerGameObject));
    return d;
}
#endif

void load(In& in, SFXTriggerState& o) {
    load(in, o.m_sfxTriggerGameObject);
    load(in, o.m_instanceID);
    load(in, o.m_startTime);
    load(in, o.m_playPosition);
    load(in, o.m_positionTime);
    load(in, o.m_lastUpdateTime);
    load(in, o.m_speed);
    load(in, o.m_groupSpeed);
    load(in, o.m_volume);
    load(in, o.m_length);
    load(in, o.m_index);
    load(in, o.m_processed);
    load(in, o.m_loop);
    load(in, o.m_sfxStateContainers[0]);
    load(in, o.m_sfxStateContainers[1]);
    load(in, o.m_sfxStateContainers[2]);
}

#ifdef SPC_SMOKE
matjson::Value dump(SFXTriggerState const& o) {
    auto d = matjson::Value::object();
    d.set("m_sfxTriggerGameObject", dump(o.m_sfxTriggerGameObject));
    d.set("m_unkInt1", dump(o.m_instanceID));
    d.set("m_unkDouble1", dump(o.m_startTime));
    d.set("m_unkDouble2", dump(o.m_playPosition));
    d.set("m_unkDouble3", dump(o.m_positionTime));
    d.set("m_unkDouble4", dump(o.m_lastUpdateTime));
    d.set("m_unkFloat1", dump(o.m_speed));
    d.set("m_unkFloat2", dump(o.m_groupSpeed));
    d.set("m_unkFloat3", dump(o.m_volume));
    d.set("m_unkFloat4", dump(o.m_length));
    d.set("m_unkInt2", dump(o.m_index));
    d.set("m_processed", dump(o.m_processed));
    d.set("m_unkBool1", dump(o.m_loop));
    d.set("m_sfxStateContainers[0]", dump(o.m_sfxStateContainers[0]));
    d.set("m_sfxStateContainers[1]", dump(o.m_sfxStateContainers[1]));
    d.set("m_sfxStateContainers[2]", dump(o.m_sfxStateContainers[2]));
    return d;
}
#endif

void load(In& in, SavedActiveObjectState& o) {
    load(in, o.m_gameObject);
    load(in, o.m_activatedByPlayer1);
    load(in, o.m_activatedByPlayer2);
}

#ifdef SPC_SMOKE
matjson::Value dump(SavedActiveObjectState const& o) {
    auto d = matjson::Value::object();
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_activatedByPlayer1", dump(o.m_activatedByPlayer1));
    d.set("m_activatedByPlayer2", dump(o.m_activatedByPlayer2));
    return d;
}
#endif

void load(In& in, SavedObjectStateRef& o) {
    load(in, o.m_gameObject);
    load(in, o.m_positionX);
    load(in, o.m_positionY);
    load(in, o.m_rotationXOffset);
    load(in, o.m_rotationYOffset);
    load(in, o.m_addToCustomScaleX);
    load(in, o.m_addToCustomScaleY);
    load(in, o.m_stepStartX);
    load(in, o.m_stepStartY);
}

#ifdef SPC_SMOKE
matjson::Value dump(SavedObjectStateRef const& o) {
    auto d = matjson::Value::object();
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_positionX", dump(o.m_positionX));
    d.set("m_positionY", dump(o.m_positionY));
    d.set("m_rotationXOffset", dump(o.m_rotationXOffset));
    d.set("m_rotationYOffset", dump(o.m_rotationYOffset));
    d.set("m_addToCustomScaleX", dump(o.m_addToCustomScaleX));
    d.set("m_addToCustomScaleY", dump(o.m_addToCustomScaleY));
    d.set("m_unkFloat3", dump(o.m_stepStartX));
    d.set("m_unkFloat4", dump(o.m_stepStartY));
    return d;
}
#endif

void load(In& in, SavedSpecialObjectState& o) {
    load(in, o.m_gameObject);
    load(in, o.m_animationID);
}

#ifdef SPC_SMOKE
matjson::Value dump(SavedSpecialObjectState const& o) {
    auto d = matjson::Value::object();
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_animationID", dump(o.m_animationID));
    return d;
}
#endif

void load(In& in, SequenceTriggerState& o) {
    load(in, o.m_sequenceTimes);
    load(in, o.m_sequenceIndices);
}

#ifdef SPC_SMOKE
matjson::Value dump(SequenceTriggerState const& o) {
    auto d = matjson::Value::object();
    d.set("m_sequenceTimes", dump(o.m_sequenceTimes));
    d.set("m_sequenceIndices", dump(o.m_sequenceIndices));
    return d;
}
#endif

void load(In& in, SongChannelState& o) {
    load(in, o.m_songTriggerGameObject1);
    load(in, o.m_startTime);
    load(in, o.m_songTriggerGameObject2);
    load(in, o.m_prepareTime);
}

#ifdef SPC_SMOKE
matjson::Value dump(SongChannelState const& o) {
    auto d = matjson::Value::object();
    d.set("m_songTriggerGameObject1", dump(o.m_songTriggerGameObject1));
    d.set("m_unkDouble1", dump(o.m_startTime));
    d.set("m_songTriggerGameObject2", dump(o.m_songTriggerGameObject2));
    d.set("m_unkDouble2", dump(o.m_prepareTime));
    return d;
}
#endif

void load(In& in, SongTriggerState& o) {
    load(in, o.m_songTriggerGameObject);
    load(in, o.m_startTime);
}

#ifdef SPC_SMOKE
matjson::Value dump(SongTriggerState const& o) {
    auto d = matjson::Value::object();
    d.set("m_songTriggerGameObject", dump(o.m_songTriggerGameObject));
    d.set("m_unkDouble", dump(o.m_startTime));
    return d;
}
#endif

void load(In& in, SoundStateContainer& o) {
    load(in, o.m_fadePointCount);
    load(in, o.m_fadePointVolumes[0]);
    load(in, o.m_fadePointVolumes[1]);
    load(in, o.m_fadePointVolumes[2]);
    load(in, o.m_fadePointVolumes[3]);
    load(in, o.m_fadePointOffsets[0]);
    load(in, o.m_fadePointOffsets[1]);
    load(in, o.m_fadePointOffsets[2]);
    load(in, o.m_fadePointOffsets[3]);
    load(in, o.m_currentOffset);
    load(in, o.m_loopStartMs);
    load(in, o.m_loopEndMs);
    load(in, o.m_currentMs);
    load(in, o.m_playStartOffset);
    load(in, o.m_playEndOffset);
    load(in, o.m_usePlayOffsets);
}

#ifdef SPC_SMOKE
matjson::Value dump(SoundStateContainer const& o) {
    auto d = matjson::Value::object();
    d.set("m_fadePointCount", dump(o.m_fadePointCount));
    d.set("m_fadePointVolumes[0]", dump(o.m_fadePointVolumes[0]));
    d.set("m_fadePointVolumes[1]", dump(o.m_fadePointVolumes[1]));
    d.set("m_fadePointVolumes[2]", dump(o.m_fadePointVolumes[2]));
    d.set("m_fadePointVolumes[3]", dump(o.m_fadePointVolumes[3]));
    d.set("m_fadePointOffsets[0]", dump(o.m_fadePointOffsets[0]));
    d.set("m_fadePointOffsets[1]", dump(o.m_fadePointOffsets[1]));
    d.set("m_fadePointOffsets[2]", dump(o.m_fadePointOffsets[2]));
    d.set("m_fadePointOffsets[3]", dump(o.m_fadePointOffsets[3]));
    d.set("m_currentOffset", dump(o.m_currentOffset));
    d.set("m_loopStartMs", dump(o.m_loopStartMs));
    d.set("m_loopEndMs", dump(o.m_loopEndMs));
    d.set("m_currentMs", dump(o.m_currentMs));
    d.set("m_playStartOffset", dump(o.m_playStartOffset));
    d.set("m_playEndOffset", dump(o.m_playEndOffset));
    d.set("m_usePlayOffsets", dump(o.m_usePlayOffsets));
    return d;
}
#endif

void load(In& in, SpawnTriggerAction& o) {
    load(in, o.m_finished);
    load(in, o.m_disabled);
    load(in, o.m_duration);
    load(in, o.m_deltaTime);
    load(in, o.m_targetGroupID);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_spawnOrdered);
    load(in, o.m_gameObject);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(SpawnTriggerAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_finished", dump(o.m_finished));
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_duration", dump(o.m_duration));
    d.set("m_deltaTime", dump(o.m_deltaTime));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_spawnOrdered", dump(o.m_spawnOrdered));
    d.set("m_gameObject", dump(o.m_gameObject));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, TimerItem& o) {
    load(in, o.m_itemID);
    load(in, o.m_time);
    load(in, o.m_active);
    load(in, o.m_timeMod);
    load(in, o.m_ignoreTimeWarp);
    load(in, o.m_targetTime);
    load(in, o.m_stopTimeEnabled);
    load(in, o.m_targetGroupID);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_remapKeys);
    load(in, o.m_disabled);
}

#ifdef SPC_SMOKE
matjson::Value dump(TimerItem const& o) {
    auto d = matjson::Value::object();
    d.set("m_itemID", dump(o.m_itemID));
    d.set("m_time", dump(o.m_time));
    d.set("m_active", dump(o.m_active));
    d.set("m_timeMod", dump(o.m_timeMod));
    d.set("m_ignoreTimeWarp", dump(o.m_ignoreTimeWarp));
    d.set("m_targetTime", dump(o.m_targetTime));
    d.set("m_stopTimeEnabled", dump(o.m_stopTimeEnabled));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    d.set("m_disabled", dump(o.m_disabled));
    return d;
}
#endif

void load(In& in, TimerTriggerAction& o) {
    load(in, o.m_disabled);
    load(in, o.m_time);
    load(in, o.m_targetTime);
    load(in, o.m_targetGroupID);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_itemID);
    load(in, o.m_multiActivate);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(TimerTriggerAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_time", dump(o.m_time));
    d.set("m_targetTime", dump(o.m_targetTime));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_itemID", dump(o.m_itemID));
    d.set("m_multiActivate", dump(o.m_multiActivate));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, ToggleTriggerAction& o) {
    load(in, o.m_disabled);
    load(in, o.m_targetGroupID);
    load(in, o.m_activateGroup);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(ToggleTriggerAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_activateGroup", dump(o.m_activateGroup));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, TouchToggleAction& o) {
    load(in, o.m_disabled);
    load(in, o.m_targetGroupID);
    load(in, o.m_holdMode);
    load(in, o.m_touchTriggerType);
    load(in, o.m_touchTriggerControl);
    load(in, o.m_triggerUniqueID);
    load(in, o.m_controlID);
    load(in, o.m_dualMode);
    load(in, o.m_remapKeys);
}

#ifdef SPC_SMOKE
matjson::Value dump(TouchToggleAction const& o) {
    auto d = matjson::Value::object();
    d.set("m_disabled", dump(o.m_disabled));
    d.set("m_targetGroupID", dump(o.m_targetGroupID));
    d.set("m_holdMode", dump(o.m_holdMode));
    d.set("m_touchTriggerType", dump(o.m_touchTriggerType));
    d.set("m_touchTriggerControl", dump(o.m_touchTriggerControl));
    d.set("m_triggerUniqueID", dump(o.m_triggerUniqueID));
    d.set("m_controlID", dump(o.m_controlID));
    d.set("m_dualMode", dump(o.m_dualMode));
    d.set("m_remapKeys", dump(o.m_remapKeys));
    return d;
}
#endif

void load(In& in, tk_spline& o) {
    load(in, o.m_x);
    load(in, o.m_y);
    load(in, o.m_b);
    load(in, o.m_c);
    load(in, o.m_d);
    load(in, o.m_c0);
    load(in, o.m_type);
    load(in, o.m_left);
    load(in, o.m_right);
    load(in, o.m_leftValue);
    load(in, o.m_rightValue);
    load(in, o.m_madeMonotonic);
}

#ifdef SPC_SMOKE
matjson::Value dump(tk_spline const& o) {
    auto d = matjson::Value::object();
    d.set("m_x", dump(o.m_x));
    d.set("m_y", dump(o.m_y));
    d.set("m_b", dump(o.m_b));
    d.set("m_c", dump(o.m_c));
    d.set("m_d", dump(o.m_d));
    d.set("m_c0", dump(o.m_c0));
    d.set("m_type", dump(o.m_type));
    d.set("m_left", dump(o.m_left));
    d.set("m_right", dump(o.m_right));
    d.set("m_leftValue", dump(o.m_leftValue));
    d.set("m_rightValue", dump(o.m_rightValue));
    d.set("m_madeMonotonic", dump(o.m_madeMonotonic));
    return d;
}
#endif

}  // namespace

// writeCheckpoint of GD 2.2081 (the mod's, through PersistenceAPI), read into this game's checkpoint.
CheckpointObject* readCheckpoint(std::span<uint8_t const> bytes, size_t& at, Objects const& objects) {
    In in{bytes, at, objects};
    // Autoreleased: if the reading stops halfway, it goes.
    auto cp = CheckpointObject::create();
    skipCCNode(in);
    auto hasP2 = in.get<uint8_t>(), hasGradients = in.get<uint8_t>();
    if (hasP2 > 1 || hasGradients > 1) throw std::runtime_error("not a checkpoint of GD 2.2081");
    load(in, cp->m_gameState);
    load(in, cp->m_shaderState);
    load(in, cp->m_audioState);
    // The checkpoint's destructor releases them.
    auto player = [&in](PlayerCheckpoint*& slot) {
        auto p = PlayerCheckpoint::create();
        p->retain();
        if (slot) slot->release();
        slot = p;
        load(in, *p);
    };
    player(cp->m_player1Checkpoint);
    if (hasP2) player(cp->m_player2Checkpoint);
    in.skip(4 + 4 + 4);  // m_unke78, m_unke7c, m_unke80: gone in 2.2091 (always -1, -1, 0)
    load(in, cp->m_ground2Invisible);
    load(in, cp->m_streakBlend);
    load(in, cp->m_uniqueID);
    load(in, cp->m_respawnID);
    load(in, cp->m_vectorSavedObjectStateRef);
    load(in, cp->m_vectorActiveSaveObjectState);
    load(in, cp->m_vectorSpecialSaveObjectState);
    load(in, cp->m_effectManagerState);
    if (hasGradients) cp->m_gradientTriggerObjectArray = loadArray<GradientTriggerObject>(in, cp->m_gradientTriggerObjectArray);
    in.skip(1);  // m_unk11e8: gone in 2.2091 (always 0)
    load(in, cp->m_sequenceTriggerStateUnorderedMap);
    load(in, cp->m_commandIndex);
    at = in.at;
    return cp;
}

#ifdef SPC_SMOKE
matjson::Value dumpCheckpoint(CheckpointObject* cp, Objects const& objects) {
    g_dumping = &objects;
    auto d = matjson::Value::object();
    d.set("hasP2", cp->m_player2Checkpoint != nullptr);
    d.set("hasGradients", cp->m_gradientTriggerObjectArray != nullptr);
    d.set("m_gameState", dump(cp->m_gameState));
    d.set("m_shaderState", dump(cp->m_shaderState));
    d.set("m_audioState", dump(cp->m_audioState));
    d.set("m_player1Checkpoint", dump(*cp->m_player1Checkpoint));
    if (cp->m_player2Checkpoint) d.set("m_player2Checkpoint", dump(*cp->m_player2Checkpoint));
    d.set("m_ground2Invisible", dump(cp->m_ground2Invisible));
    d.set("m_streakBlend", dump(cp->m_streakBlend));
    d.set("m_uniqueID", dump(cp->m_uniqueID));
    d.set("m_respawnID", dump(cp->m_respawnID));
    d.set("m_vectorSavedObjectStateRef", dump(cp->m_vectorSavedObjectStateRef));
    d.set("m_vectorActiveSaveObjectState", dump(cp->m_vectorActiveSaveObjectState));
    d.set("m_vectorSpecialSaveObjectState", dump(cp->m_vectorSpecialSaveObjectState));
    d.set("m_effectManagerState", dump(cp->m_effectManagerState));
    if (cp->m_gradientTriggerObjectArray) d.set("m_gradientTriggerObjectArray", dumpArray(cp->m_gradientTriggerObjectArray));
    d.set("m_sequenceTriggerStateUnorderedMap", dump(cp->m_sequenceTriggerStateUnorderedMap));
    d.set("m_commandIndex", dump(cp->m_commandIndex));
    g_dumping = nullptr;
    return d;
}
#endif

}  // namespace convert2081
