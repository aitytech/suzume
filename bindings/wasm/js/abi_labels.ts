// POS presentation labels remain binding-local because the C ABI exposes only
// the stable English POS label. Extended POS and conjugation labels are decoded
// through the canonical C functions.

const POS_ENGLISH = [
  'OTHER',
  'NOUN',
  'VERB',
  'ADJ',
  'ADV',
  'PARTICLE',
  'AUX',
  'CONJ',
  'DET',
  'PRON',
  'PREFIX',
  'SUFFIX',
  'INTJ',
  'SYMBOL',
  'OTHER',
] as const;

const POS_JAPANESE = [
  'その他',
  '名詞',
  '動詞',
  '形容詞',
  '副詞',
  '助詞',
  '助動詞',
  '接続詞',
  '連体詞',
  '代名詞',
  '接頭辞',
  '接尾辞',
  '感動詞',
  '記号',
  'その他',
] as const;

export const MORPHEME_FLAG = {
  userDict: 1 << 0,
  formalNoun: 1 << 1,
  lowInfo: 1 << 2,
  unknown: 1 << 3,
  fromDictionary: 1 << 4,
  conjugatable: 1 << 5,
} as const;

export function posEnglish(code: number): string {
  return POS_ENGLISH[code] ?? 'OTHER';
}

export function posJapanese(code: number): string {
  return POS_JAPANESE[code] ?? 'その他';
}
