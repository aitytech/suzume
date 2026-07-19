#ifndef SUZUME_ANALYSIS_JOIN_COMPOUND_VERB_INTERNAL_H_
#define SUZUME_ANALYSIS_JOIN_COMPOUND_VERB_INTERNAL_H_

#include "bigram_table.h"
#include "candidate_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/inflection.h"
#include "join_candidates.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "tokenizer_utils.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis::compound_verb_detail {

using CharType = normalize::CharType;

// V2 Subsidiary verbs for compound verb joining
// Verb type determines how to generate renyokei from base form
enum class V2VerbType : uint8_t {
  Godan,    // 五段: 込む→込み, 返す→返し (replace ending with i-row)
  Ichidan,  // 一段: 続ける→続け, 上げる→上げ (drop る)
};

// The compound inherits V2's inflection class. Retaining this on generated
// edges prevents later lemmatization from treating a GodanSa compound such as
// 見直す as the classical suru form 見直する.
dictionary::ConjugationType compoundConjugationType(V2VerbType verb_type, std::string_view base_ending);

struct SubsidiaryVerb {
  const char* surface;      // Kanji form (or hiragana if no kanji)
  const char* reading;      // Hiragana reading (nullptr if same as surface)
  const char* base_ending;  // Base form ending for verb type detection
  V2VerbType verb_type;     // Determines renyokei generation
};

