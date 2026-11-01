# Contributing to Felucca

Thanks for helping! To keep things easy to follow, please use the right place:

| What | Where |
|---|---|
| A bug you can reproduce | [Issues](https://github.com/hugelton/Felucca/issues/new/choose): one issue per bug |
| A feature idea or request | [Discussions › Ideas](https://github.com/hugelton/Felucca/discussions/categories/ideas) (no promise it will be added) |
| How to use it, or how something works | [Discussions › Q&A](https://github.com/hugelton/Felucca/discussions/categories/q-a) |
| Forks, mods, music, demos | [Discussions › Show and tell](https://github.com/hugelton/Felucca/discussions/categories/show-and-tell) |
| Anything else | [Discussions › General](https://github.com/hugelton/Felucca/discussions/categories/general) |

Feature requests posted as issues will be moved to Discussions or closed.

## Reporting a bug

Please include the Felucca version (hold HOME > ABOUT), your computer's OS and browser if the editor or
installer is involved, the steps to reproduce it, what you expected and what happened. A photo or a short
video helps.

## Pull requests

Pull requests are welcome, including AI-assisted ones, with the same bar for all:

- keep it small and focused on one thing;
- make sure you understand the change and can explain it;
- test it on a real FM-1 (a change only to a host platform under `platforms/`: on that platform);
- run `tests/run_tests.sh` (see [BUILDING.md](BUILDING.md)); CI runs the tests that need no FM-1 toolchain on every
  pull request;
- new files carry the GPL-3.0-only SPDX header.

By contributing you agree that your contribution is licensed under GPL-3.0-only.

The ANALOG, LOFI, TRIO, WHEEL, NOISE and PHASE engines and the code they share (`firmware/engines/`, a git submodule) live in
[Felucca-Engines](https://github.com/hugelton/Felucca-Engines), under the MIT licence: changes to them go there, as
pull requests to that repository.

The web editor and installer live in [Felucca-WebApp](https://github.com/hugelton/Felucca-WebApp).

A new sound engine does not need a pull request here: make it a plugin, in a repository of your own and under the
licence you choose ([docs/PLUGINS.md](docs/PLUGINS.md)). To reserve its id, add it to Felucca-Engines'
`docs/PLUGIN_IDS.md` by a pull request there. A new platform: see [docs/PORTING.md](docs/PORTING.md), and open an issue
first so that the work can be shared.
