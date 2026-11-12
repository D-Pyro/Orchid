> [!IMPORTANT]
> ## Please read first
> - **Hyrule Warriors: Age of Calamity and Xenoblade Chronicles 3 are no longer supported.** Neither has a Switch 2 improvement, so Orchid now shows the unsupported-game notice when you start them.
> - **Kirby and the Forgotten Land, Star-Crossed World:** the slow memory growth some of you reported (freeze or crash after a while, mostly on 8 GB graphics cards) is **not fixed yet**. A fix is being tested in [#146](https://github.com/MythosOfs/Orchid/pull/146).
> - **Kirby Air Riders:** races can still hang the GPU. Please don't play it on Orchid yet.
> - Found a problem? Use **Help > Report an Issue on GitHub** inside Orchid. It fills in your build, system, GPU, driver and recent game log for you.

# Orchid 0.10

This is a big one: more than 100 changes since 0.9.5. Most of them are many small fixes for crashes, freezes and rendering errors, collected from other emulator projects and tested in Orchid. Here are the parts you'll notice.

## What's better in your games

- **Super Mario Bros. Wonder:** no more drops to 20-30 fps after playing a while. Graphics memory used to fill up whenever the game changed its resolution.
- **Kirby and the Forgotten Land (Switch 2 Edition):** holds 60 fps in Waddle Dee Town instead of about 52, thanks to better default GPU settings. If you changed those settings yourself, your choice is kept.
- **Yoshi and the Mysterious Book:** the video player no longer aborts the game after a few minutes. Hyrule Warriors: Age of Imprisonment had the same rare problem, and that's fixed too.
- **Mario Tennis Fever:** button icons show up in text instead of question marks.
- **Pokémon Legends: Z-A:** the cutscene after the train station is no longer one flat colour.
- **Sound:** fixed loud clicks at the start of some sound effects, plus audio that could stop in some games.
- **Fewer short freezes on Linux:** Orchid no longer forces unused game memory into swap, which could pause the whole game (and its sound) for several seconds.
- **Fewer crashes, hangs and broken graphics overall.** Lots of fixes to shaders, textures and GPU syncing, especially for Switch 2 games.

## New features

- **New modern interface (experimental, off by default).** Turn it on under **Configure > General > UI > Modern interface**. You get a sidebar, a big picture for the selected game, a game details page and settings right in the main window. You can add your own hero art per game, or pick it from SteamGridDB with your own API key.
- **Better "Standard" graphics preset.** It now uses SMAA and 16x texture filtering, which looks clearly sharper and costs almost nothing. "Low" and "High" haven't changed.
- **More control over NUP game files.** In a game's **Add-Ons** tab you can now see every update, DLC and Switch 2 Edition inside a NUP and switch each one off. For example, you can boot the Switch 1 version instead of the Switch 2 Edition.
- **More dump layouts are recognised.** Switch 2 Edition dumps that keep the program in a `program1` folder (for example Kirby) boot and show up in the game list.

## For bug reports

Orchid now writes graphics and system memory use to the log once a minute. If a game slows down or crashes after a while, that line helps a lot, so please attach the full log.

## Thanks

Many fixes in this release come from other Eglantine-based projects and their authors. They're credited in each change below and in the README.
