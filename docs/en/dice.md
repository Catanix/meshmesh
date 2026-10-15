# Dice (the DIC3R dice roller) and the character sheet

Dice for tabletop games: the DIC3R app brought into MeshMesh. Three modes: **RPG** (a dice pool and
formulas), **Warhammer** (a grid of dice with a success threshold) and the **character sheet** (since 0.25.0,
in place of the separate counters). The device keeps it all, so the screen, the web page and the Android app
show the same: a roll from the phone appears on the board's screen at once and the other way round. The
hardware random number generator rolls the dice, without modulo bias. Rolls are not sent over the radio; a
sheet can be sent to a contact (below).

On every board: a "Dice" tile on the M9 and T-Deck, a screen after "Pet" on the one-button boards, the GAT562
and the T114. It works in every role, the repeater and room server included.

Code: `src/Dice.cpp`, `include/Dice.h` (rolls, formulas, the sheet, templates, storage, sending, the `dice`
command), `src/UiDice.inc` (the 320×240 screen), `src/UiDiceCompact.inc` (the 128×64 OLED and the menus of the
one-button boards), the page in `src/UiHires.inc` (T114), the shapes of dice and stats — `drawDieShape`,
`drawStatShape` in `src/UiIcons.cpp`. The state is `/meshmesh/dice.bin` in LittleFS (mode, pool, grid, the
characters' names); each character's sheet is its own file `/meshmesh/sheetN.bin` (only the blocks in use and
their descriptions, with a checksum); the current character's sheet is in memory. The last eight rolls and the
grid values live in memory only.

## Modes

**RPG.** The pool: the number of dice (1–99), the die (d4, d6, d8, d10, d12, d20, d66, d%) and a modifier
(−99…+99), for example `2d6+1`. Any formula: terms joined by `+` and `−`, each `NdX` (N up to 99, X from 2 to
1000), `dX`, `d%`, `d66`, `rN` (the R.E.T. die of level N, below) or a number, up to eight terms: `3d8+2d4-1`,
`d20+5`. The Russian notation `2к6` (and `д`) works too. The result shows each die in its own shape (d4 a
triangle, d6 pips, d8 a diamond, d10 a kite, d12 a pentagon, d20 a hexagon), the maximum in green, a one in
red, and the sum. d66 and d% show each die on its own, without a sum. The last eight rolls are kept. Saved
rolls are the sheet's abilities with a formula (below): the "S" list on the M9, the buttons of the page and
the menu item of the one-button boards roll them.

**Warhammer.** A grid of 20 dice (up to 50 with "+5"): choose how many of the first dice to roll; the roll lays
them out from high to low. With the threshold on (`4+`) the dice at or over it are green, the others red, and
the successes are counted; without it, the sum.

**Characters.** The "General" set and up to five characters, each with its own sheet. A new character starts
with an empty sheet (one counter, as in DIC3R) or from the D&D 5e or R.E.T. template. The general set cannot
be removed. Since 0.26.0 a board adds two ready characters once, "D&D 5e" and "R.E.T." (where there is room):
open, look at, change or delete them; deleted ones do not come back (a flag in `dice.bin`).

## The character sheet

A sheet holds up to 64 blocks in rows of 12 columns. A block is 1/6, 1/4, 1/3, 1/2 of a row or the whole row
wide; on a phone the grid is narrower (6 columns), the OLED shows one block at a time. The blocks:

| Block | Shows | Action |
|---|---|---|
| **Stat** | a number in a shape (circle, square, shield, diamond, hexagon) and a label; below it the D&D modifier or the R.E.T. die | roll its check; ±1 |
| **Track** | the current value and a maximum (a bar) or just a number — the former counter | ±1, ±5, type a value |
| **Marks** | a row of circles (spell slots, death saves, ammunition), up to 12 | mark the next |
| **Ability** | a name, a roll, a cost and a description (up to 200 bytes) | roll and pay the cost |
| **Note** | text (equipment, background) | — |
| **Header** | a section | — |

**Codes and formulas.** A block may have a code of Latin letters, digits and `_` (up to 7: `dex`, `hp`,
`mana`). A stat's value and a track's maximum can come from a formula: integers, `+ − * /` (division rounds
down), brackets and references `@code`; for example `10+@dex` (armour class), `(@edu+@wit)/2`, `@body*2`. An
ability's roll is a roll formula with references: `d20+@dex+@pb`, `1d8+@str`, `@cha+3`, `r3+@fine!`. A
reference `@code` gives what the block "brings": a D&D stat its modifier, a R.E.T. stat in a roll the die of
its level (in arithmetic its level), the others their value (a track its current value, marks how many are
marked). `@code!` is the stat's own value (for tracks and marks, the maximum). A reference to a missing code or
a loop of references shows as "?", and such an ability does not roll.

**Stat rules.** "Plain number"; "D&D: modifier" — `(value − 10) / 2` rounded down below the value, a check is
`d20+modifier`; "Bonus to d20" — the value with its sign, a check is `d20+value` (initiative); "R.E.T.: die by
level" — level 0…8 gives the die d6−3, d6−2, d6−1, d6, d8, d10, d12, d20, d100, and a check rolls it.

**A R.E.T. roll** (`rN`): the top value of the die is a success and adds the die of the next level, on up to
d100; a one on the first die is a failure: the d4 of failure is rolled (1 is critical), the roll's bonuses are
lost and the result is the die alone. The screens show "Success x2", "Failure, d4: 3" or "Critical failure".

**An ability's cost.** An ability can take a number (`spend`) from the block with a code (`use`), a track or
marks. Using it rolls the formula (if any) and lowers that value; when it is short, it refuses: "Not enough:
Mana".

**Templates.** *D&D 5e*: the six abilities (modifiers), proficiency, armour class `10+@dex`, initiative
`@dex`, speed, hit points, hit dice, death saves, an attack and its damage, 18 skills (`d20+@ability`; add
`+@pb` for proficiency), level 1 slots, inspiration, equipment. *R.E.T.* (Mind, Emotions, Body; the system at
[nri-ret.ru](https://nri-ret.ru/rulebook/)): the stat tree — 12 level III stats (3 at the start) with the level
II and I stats above them as half the sum of the two below; health `@body*2`, sanity `@mind+@emo`, luck
`@lvl*2`, level `(@mind+@emo+@body)/3`, strike `@str/2`, speed, growth points, a sample skill (`@cha+3`) and
talent (`r3+@fine!`), a note on rolls, equipment. Block names follow the interface language; codes are Latin.
A template can be applied to an existing character too (page: "Builder → Template"); it replaces all its
blocks.

**Coming from 0.24.** On the first start of 0.25.0 the old `dice.bin` is converted: each character's counters
become tracks without a maximum, with the same value and colour; saved rolls become abilities.

## The sheet as text and over the mesh

A sheet is written as one line: `MMS1~N|name~kind|width|shape|colour|rule|code|name|value|max|formula|use|spend|note~…`
(empty and zero fields are left out; `\` escapes `\`, `|`, `~` and a line break). Copy the line on the page
("Text"), send it in any chat and add it on another device as a new character. From a phone a long line goes
in pieces (`dice sheet put K/N …`): a BLE write takes 255 bytes.

**Sending to a contact.** "Send" on the page, or `P` → `S` on the M9, chooses a chat contact. The sheet goes as
MeshCore direct messages in parts of up to 120 bytes like `🎲S 3F2A 2/9 …` (the MeshCore app shows them as
text); each next part goes after the ACK of the last, each is tried up to three times. A D&D sheet is about 13
parts. A MeshMesh receiver gathers the parts (for 10 minutes), checks the sheet and offers it: "Sheet
«Arwen» from Heltec V4" — accept (a new character) or decline. Parts are taken only from contacts and only in
direct messages; the offer lasts until a restart.

## Controls

**M9 and T-Deck.** Tabs on top: `M` (or `1`–`3`, a tap on a tab) changes the mode, `P` opens the characters.
RPG: `OK` rolls the pool, `<` `>` the die, `^` `v` the number of dice, `+`/`−` the modifier, `R` reset, `E` any
formula, `S` the saved rolls (`OK` roll, `A` add, `F` formula, `N` name, `C` colour, `DEL` twice — delete), `X`
clears the history. Warhammer: the arrows choose the number of dice, `OK` rolls, `A` five more dice, `R` reset,
`D` the die, `T` and `+`/`−` the threshold. Sheet: the arrows choose a block (up/down — the nearest in the next
row), `OK` rolls a stat's check or an ability (with its cost), marks the marks, opens a note's text; `+`/`−`
±1, `V` types a value (`23`, `+5`, `-7`), `I` the whole description. The last roll stays over the sheet for a
few seconds. Characters: `OK` choose, `A` new (a name, then a template), `N` name, `S` send the sheet to a
contact, `O` the sheet a contact sent, `DEL` twice — delete. On the T-Deck all of it works by touch: a tap on a
block chooses it, a second one rolls.

**The builder on the M9 and T-Deck** (since 0.26.0). On the sheet `E` opens the chosen block, `A` adds a block
after it. The block's fields are rows: `^` `v` choose a field, `<` `>` change the kind, width, colour, shape,
rule and the block's place in the sheet at once, `OK` types the name, code, value, maximum, formula, cost and
description (formulas and codes in Latin as they are, names and descriptions in the RU/EN layout), `DEL` twice
deletes the block, `BACK` returns to the sheet. A new block not touched yet takes the defaults of the new kind
when its kind changes. The device checks the fields as for the web page; a formula referring to a missing code
shows "The formula does not compute". On the T-Deck a tap chooses a row, a second tap is `OK`.

**One-button boards, GAT562, T114.** A click on the closed dice page moves to the next page, a hold opens the
dice. In RPG and Warhammer a short press repeats the roll, a hold opens the menu. On the sheet a click goes
through the blocks (the OLED shows the chosen block large, the T114 the grid of blocks as the M9 does); the
menu has the block's action ("Roll: Dexterity", "Mark", "Use"), "Count with button" or ±1 on the joystick, ±5,
the previous and next block, "Add counter", the character, the mode; a sheet sent by a contact adds "Accept
sheet" and "Decline the sheet". The joystick's up/down change the number of dice, the chosen grid dice or the
block's value. Blocks and templates are easier to make on the web page or in the app.

**Count with button** (one-button boards and T114). The item counts the chosen block's value (a track, a stat
without a formula, marks): click +1, hold (0.8 s) −1, holding on to 5 s leaves (that hold's −1 is taken back).
While counting, a press with the screen off counts at once.

**Web page and app.** The "Dice" section: mode tabs, the character ("+ New character…" — a name and a
template), the pool, a formula, saved rolls, the Warhammer grid. The "Sheet" tab: the last roll, the blocks as
a grid (a tap on a stat or an ability rolls, ± buttons, a tap on a track's number types it, the circles of
marks), "Builder" (↑ ↓ ✎ ✕ on each block; the block form: kind, name, code, width, colour, shape, rule, value
or formula, maximum, roll, cost, description; "Add a block"; "Template"), "Send" (contacts) and "Text" (the
sheet's line and adding a character from a line). A sheet sent by a contact shows on top with "Accept" and
"Decline". The page asks the device every 2 s (3 s in the app) and fetches the sheet only when its number
(`sheet.rev`) changes.

## The `dice` command

USB, Bluetooth and `/api/command`; `GET /api/dice` is the same as `dice`.

| Command | Action |
|---|---|
| `dice` | the state (JSON): mode, characters, pool, rolls, saved rolls, results, grid, `sheet.rev`, `offer`, `send` |
| `dice roll [FORMULA]` | roll a formula or the pool |
| `dice pool N TYPE [MOD]` | the pool: `dice pool 3 d8 -2` |
| `dice mode rpg\|grid\|sheet`, `dice clear` | the mode (`counters` is `sheet`); clear the history |
| `dice saved add FORMULA NAME`, `edit I FORMULA NAME`, `del I`, `color I C`, `roll I` | saved rolls (abilities with a formula) |
| `dice grid choose N\|roll\|row\|reset\|type T\|threshold off\|N` | the Warhammer grid |
| `dice sheet` | the current character's sheet (JSON: blocks with computed values, `b` the modifier or die, `roll` the roll with the codes put in, `bad` a formula that does not compute) |
| `dice sheet act I`, `dice sheet I +N\|-N\|set N` | roll, use, mark; change a value |
| `dice sheet add KIND [AT]`, `set I FIELD VALUE`, `move I TO`, `del I` | the builder: `stat track pips ability note header`; fields `name code width shape color rule kind value max formula use spend note` (`\n` in a note is a line break) |
| `dice sheet template empty\|dnd\|ret` | replace the sheet with a template |
| `dice sheet export`, `import TEXT`, `put K/N PIECE` | the sheet as a line; a new character from a line (whole or in pieces) |
| `dice sheet send NODE_ID`, `offer accept\|decline` | send the sheet to a contact; accept or decline the one that came |
| `dice char add NAME`, `new empty\|dnd\|ret NAME`, `use I`, `name NAME`, `del` | characters (`name` and `del` act on the current one) |

Actions answer `OK … {JSON}`, errors `ERR …`. In the JSON each part of a roll is one number: `value×4+kind`
(0 a die, 1 a number, 2 d66, 3 d%), negative when subtracted; R.E.T. rolls carry `successes`, `fail` and
`dice` (the shape of each die).

## Memory and size

A sheet is up to 64 blocks of 102 bytes and 2048 bytes of descriptions; only the current one is in memory
(about 8.6 KB on the heap). On the nRF52 the dice are an optional module (`--without dice`). For the sheet to
fit the Chinese T114 image (1 MB flash), the nRF52 libraries and core are built with `-Os` (our code with
`-O2`, except the files with an `-Os` pragma), see `platformio.ini`.

## Checked and not checked

Checked on a computer (`tools/ui_preview`, `dice checks passed`): formula rules, R.E.T. dice (chained
successes, failure, penalty levels, d100), D&D modifiers rounded down, derived values, reference loops, an
ability's cost, templates, the sheet line both ways (with escapes), upload in pieces, sending in acknowledged
parts and gathering them at the receiver, the conversion of a 0.24 `dice.bin`, reply sizes; the M9, T-Deck
(with touch), Heltec, GAT562 and T114 screens. The web section — in a browser against a mock API that runs the
same `Dice.cpp` (rolls, the builder, a cost, sending and receiving). The conversion of old counters was
confirmed on the M9. Results on the devices are in [verification.md](verification.md).
