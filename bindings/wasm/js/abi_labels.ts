// Numeric C ABI labels live in JS so the WASM does not carry formatting
// switches and duplicate English/Japanese strings for every morpheme.

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

const EXTENDED_POS = [
  'UNKNOWN',
  'VERB_終止',
  'VERB_連用',
  'VERB_未然',
  'VERB_音便',
  'VERB_て形',
  'VERB_仮定',
  'VERB_命令',
  'VERB_連体',
  'VERB_た形',
  'VERB_たら形',
  'ADJ_終止',
  'ADJ_連用',
  'ADJ_語幹',
  'ADJ_かっ',
  'ADJ_け形',
  'ADJ_NA',
  'AUX_過去',
  'AUX_丁寧',
  'AUX_否定',
  'AUX_否定古',
  'AUX_願望',
  'AUX_意志',
  'AUX_受身',
  'AUX_使役',
  'AUX_可能',
  'AUX_継続',
  'AUX_完了',
  'AUX_準備',
  'AUX_試行',
  'AUX_進行',
  'AUX_接近',
  'AUX_様態',
  'AUX_推定',
  'AUX_みたい',
  'AUX_断定',
  'AUX_丁寧断定',
  'AUX_尊敬',
  'AUX_丁重',
  'AUX_過度',
  'AUX_ガル',
  'PART_格',
  'PART_係',
  'PART_終',
  'PART_接続',
  'PART_引用',
  'PART_副',
  'PART_準体',
  'PART_係結',
  'NOUN',
  'NOUN_形式',
  'NOUN_転成',
  'NOUN_固有',
  'NOUN_姓',
  'NOUN_名',
  'NOUN_数',
  'PRON',
  'PRON_疑問',
  'ADV',
  'ADV_引用',
  'CONJ',
  'DET',
  'PREFIX',
  'SUFFIX',
  'SYMBOL',
  'INTJ',
  'OTHER',
  'ADJ_未然',
  'AUX_打消推量',
  'AUX_文語断定',
  'AUX_文語過去',
  'AUX_文語断定連体',
  'AUX_文語当為',
] as const;

const CONJUGATION_TYPE_JAPANESE: readonly (string | null)[] = [
  null,
  '一段',
  '五段・カ行',
  '五段・ガ行',
  '五段・サ行',
  '五段・タ行',
  '五段・ナ行',
  '五段・バ行',
  '五段・マ行',
  '五段・ラ行',
  '五段・ワ行',
  'サ変',
  'カ変',
  '形容詞',
];

const CONJUGATION_FORM_JAPANESE = [
  '終止形',
  '未然形',
  '連用形',
  '連用形',
  '仮定形',
  '命令形',
  '意志形',
] as const;

export const MORPHEME_FLAG = {
  userDict: 1 << 0,
  formalNoun: 1 << 1,
  lowInfo: 1 << 2,
  unknown: 1 << 3,
  fromDictionary: 1 << 4,
} as const;

export function posEnglish(code: number): string {
  return POS_ENGLISH[code] ?? 'OTHER';
}

export function posJapanese(code: number): string {
  return POS_JAPANESE[code] ?? 'その他';
}

export function extendedPosLabel(code: number): string {
  return EXTENDED_POS[code] ?? 'UNKNOWN';
}

export function conjugationTypeJapanese(code: number): string | null {
  return CONJUGATION_TYPE_JAPANESE[code] ?? null;
}

export function conjugationFormJapanese(code: number): string | null {
  return CONJUGATION_FORM_JAPANESE[code] ?? null;
}
