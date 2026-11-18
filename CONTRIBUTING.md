# Contributing to Kaepora

One person maintains Kaepora and reads every line that goes in. Small changes that say plainly what they do get merged. For anything large, open an issue first, so the work is agreed before you write it.

## Never share Nintendo's data

Do not put the game file, or any file taken out of the game, into a commit, an issue or a pull request. The same goes for Ship of Harkinian's `oot.o2r`. A pull request that contains game data is closed.

## Report a problem

Open an issue with one of the forms. The most useful report is a seed that cannot be finished, with its spoiler log attached.

## Build and test

The [README](README.md#build-from-source) has the steps. Some tests read the game file, and GitHub cannot run those. Run them yourself with `-DKAEPORA_GAME_NUP=<your game file>` and say in the pull request that they passed.

## Change a rule of the logic

The logic under `shipwright/` is Ship of Harkinian's, changed where the remake differs from the original game. A rule changes only on evidence from the remake:

- Say what you did in the game and what happened. A short video or a screenshot helps.
- A rule that asks for less than the game can leave a seed that cannot be finished. So "it worked for me once" is not enough for a rule that lets Link through. Say where Link stood and what he held.
- If you only read the game's files, say so. A reading is a lead, and someone has to see it in the game before the rule changes.

## Send a pull request

- One change per pull request, at the size of the problem.
- Write the test first where the behaviour is known, and watch it fail. One test per behaviour a player or a caller depends on.
- Every commit builds with GCC and with Clang, with no warnings from Kaepora's files, and passes its tests.
- The stored seed in `tests/fixtures/` is regenerated only by that commit's own `kaepora-seedgen`, never by hand.
- Use [conventional commit](https://www.conventionalcommits.org) subjects, such as `fix: the hookshot hits the eye switches that take it`. The message says what was seen in the game, what was measured, and what was only read from the game's files.
- Format C++ with the repository's `.clang-format`. A comment states a constraint the code cannot show. It carries no history.
- Ask before you add a dependency. Say what it is, who maintains it, and why.
- A change to the window comes with a screenshot.

## Licence

Kaepora's own code is under the MIT licence. By sending a pull request you agree that your change is too.
