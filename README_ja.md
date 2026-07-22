# Suzume

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/suzume/ci.yml?branch=main&label=CI)](https://github.com/libraz/suzume/actions)
[![npm](https://img.shields.io/npm/v/@libraz/suzume)](https://www.npmjs.com/package/@libraz/suzume)
[![PyPI](https://img.shields.io/pypi/v/suzume)](https://pypi.org/project/suzume/)
[![codecov](https://codecov.io/gh/libraz/suzume/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/suzume)
[![License](https://img.shields.io/github/license/libraz/suzume)](https://github.com/libraz/suzume/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Browser%20%7C%20Node.js%20%7C%20Python%20%7C%20C%2B%2B-lightgrey)](https://github.com/libraz/suzume)
[![Docs](https://img.shields.io/badge/docs-suzume.libraz.net-2563eb)](https://suzume.libraz.net/ja/)

**Suzumeは、ブラウザからネイティブアプリまで使える軽量な日本語トークナイザーです。** MeCabのような形態素解析器ではなく、検索・表示・テキスト処理で使いやすい単位への分割を目的としています。単語境界だけを返す軽量トークナイザーとは異なり、品詞付与と原形復元にも対応します。

**こんなときに使えます:**

- **どこでも日本語をトークン化したい** — ブラウザ、サーバーレス、Python、C/C++で同じトークナイザーを使えます。
- **検索向けの語を取り出したい** — 品詞フィルタ、原形、重複除去を使ってキーワードタグを生成できます。
- **独自の語彙を追加したい** — アプリケーション固有の語をユーザー辞書から読み込めます。

📖 **[ドキュメント](https://suzume.libraz.net/ja/)** &nbsp;·&nbsp; 🧪 **[デモ](https://suzume.libraz.net/ja/#demo)** &nbsp;·&nbsp; **[はじめる](https://suzume.libraz.net/ja/docs/getting-started)**

## MeCabとの違い

MeCabは、辞書に基づく詳細な分析を目的とする形態素解析器です。Suzumeは、実用的な分割単位を重視するトークナイザーです。文字パターンとコンパクトな規則を使い、検索単位として扱いやすい複合語や数量をまとめます。

```text
入力:   経済成長     3人
MeCab:  経済 / 成長  3 / 人
Suzume: 経済成長     3人
```

Suzumeは品詞付与と原形復元も行うため、活用した動詞・形容詞を検索用に正規化できます。ただし目的が異なるため、出力はMeCabと意図的に一致しません。具体例、トレードオフ、既知の制約は[MeCabとの違い](https://suzume.libraz.net/ja/docs/mecab-comparison)を参照してください。

## インストール

```bash
npm install @libraz/suzume  # JavaScript / TypeScript
pip install suzume          # Python
```

C/C++のインストール、ネイティブビルド、ユーザー辞書、すべての実行時オプションは[ドキュメント](https://suzume.libraz.net/ja/docs/getting-started)を参照してください。

## クイックスタート

```typescript
import { Suzume } from '@libraz/suzume'

const suzume = await Suzume.create()
const tokens = suzume.analyze('すもももももももものうち')
const tags = suzume.generateTags('東京の公園に行きました')

suzume.destroy() // 必要なら即時にリソースを解放
```

```python
from suzume import Suzume

with Suzume() as sz:
    tokens = sz.analyze("すもももももももものうち")
    tags = sz.generate_tags("東京の公園に行きました")
```

## ドキュメント

- [はじめる](https://suzume.libraz.net/ja/docs/getting-started)
- [JavaScript / TypeScript API](https://suzume.libraz.net/ja/docs/api)
- [Python API](https://suzume.libraz.net/ja/docs/python)
- [C / C++ ライブラリ](https://suzume.libraz.net/ja/docs/cpp)
- [ユーザー辞書](https://suzume.libraz.net/ja/docs/user-dictionary)

## ライセンス

[Apache License 2.0](LICENSE)
