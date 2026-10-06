# Translating Lamium

Lamium's UI ships in English (default), Japanese and Simplified Chinese
(`zh_CN`). The text follows the game language; any other language shows
English. Traditional Chinese is not offered until it is translated and
reviewed on its own (it is not generated from Simplified Chinese).

## Where the text lives

- `src/ui/Translations.h`: every key with its English and Japanese text.
- `src/ui/TranslationsZhCN.h`: Simplified Chinese, one row per key in the
  same order as `Translations.h`. The build fails if a key is missing or out
  of order.

## Correcting the Chinese text

The first Simplified Chinese text was drafted by a coding agent and has not
been reviewed by a native speaker yet. Corrections are welcome, especially
Minecraft and mod terminology. Send them as a pull request that edits
`TranslationsZhCN.h`:

- Change only the text, never the key or the row order.
- Keep every placeholder (`{}`, `{:.1f}`, ...) in its original order.
- Settings labels use `Name: {}` with an ASCII colon and space; the settings
  screen splits the name from the value there. Hotkey names keep the
  `Lamium: ` prefix.
- Prefer the terms the game itself uses in `zh_CN` (for example 物品栏, 潜影盒,
  收纳袋, 鞘翅, 区块).

If you would rather not open a pull request, open an issue with the
language, where the text appears, the current text and your wording
([Contributing](../CONTRIBUTING.md)).

Run `xmake build LamiumTests && xmake run LamiumTests` if you can; the tests
check placeholders and that every locale is complete. A screenshot from the
game helps when a line is too long for its column.

## Adding a key

Add the key with English and Japanese to `Translations.h`, then add the
Chinese row at the same position in `TranslationsZhCN.h`.