// List of V2 verbs that can form compound verbs
// Renyokei forms are generated automatically from base forms
// This is intentionally a closed lexical allowlist, not a generic "any verb can
// be V2" rule. Japanese compound-verb productivity is high, but unrestricted
// generation over-splits ordinary kanji+verb sequences and creates many false
// positives. Keep this table synchronized with tokenization tests when adding
// new V2 verbs.
// 始める・過ぎる・終わる／終える are aspectual auxiliaries and remain
// separate search units. 続ける is intentionally retained because productive
// V1+続ける compounds form the search unit represented by this table.
inline constexpr SubsidiaryVerb kSubsidiaryVerbs[] = {
    // Godan verbs (五段)
    {"込む", "こむ", "む", V2VerbType::Godan},      // 読み込む, 飛びこむ
    {"出す", "だす", "す", V2VerbType::Godan},      // 呼び出す, 走りだす
    {"続く", "つづく", "く", V2VerbType::Godan},    // 引き続く
    {"返す", "かえす", "す", V2VerbType::Godan},    // 繰り返す, 繰りかえす
    {"戻す", "もどす", "す", V2VerbType::Godan},    // 取り戻す, 取りもどす
    {"返る", "かえる", "る", V2VerbType::Godan},    // 振り返る, 振りかえる
    {"変わる", "かわる", "る", V2VerbType::Godan},  // 移り変わる, 生まれ変わる
    {"替わる", "かわる", "る", V2VerbType::Godan},  // 入れ替わる, 切り替わる
    {"つかる", nullptr, "る", V2VerbType::Godan},   // 見つかる
    {"合う", "あう", "う", V2VerbType::Godan},      // 話し合う, 話しあう
    {"扱う", "あつかう", "う", V2VerbType::Godan},  // 取り扱う
    {"運ぶ", "はこぶ", "ぶ", V2VerbType::Godan},    // 持ち運ぶ
    {"過ごす", "すごす", "す", V2VerbType::Godan},  // 見過ごす
    {"消す", "けす", "す", V2VerbType::Godan},      // 取り消す
    {"直す", "なおす", "す", V2VerbType::Godan},    // やり直す, やりなおす
    {"切る", "きる", "る", V2VerbType::Godan},      // 締め切る, 締めきる
    {"上がる", "あがる", "る", V2VerbType::Godan},  // 立ち上がる, 盛り上がる
    {"下がる", "さがる", "る", V2VerbType::Godan},  // 立ち下がる
    {"回す", "まわす", "す", V2VerbType::Godan},    // 振り回す, 持ち回す
    {"回る", "まわる", "る", V2VerbType::Godan},    // 持ち回る, 振り回る
    {"抜く", "ぬく", "く", V2VerbType::Godan},      // 追い抜く, 突き抜く
    {"掛かる", "かかる", "る", V2VerbType::Godan},  // 取り掛かる
    {"付く", "つく", "く", V2VerbType::Godan},      // 思い付く, 気付く
    {"当たる", "あたる", "る", V2VerbType::Godan},  // 見当たる, 行き当たる
    {"巡る", "めぐる", "る", V2VerbType::Godan},    // 駆け巡る, 飛び巡る
    {"飛ばす", "とばす", "す", V2VerbType::Godan},  // 吹き飛ばす, 弾き飛ばす
    {"交う", "かう", "う", V2VerbType::Godan},      // 飛び交う, 行き交う
    {"潰す", "つぶす", "す", V2VerbType::Godan},    // 押し潰す, 叩き潰す
    {"崩す", "くずす", "す", V2VerbType::Godan},    // 切り崩す, 打ち崩す
    {"倒す", "たおす", "す", V2VerbType::Godan},    // 打ち倒す, 蹴り倒す
    {"起こす", "おこす", "す", V2VerbType::Godan},  // 引き起こす, 呼び起こす
    {"去る", "さる", "る", V2VerbType::Godan},      // 立ち去る, 走り去る
    {"開く", "ひらく", "く", V2VerbType::Godan},    // 切り開く, 押し開く
    {"組む", "くむ", "む", V2VerbType::Godan},      // 取り組む, 組み組む
    {"上る", "のぼる", "る", V2VerbType::Godan},    // 立ち上る, 這い上る
    {"こもる", "こもる", "る", V2VerbType::Godan},  // 閉じこもる, 立てこもる, 引きこもる
    // Ichidan verbs (一段)
    {"続ける", "つづける", "ける", V2VerbType::Ichidan},    // 読み続ける, 読みつづける
    {"つける", nullptr, "ける", V2VerbType::Ichidan},       // 見つける
    {"替える", "かえる", "える", V2VerbType::Ichidan},      // 切り替える
    {"換える", "かえる", "える", V2VerbType::Ichidan},      // 入れ換える
    {"合わせる", "あわせる", "せる", V2VerbType::Ichidan},  // 組み合わせる
    {"浮かべる", "うかべる", "べる", V2VerbType::Ichidan},  // 思い浮かべる
    {"切れる", "きれる", "れる", V2VerbType::Ichidan},      // 使い切れる
    {"出る", "でる", "る", V2VerbType::Ichidan},            // 飛び出る
    {"上げる", "あげる", "げる", V2VerbType::Ichidan},      // 売り上げる, 取り上げる
    {"下げる", "さげる", "げる", V2VerbType::Ichidan},      // 引き下げる
    {"抜ける", "ぬける", "ける", V2VerbType::Ichidan},      // 突き抜ける
    {"着く", "つく", "く", V2VerbType::Godan},              // 落ち着く, たどり着く
    {"取る", "とる", "る", V2VerbType::Godan},              // 搾り取る, 掠め取る
    {"越す", "こす", "す", V2VerbType::Godan},              // 引っ越す, 追い越す
    {"越える", "こえる", "える", V2VerbType::Ichidan},      // 乗り越える, 飛び越える
    {"張る", "はる", "る", V2VerbType::Godan},              // 引っ張る, 頑張る
    {"叫ぶ", "さけぶ", "ぶ", V2VerbType::Godan},            // 泣き叫ぶ, 喚き叫ぶ
    {"注ぐ", "そそぐ", "ぐ", V2VerbType::Godan},            // 降り注ぐ, 流し注ぐ
    {"継ぐ", "つぐ", "ぐ", V2VerbType::Godan},              // 語り継ぐ, 受け継ぐ, 引き継ぐ
    {"刺す", "さす", "す", V2VerbType::Godan},              // 突き刺す, 差し刺す
    {"望む", "のぞむ", "む", V2VerbType::Godan},            // 待ち望む, 見望む
    {"落とす", "おとす", "す", V2VerbType::Godan},          // 切り落とす, 打ち落とす
    {"落ちる", "おちる", "ちる", V2VerbType::Ichidan},      // 転げ落ちる
    {"掛ける", "かける", "ける", V2VerbType::Ichidan},      // 呼び掛ける, 働き掛ける
    {"付ける", "つける", "ける", V2VerbType::Ichidan},      // 押し付ける, 決め付ける
    {"入れる", "いれる", "れる", V2VerbType::Ichidan},      // 取り入れる, 持ち入れる
    {"分ける", "わける", "ける", V2VerbType::Ichidan},      // 切り分ける, 振り分ける
    {"立てる", "たてる", "てる", V2VerbType::Ichidan},      // 組み立てる, 打ち立てる
    {"重ねる", "かさねる", "ねる", V2VerbType::Ichidan},    // 積み重ねる, 折り重ねる
    {"広げる", "ひろげる", "げる", V2VerbType::Ichidan},    // 繰り広げる, 押し広げる
    {"支える", "ささえる", "える", V2VerbType::Ichidan},    // 差し支える
    {"受ける", "うける", "ける", V2VerbType::Ichidan},      // 引き受ける, 請け受ける
    {"降りる", "おりる", "りる", V2VerbType::Ichidan},      // 乗り降りる
    {"締める", "しめる", "める", V2VerbType::Ichidan},      // 抱きしめる, 締め締める
    {"止める", "とめる", "める", V2VerbType::Ichidan},      // 受け止める, 食い止める
    {"入る", "いる", "る", V2VerbType::Godan},              // 飛び入る, 立ち入る
    {"止まる", "とまる", "る", V2VerbType::Godan},          // 立ち止まる, 踏み止まる, 思い止まる
};

