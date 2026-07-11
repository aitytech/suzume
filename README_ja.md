# Suzume

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/suzume/ci.yml?branch=main&label=CI)](https://github.com/libraz/suzume/actions)
[![npm](https://img.shields.io/npm/v/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![PyPI](https://img.shields.io/pypi/v/suzume)](https://pypi.org/project/suzume/)
[![codecov](https://codecov.io/gh/libraz/suzume/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/suzume)
[![License](https://img.shields.io/github/license/libraz/suzume)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)

WebAssemblyでブラウザ上で動作する軽量な日本語トークナイザー。大規模辞書の代わりに特徴量ベースの解析を行います。

[ドキュメント](https://suzume.libraz.net/ja/) | [デモ](https://suzume.libraz.net/ja/#demo)

## 概要

Suzumeは、従来の辞書ベースの形態素解析器が使う大規模辞書（20〜50MB超）の代わりに、文字パターン・接続規則・小規模辞書で日本語テキストをトークン化します。WASMビルドはgzip圧縮で約424KBです。

| | 従来の形態素解析器 | Suzume |
|---|---|---|
| **バンドルサイズ** | 20〜50MB超（辞書） | 450KB未満（gzip） |
| **ブラウザ対応** | 限定的または非対応 | 対応（WASM） |
| **サーバー必須** | 通常は必要 | 不要 |
| **品詞タグ** | あり | あり |
| **原形復元** | あり | あり |

### トレードオフ

- **軽量** — 大規模辞書のダウンロードが不要。フロントエンド、エッジ、サーバーレス環境に適する
- **未知語に対応** — 特徴量ベースのため、辞書にない単語でも解析が破綻しにくい
- **精度の限界** — 専門用語や複雑な言語解析では、従来の辞書ベース解析器の方が高精度

## インストール

JavaScript / TypeScript（WebAssemblyでブラウザ・Node.js対応）:

```bash
npm install @libraz/suzume
# または: yarn add / pnpm add / bun add @libraz/suzume
```

Python（共有ライブラリ同梱のネイティブ実装。追加依存なし）:

```bash
pip install suzume
```

## クイックスタート

### JavaScript / TypeScript

```typescript
import { Suzume } from '@libraz/suzume'

const suzume = await Suzume.create()

const tokens = suzume.analyze('すもももももももものうち')
for (const t of tokens) {
  console.log(`${t.surface} [${t.posJa}]`)
}

// タグ抽出（{ tag, pos } オブジェクトの配列を返す）
const tags = suzume.generateTags('東京スカイツリーに行きました')
// → [{ tag: '東京', pos: 'noun' }, { tag: 'スカイツリー', pos: 'noun' }, { tag: '行く', pos: 'verb' }]

// 名詞のみ
suzume.generateTags('美味しいラーメンを食べた', { pos: ['noun'] })
// → [{ tag: 'ラーメン', pos: 'noun' }]

// 基本語除外（する、ある、いい などひらがなのみの原形を除外）
suzume.generateTags('今日はいい天気ですね', { excludeBasic: true })
// → [{ tag: '今日', pos: 'noun' }, { tag: '天気', pos: 'noun' }]
```

### Python

```python
from suzume import Suzume

with Suzume() as sz:
    for m in sz.analyze('すもももももももものうち'):
        print(m.surface, m.pos_ja)

    # タグ抽出（.text / .pos を持つ Tag オブジェクトを返す）
    tags = sz.generate_tags('東京に行きました')
    print([(t.text, t.pos) for t in tags])
```

Python APIの詳細は [bindings/python/README.md](bindings/python/README.md) を参照してください。

### ブラウザ（CDN）

```html
<script type="module">
  import { Suzume } from 'https://esm.sh/@libraz/suzume'

  const suzume = await Suzume.create()
  console.log(suzume.analyze('こんにちは'))
</script>
```

### C++

```cpp
#include "suzume.h"

suzume::Suzume tokenizer;
auto tokens = tokenizer.analyze("東京に行きました");

for (const auto& t : tokens) {
    std::cout << t.surface << "\t" << t.lemma << std::endl;
}
```

ソースからビルド（C++17、CMake 3.15+が必要）:

```bash
make          # ビルド
make test     # テスト実行
```

## ドキュメント

- [はじめる](https://suzume.libraz.net/ja/docs/getting-started) — インストールと基本的な使い方
- [API リファレンス](https://suzume.libraz.net/ja/docs/api) — APIドキュメント
- [ユーザー辞書](https://suzume.libraz.net/ja/docs/user-dictionary) — カスタム単語の追加
- [仕組み](https://suzume.libraz.net/ja/docs/how-it-works) — 技術的な解説

## ライセンス

[Apache License 2.0](LICENSE)
