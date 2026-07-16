#include <algorithm>

#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "postprocess/lemmatizer.h"
#include "postprocess/lemmatizer_internal.h"

namespace suzume::postprocess {

using namespace lemmatizer_detail;

void Lemmatizer::lemmatizeAll(std::vector<core::Morpheme>& morphemes) const {
  for (size_t i = 0; i < morphemes.size(); ++i) {
    auto& morpheme = morphemes[i];
    // B45: Special fix for ない adjective + さ + そう pattern
    // The adjective candidate generator sets lemma to なさい, but correct is ない
    // なさそう = ない + さそう (looks like there isn't)
    if (morpheme.pos == core::PartOfSpeech::Adjective && morpheme.surface.find("なさそう") != std::string::npos &&
        morpheme.lemma == "なさい") {
      morpheme.lemma = "ない";
    }

    // Fix classical suru-verb lemma: 漢字2文字以上+す → 漢字+する (確認す → 確認する)
    // The verb_candidates sometimes returns classical form that needs conversion
    if (morpheme.pos == core::PartOfSpeech::Verb) {
      if (std::string suru = fixSuruClassical(morpheme.lemma); !suru.empty()) {
        morpheme.lemma = suru;
      }
    }

    // Normalize the classical negative auxiliary ず to its dictionary form ぬ.
    if (morpheme.pos == core::PartOfSpeech::Auxiliary && morpheme.surface == "ず" && morpheme.lemma == "ず") {
      morpheme.lemma = "ぬ";
    }

    // Fix tari-adjective adverb lemma: 颯爽と → 颯爽, 堂々と → 堂々
    // MeCab uses stem only as lemma for tari-adverbs
    if (morpheme.pos == core::PartOfSpeech::Adverb) {
      if (std::string tari_stem = fixTariAdverb(morpheme.surface); !tari_stem.empty()) {
        morpheme.lemma = tari_stem;
      }
    }

    // Fix potential verb (可能動詞) lemma: 泊まれる should have lemma=泊まれる, not 泊む
    if (morpheme.lemma != morpheme.surface) {
      if (std::string potential = fixPotentialVerb(morpheme); !potential.empty()) {
        morpheme.lemma = potential;
      }
    }

    // Fix compound verbs analyzed as ichidan but actually サ変/godan-sa
    // E.g., lemma=やりなおしる → やりなおす, lemma=対しる → 対する
    if (morpheme.pos == core::PartOfSpeech::Verb && morpheme.conj_type != dictionary::ConjugationType::GodanRa) {
      if (std::string shiru = fixShiru(morpheme.lemma); !shiru.empty()) {
        morpheme.lemma = shiru;
      }
    }

    // Fix special ra-row (ラ行特殊活用) verb lemma: ~いる → ~る
    // Verbs like ござる, いらっしゃる have renyokei ending in い (not り)
    // The inflection analyzer incorrectly reconstructs ~いる as base form
    // E.g., ござい → ございる (wrong) → ござる (correct)
    if (morpheme.pos == core::PartOfSpeech::Verb) {
      if (std::string ru_form = fixSpecialRaRowLemma(morpheme.lemma, dict_manager_); !ru_form.empty()) {
        morpheme.lemma = ru_form;
      }
    }

    // Preserve lemma if intentionally set (e.g., from verb_candidates for passive verbs)
    // Recalculate if:
    // 1. Lemma is empty, OR
    // 2. Lemma equals surface AND it's a conjugated form (not dictionary form)
    //    Dictionary forms end with: る, う, く, ぐ, す, つ, ぬ, ぶ, む (verbs), い (adjectives)
    bool needs_lemmatization = morpheme.lemma.empty();
    // A 終止形/連体形 てる-ending ichidan verb is its own dictionary form, so its lemma must be
    // the surface. When a candidate edge attached a mis-derived lemma — e.g. 立てる carrying
    // the ichidan base 立る from verb_candidates — the empty/equals-surface tests below leave
    // it untouched (neither empty nor equal), preserving the wrong lemma. Force
    // re-lemmatization so the てる gate in lemmatize() restores the surface; a genuine
    // dictionary lemma is still protected there by the is_from_dictionary short-circuit.
    if (!needs_lemmatization && morpheme.pos == core::PartOfSpeech::Verb && morpheme.lemma != morpheme.surface &&
        utf8::endsWith(morpheme.surface, "てる") &&
        (morpheme.extended_pos == core::ExtendedPOS::VerbShuushikei ||
         morpheme.extended_pos == core::ExtendedPOS::VerbRentaikei)) {
      needs_lemmatization = true;
    }
    if (!needs_lemmatization && morpheme.lemma == morpheme.surface) {
      if (morpheme.pos == core::PartOfSpeech::Verb) {
        // Check if surface looks like a dictionary form verb
        // Dictionary form verbs end with: る, う, く, ぐ, す, つ, ぬ, ぶ, む
        bool is_dict_form = utf8::endsWithAny(morpheme.surface, {"る", "う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む"});
        // If it's a dictionary form, lemma == surface is correct
        // If it's a conjugated form (て, た, ない, etc.), recalculate
        if (!is_dict_form) {
          needs_lemmatization = true;
        }
        // NOTE: Passive verbs ending in 〜れる (e.g., いわれる → いう, かかれる → かく)
        // are usually SPLIT by the tokenizer (読ま+れる), not kept as single tokens.
        // Single token 〜れる verbs are typically potential verbs (可能動詞) like:
        // 書ける, 泊まれる, 読める - these should keep lemma = surface.
        // So we DON'T mark 〜れる verbs for re-lemmatization here.
        // The earlier fix already sets lemma = surface for these patterns.
        // Causative forms need lemmatization
        // E.g., 勉強させる → 勉強する, 書かせる → 書く
        if (is_dict_form && utf8::endsWithAny(morpheme.surface, {"させる", "わせる", "かせる", "がせる", "たせる",
                                                                 "なせる", "ばせる", "ませる", "らせる"})) {
          needs_lemmatization = true;
        }
        // Suru-verb te-form + subsidiary verb patterns need lemmatization
        // E.g., 説明してもらう → 説明する, 勉強してくる → 勉強する
        if (is_dict_form && utf8::endsWithAny(morpheme.surface, {"してもらう", "してあげる", "してみる", "してくれる",
                                                                 "していく", "してくる", "しておく", "してしまう"})) {
          needs_lemmatization = true;
        }
        // Colloquial とく/どく contractions need lemmatization
        // E.g., 見とく → 見る, 読んどく → 読む, 書いとく → 書く
        if (is_dict_form && utf8::endsWithAny(morpheme.surface, {"とく", "んどく"})) {
          needs_lemmatization = true;
        }
        // Colloquial てる/でる contractions need lemmatization
        // E.g., 見てる → 見る, 読んでる → 読む, 買ってる → 買う
        if (is_dict_form && utf8::endsWithAny(morpheme.surface, {"てる", "でる", "ってる"})) {
          needs_lemmatization = true;
        }
        // Volitional form needs lemmatization
        // E.g., 始めよう → 始める, 食べよう → 食べる
        if (utf8::endsWith(morpheme.surface, "よう")) {
          needs_lemmatization = true;
        }
      } else if (morpheme.pos == core::PartOfSpeech::Adjective) {
        // Check if surface looks like a dictionary form adjective (ends with い)
        bool is_dict_form = utf8::endsWith(morpheme.surface, "い");
        if (!is_dict_form) {
          needs_lemmatization = true;
        }
      }
    }
    if (needs_lemmatization) {
      morpheme.lemma = lemmatize(morpheme);
    }
    // Get next morpheme for context-dependent fixes
    std::string_view next_surface;
    std::string_view next_lemma;
    if (i + 1 < morphemes.size()) {
      next_surface = morphemes[i + 1].surface;
      next_lemma = morphemes[i + 1].lemma;
    }
    // A one-kanji サ変 verb uses せ before the classical negative auxiliary:
    // 屈せ+ず, 達せ+ぬ. The generic unknown-verb candidate is necessarily
    // ambiguous with an Ichidan stem and can therefore carry the fabricated
    // lemma 屈せる. The following closed-class auxiliary supplies the missing
    // grammatical evidence. Multi-kanji サ変 expressions remain compositional
    // noun + せ (説明+せ+ず), so this correction is intentionally one-kanji only.
    if (morpheme.pos == core::PartOfSpeech::Verb && morpheme.extended_pos == core::ExtendedPOS::VerbRenyokei &&
        morpheme.surface.size() == core::kTwoJapaneseCharBytes && utf8::endsWith(morpheme.surface, "せ") &&
        i + 1 < morphemes.size() && morphemes[i + 1].extended_pos == core::ExtendedPOS::AuxNegativeNu) {
      std::string stem(utf8::dropLastChar(morpheme.surface));
      if (grammar::isAllKanji(stem)) {
        morpheme.lemma = stem + "する";
        morpheme.conj_type = dictionary::ConjugationType::Suru;
        morpheme.extended_pos = core::ExtendedPOS::VerbMizenkei;
      }
    }
    // Fix onbin lemma using next morpheme context
    // イ音便: 書い+た/て → lemma should be 書く (not 書う)
    // 連用形: 使い+ます/にくい → lemma should be 使う (correct)
    // Pattern: surface ends with い, lemma ends with う, next is た/て/だ/で
    if (morpheme.pos == core::PartOfSpeech::Verb && utf8::endsWith(morpheme.surface, "い") &&
        utf8::endsWith(morpheme.lemma, "う") && morpheme.lemma.size() >= core::kTwoJapaneseCharBytes &&
        utf8::equalsAny(next_surface, {"た", "て", "だ", "で"})) {
      // This is onbin form - fix lemma from 〜う to 〜く or 〜ぐ.
      // The tiebreak here is the following token's voicing (だ/で ⇒ ガ行), which is
      // deterministic and dictionary-free — do NOT route this through the dict-order
      // helper (getGodanTypesByOnbin is Ka-first and dict-gated): voicing correctly
      // handles out-of-dict verbs (凪いだ→凪ぐ) and voiced ties (ついだ→つぐ).
      std::string stem(utf8::dropLastChar(morpheme.lemma));
      // Check if next is voiced (だ/で) → 〜ぐ, otherwise → 〜く
      if (grammar::inflection::isValidKanjiIStemException(morpheme.surface)) {
        // Kami-ichidan renyokei (率い, 老い, ...) - dictionary form is surface + る
        morpheme.lemma = morpheme.surface + "る";
      } else if (utf8::equalsAny(next_surface, {"だ", "で"})) {
        morpheme.lemma = stem + "ぐ";  // GodanGa: 泳い+だ → 泳ぐ
      } else {
        morpheme.lemma = stem + "く";  // GodanKa: 書い+た → 書く
      }
    }
    // Fix ichidan renyokei misread as a godan base using next morpheme context
    // 連用形+て/た: 借り+て → lemma 借りる (not godan-ra 借る), 過ぎ+て → 過ぎる (not godan-ga 過ぐ)
    if (morpheme.pos == core::PartOfSpeech::Verb && morpheme.extended_pos == core::ExtendedPOS::VerbRenyokei) {
      if (std::string ichidan = fixIchidanRenyokeiBeforeTe(morpheme.surface, morpheme.lemma, next_surface);
          !ichidan.empty()) {
        morpheme.lemma = ichidan;
      }
    }
    // Fix irregular sokuonbin: いっ+た/て → いく (not いう)
    // いく is the only godan-ka verb that uses 促音便 instead of イ音便
    // Apply when preceded by the て particle (〜ていく construction: 出て+いっ+た),
    // a motion particle (に/へ), or adjective renyokei (〜く)
    // Do NOT apply after quotative markers (と/そう/こう etc.) where いっ = 言う
    if (morpheme.pos == core::PartOfSpeech::Verb && utf8::endsWith(morpheme.surface, "いっ") &&
        morpheme.lemma.size() >= core::kTwoJapaneseCharBytes && utf8::endsWith(morpheme.lemma, "いう") &&
        utf8::equalsAny(next_surface, {"た", "て", "たら", "ちゃ"}) && i > 0) {
      bool has_te_particle = morphemes[i - 1].surface == "て";
      bool has_motion_particle = utf8::equalsAny(morphemes[i - 1].surface, {"に", "へ"});
      bool has_adj_renyokei =
          morphemes[i - 1].pos == core::PartOfSpeech::Adjective && utf8::endsWith(morphemes[i - 1].surface, "く");
      if (has_te_particle || has_motion_particle || has_adj_renyokei) {
        std::string stem = morpheme.lemma.substr(0, morpheme.lemma.size() - core::kTwoJapaneseCharBytes);
        morpheme.lemma = stem + "いく";
      }
    }
    // Fix 仮定形 lemma: verb + ば → godan conditional, not ichidan potential
    // E.g., 書け+ば → lemma=書く (not 書ける), 行け+ば → lemma=行く (not 行ける)
    if (morpheme.pos == core::PartOfSpeech::Verb && utf8::equalsAny(next_surface, {"ば"}) &&
        morpheme.lemma.size() >= core::kTwoJapaneseCharBytes) {
      // Special case: なけれ+ば → lemma=ない
      if (morpheme.surface == "なけれ") {
        morpheme.lemma = "ない";
      } else if (utf8::endsWithAny(morpheme.surface, {"え", "け", "げ", "せ", "て", "ね", "べ", "め", "れ"})) {
        // Check if lemma looks like ichidan potential (ends with e-row + る)
        if (utf8::endsWithAny(morpheme.lemma,
                              {"える", "ける", "げる", "せる", "てる", "ねる", "べる", "める", "れる"})) {
          // Convert ichidan potential lemma to godan base
          // Remove trailing える/ける/... (6 bytes) and get the stem
          std::string stem(utf8::dropLast2Chars(morpheme.lemma));
          // Map e-row ending to godan base: え→う, け→く, げ→ぐ, etc.
          std::string_view surface_tail(morpheme.surface.data() + morpheme.surface.size() - core::kJapaneseCharBytes,
                                        core::kJapaneseCharBytes);
          // Map e-row ending to godan base (け→く, せ→す, ...) via the shared
          // Conjugation-derived table. れ is ambiguous and handled separately.
          char32_t tail_cp = utf8::decodeFirstChar(surface_tail);
          std::string godan_base;
          if (tail_cp == U'れ') {
            // Check if this is ichidan conditional (食べれ+ば → 食べる)
            // rather than godan-ra conditional (取れ+ば → 取る)
            // For ichidan: surface_stem + る == original lemma
            std::string surface_stem = morpheme.surface.substr(0, morpheme.surface.size() - core::kJapaneseCharBytes);
            if (surface_stem + "る" != morpheme.lemma) {
              godan_base = "る";
            }
            // else: ichidan conditional - lemma is already correct, don't convert
          } else {
            godan_base = std::string(grammar::godanBaseSuffixFromERow(tail_cp));
          }
          if (!godan_base.empty()) {
            morpheme.lemma = stem + godan_base;
          }
        } else if (utf8::endsWith(morpheme.surface, "れ") && utf8::endsWith(morpheme.lemma, "る") &&
                   morpheme.surface.size() >= core::kTwoJapaneseCharBytes) {
          // Ichidan conditional: 食べれ+ば → lemma=食べる
          // Surface = stem + れ, correct lemma = stem + る
          std::string stem = morpheme.surface.substr(0, morpheme.surface.size() - core::kJapaneseCharBytes);
          morpheme.lemma = stem + "る";
        }
      }
    }
    // Fix 命令形 ろ ending lemma: ichidan imperative
    // E.g., 見ろ → lemma=見る (not 見ろる), 寝ろ → lemma=寝る (not 寝ろる)
    if (morpheme.pos == core::PartOfSpeech::Verb && utf8::endsWith(morpheme.surface, "ろ")) {
      if (utf8::endsWith(morpheme.lemma, "ろる")) {
        // Lemma incorrectly derived as 〜ろる → fix to 〜る
        morpheme.lemma = morpheme.lemma.substr(0, morpheme.lemma.size() - core::kTwoJapaneseCharBytes) + "る";
      } else if (morpheme.lemma == morpheme.surface && morpheme.surface.size() >= core::kTwoJapaneseCharBytes) {
        // Lemma equals surface (e.g., 起きろ→起きろ) → fix to stem + る
        morpheme.lemma = morpheme.surface.substr(0, morpheme.surface.size() - core::kJapaneseCharBytes) + "る";
      }
    }
    // Fix たら lemma: should be た (not たら)
    if (morpheme.surface == "たら" && morpheme.lemma == "たら") {
      morpheme.lemma = "た";
    }
    morpheme.conj_form = detectConjForm(morpheme.surface, morpheme.lemma, morpheme.pos, next_lemma);
  }
}

}  // namespace suzume::postprocess