// Sokuonbin-compatible godan verb endings (く, つ, う, る)
// Used to try all possible base forms when analyzing っ-onbin compound verbs
inline constexpr char32_t kSokuonbinEndings[] = {U'く', U'つ', U'う', U'る'};

// Generate renyokei surface from base form
// Godan: replace ending with i-row (込む→込み, 返す→返し)
// Ichidan: drop る (続ける→続け)
std::string generateRenyokei(std::string_view surface, std::string_view reading, V2VerbType verb_type);

// Generate mizenkei surface from base form
// Godan: replace ending with a-row (込む→込ま, 返す→返さ)
// Ichidan: drop る (続ける→続け, same as renyokei for ichidan)
std::string generateMizenkei(std::string_view surface, std::string_view reading, V2VerbType verb_type);

// Generate a Godan potential form (戻す→戻せる) from the dictionary form.
// The V2 allowlist controls which lexical verbs participate; this merely shares
// their ordinary Godan conjugation across all of those entries.
std::string generateGodanPotential(std::string_view surface, std::string_view reading, V2VerbType verb_type);

// Te-form euphonic form type for Godan verbs
enum class TeFormType : uint8_t {
  Ionbin,      // イ音便 (書く→書い, 泳ぐ→泳い) + て/で
  Sokuonbin,   // 促音便 (待つ→待っ, 買う→買っ, 帰る→帰っ) + て
  Hatsuonbin,  // 撥音便 (読む→読ん, 飛ぶ→飛ん, 死ぬ→死ん) + で
  Renyokei,    // 連用形 (話す→話し) + て
  Ichidan,     // 一段 (食べる→食べ) + て
};

// Determine te-form type and suffix from verb ending
TeFormType getTeFormType(std::string_view base_ending);

// Generate te-form euphonic stem (before て/で)
// Returns: stem and whether it uses で (vs て)
std::pair<std::string, bool> generateTeFormStem(std::string_view surface, std::string_view reading,
                                                V2VerbType verb_type, std::string_view base_ending);

// Generate kanji renyokei from kanji surface
std::string generateKanjiRenyokei(std::string_view kanji_surface, std::string_view reading, V2VerbType verb_type);

// Map a Godan i-row 連用形 codepoint (き, ぎ, し, ...) to its dictionary-form
// codepoint (く, ぐ, す, ...); returns 0 when the char is not a Godan renyokei
// ending. Backed by the shared Conjugation-derived table in grammar so the
// い段→終止形 mapping lives in exactly one place.
char32_t godanRenyokeiBaseCp(char32_t renyokei_cp);

// Cost bonuses imported from candidate_constants.h:
// candidate::kCompoundVerbBonus, candidate::kVerifiedV1Bonus

}  // namespace suzume::analysis::compound_verb_detail

#endif  // SUZUME_ANALYSIS_JOIN_COMPOUND_VERB_INTERNAL_H_
