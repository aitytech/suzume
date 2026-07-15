#include "entries_internal.h"

namespace suzume::dictionary::entries {

std::vector<DictionaryEntry> getInterjectionEntries() {
  return {
      // Common interjections (exclamations)
      intj("えっ"),    // Surprise
      intj("ええ"),    // Affirmation/Surprise
      intj("あっ"),    // Realization
      intj("ああ"),    // Agreement/Sigh
      intj("おお"),    // Amazement
      intj("うわ"),    // Surprise
      intj("うわっ"),  // Surprise (emphatic)
      intj("わあ"),    // Amazement
      intj("へえ"),    // Interest
      intj("ふーん"),  // Understanding/Disinterest
      intj("ふうん"),  // Understanding
      // Note: ほう removed - formal noun usage (ほうがいい) is more common
      intj("おい"),    // Calling attention
      intj("おーい"),  // Calling from afar
      intj("あれ"),    // Confusion
      intj("あれっ"),  // Confusion (emphatic)
      intj("まあ"),    // Surprise/Moderation
      intj("さあ"),    // Prompting/Urging
      intj("ねえ"),    // Attention-getting (also particle, but standalone usage)
      // Responses
      intj("はい"),    // Yes
      intj("いいえ"),  // No
      intj("うん"),    // Casual yes
      intj("ううん"),  // Casual no
      // Hesitation/Filler
      intj("えーと"),  // Hesitation
      intj("えっと"),  // Hesitation
      intj("ええと"),  // Hesitation
      intj("あの"),    // Hesitation (also determiner)
      intj("その"),    // Hesitation (rare, also determiner)
  };
}

}  // namespace suzume::dictionary::entries
