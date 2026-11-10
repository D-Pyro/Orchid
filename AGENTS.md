# Repository instructions

## Commit and push completed changes

For small, clearly scoped fixes requested by the user (for example, emulator UI or
status bar fixes), complete appropriate checks, then commit the changes and push
them directly to `origin/main` without asking for confirmation. This is the user's
standing authorization for these fixes, unless they explicitly request otherwise.

For larger changes requested by the user, also commit and push directly to
`origin/main` without asking for confirmation once all required tests and checks
have completed successfully. Failed, skipped, or unavailable required checks do
not satisfy this condition. This is the user's standing authorization for these
larger changes, unless they explicitly request otherwise.

Include only changes belonging to the requested task. Fetch `origin/main` before
pushing and integrate any newer changes as needed. Never force-push `main` or
overwrite unrelated user changes. If a push is blocked by branch protection or
permissions, report the blocker instead of bypassing it.

# Local development builds

For repeated local Linux emulator testing, use `./tools/build-local.sh` from this
checkout. Run the resulting GUI with `./build-local/bin/orchid`. Keep the build
directory between edits; do not clean or recreate it routinely.

The script uses Ninja, ccache, the available faster linker, `-O2 -g1`, and no LTO.
It builds the `yuzu` GUI target (output name `orchid`) and its dependencies, without
tests, CLI or the standalone room server. Normal emulation and graphics features
remain enabled. This is a development configuration: use a separate release/PGO
build for performance comparisons, or full debug information for variable inspection.

- Required tools: normal Linux build dependencies plus `ccache` and Ninja.
- Compiler cache: `${XDG_CACHE_HOME:-$HOME/.cache}/orchid/ccache`, shared across
  worktrees. Prefer the script so its ccache environment and debug path mapping
  remain consistent. Do not clear the cache routinely.
- Parallelism is capped by CPU count and available RAM. Override with
  `CMAKE_BUILD_PARALLEL_LEVEL=6 ./tools/build-local.sh` if necessary. Avoid running
  several full builds concurrently on the same host.
- Existing builds skip explicit reconfiguration to avoid timestamp-induced rebuilds.
  Ninja detects changed CMake inputs automatically. Configure explicitly with
  `./tools/build-local.sh --configure-only`, or pass extra CMake options.
- Extra arguments are CMake configure options. Use `ORCHID_BUILD_DIR` for a separate
  output directory. Do not reuse one build directory across different source worktrees.
- Dependency downloads can be shared via `-DCPM_SOURCE_CACHE=/absolute/cache/path`.
  CMake preserves this choice in that build directory. On Leon's PC an existing
  dependency cache is `/home/leon/src/Eglantine/.cache/cpm`; reuse it when available
  instead of downloading the same sources for every worktree.
- First builds populate the compiler cache and can still take time. Do not claim a
  measured speedup without measurement. Verify the build and report any limitations.

See [docs/Build.md](docs/Build.md#faster-local-linux-test-builds) for details. These
settings are for local iteration; preserve release/CI configurations.
Measured results and their limits are recorded in
[docs/LocalBuildBenchmark.md](docs/LocalBuildBenchmark.md). In particular, cached
rebuild timings must not be presented as timings for genuinely changed source.

For GitHub Actions, preserve Release/PGO flags and packaging targets. The release
workflow already uses compiler caching and Ninja; normal Linux builds use mold,
while PGO builds retain LLD. Consult per-job build durations and cache statistics
in Actions summaries before claiming a CI speedup. Local timings do not establish
hosted-runner performance. Do not cancel or restart an important running release
to apply build optimizations; prepare them for subsequent runs.

# Orchid agent workflow

## Orchid: automatische Mutationsprüfung bei Agent-Tests

Gilt für `/home/leon/src/Eglantine`, dessen Worktrees (auch unter `.t3/worktrees/`
und `Documents/Codex/`) und andere Checkouts von MythosOfs/Orchid.
Bei Orchid-Änderungen an Produktionscode oder Unit-Tests die Mutationsprüfung
selbstständig als verpflichtenden Teil der Abschlussverifikation ausführen, ohne
Erinnerung, gesonderten Auftrag oder Rückfrage an Leon. Das gilt auch für Subagents
und automatische Issue-, Review-/Merge- und Forkscan-Läufe, wenn sie solchen Code ändern.
Passenden Catch2-Filter, Build-Ordner und Diff-Basis selbst bestimmen; bei reinen
Teständerungen die betroffene Produktionsdatei mit `--file` auswählen.
Für diese Verifikation verbindlich den lokalen Runner verwenden:

```sh
orchid-agent-test -- "<passender Catch2-Filter>"
# Andere Build-Verzeichnisse oder bereits committete Änderungen:
orchid-agent-test --build-dir build-local --base origin/main -- "<Filter>"
# Bei reinen Teständerungen die zugehörige Produktionsdatei angeben:
orchid-agent-test --file src/common/example.h -- "<Filter>"
```

Der Runner baut und testet zuerst normal, danach automatisch eine begrenzte Auswahl
von Produktionscode-Mutationen mit dumbmutate in einer privaten Quellkopie.
Standard: maximal drei geänderte Zeilen, mindestens 80 % erkannte gültige Mutanten.
`--base HEAD` erfasst unstaged, staged und neue Produktionsdateien; für committete
Branch-Änderungen die fachlich passende Basis explizit übergeben.
Direkte `build/bin/tests`-Aufrufe bleiben für schnelle Diagnose erlaubt, ersetzen
aber nicht diesen Abschlusslauf. Exit 1 = Mutation-Score unter Schwelle;
Exit 2 = fehlgeschlagene Baseline oder unvollständige/inkonklusive Prüfung.
Keine passenden Mutationen gilt nicht als bestandene Mutationsprüfung. Überlebende
Mutanten untersuchen: Test ergänzen oder Verhaltensäquivalenz konkret begründen;
die Schwelle nicht bloß zum Grünwerden senken. Teilprüfung (Dateien/Zeilenbudget)
im Ergebnis nennen; sie garantiert keine vollständige Fehlerfreiheit.
Berichte: `/home/leon/Orchid/orchid-issues/mutation/<Lauf>/`.
Private Builds unter `~/.cache/orchid-mutation/` nicht ausliefern oder direkt starten.
Die private Kopie übernimmt den aktuellen Build per Btrfs-Reflink; bubblewrap
erhält dessen absolute Pfade in einer eigenen Mount-Umgebung. Nur betroffene
Dateien müssen für Mutanten neu gebaut werden. Zeitlimit pro Zeile: 10 Minuten;
Timeouts gelten als unvollständig. Ein veralteter normaler Build wird zuerst aktualisiert.
Installation/Quellen: `~/.local/share/orchid-mutation/`, Engine:
`~/.local/share/orchid-dumbmutate/`. Keine Git-Hooks oder globalen Compilerflags.

Ninja-Diagnosen (`-n`, `-t`) ebenfalls nicht parallel auf dasselbe Orchid-Build-Verzeichnis richten;
auch Diagnoseaufrufe können beschädigte Ninja-Logdateien verändern.

## Orchid-Testshortcuts richtig anlegen (Leon testet selbst)

Ein Shortcut gilt erst als fertig, wenn sein exakter `Exec` mit `--check` durchläuft. Leon soll nie zweimal klicken müssen.

1. Datei `~/.local/share/applications/orchid-test-<label>.desktop`:
   ```ini
   [Desktop Entry]
   Type=Application
   Name=Orchid <label> (Test)
   Exec=env EDEN_XYZ=1 /home/leon/.local/bin/orchid-test-run <label> <absoluter Pfad zu AppImage oder build*/bin/orchid> %f
   Icon=orchid
   Terminal=false
   Categories=Game;Emulator;
   X-Orchid-Test=true
   ```
   Env-Schalter nur über `env VAR=wert` vor `orchid-test-run` setzen. Pfade mit Leerzeichen in `"..."`. Kein `QT_QPA_PLATFORM` und kein `DISPLAY` setzen, das macht `orchid-test-run`.
2. Prüfen, alle drei müssen grün sein:
   ```sh
   desktop-file-validate ~/.local/share/applications/orchid-test-<label>.desktop
   ex=$(sed -n 's/^Exec=//p' <datei> | sed 's/ %f$//; s#orchid-test-run #orchid-test-run --check #'); eval "$ex"   # -> "check ok"
   ldd <binary> | grep 'not found'    # muss leer sein (bei AppImages entfällt das)
   ```
3. `update-desktop-database ~/.local/share/applications`.

Warum (Stand 2026-11-07): Hyprland 0.56 stellt Xwayland nur als `/tmp/.X11-unix/X0_` bereit, Qt-xcb sucht aber `X0` und bricht dann nach 0,5 s mit Exit 134 ab. Das sieht so aus, als würde der Shortcut nicht starten. `orchid-test-run` legt den Link `X0 -> X0_` jetzt selbst an, prüft das Display mit `xprop` und meldet Startfehler als Desktop-Benachrichtigung. Startet ein Test trotzdem nicht: `coredumpctl list` und `~/orchid-perf/<neuester>/meta.txt` (Exit-Code) ansehen.

## Orchid: Worktrees und Builds nach Abschluss aufräumen

Sobald eine Orchid-Aufgabe fertig ist und ihr Worktree/Build nicht mehr gebraucht wird, räumt
der Agent selbst auf, ohne Rückfrage (Leon 2026-11-08). Gilt für Worktrees unter
`~/.t3/worktrees/Eglantine/`, `~/src/Eglantine-*`, `~/src/Eglantine/.claude/worktrees/`,
`~/Documents/Codex/*/work/`, für deren `build*/`-Ordner, Test-Binaries unter
`~/Orchid/orchid-builds/` und private Builds unter `~/.cache/orchid-mutation/`.

- Entfernen nur, wenn alles gesichert ist: `git status` ohne Code-Änderungen (nur Logs/Builds),
  `git rev-list HEAD --not --remotes` leer, kein Prozess mit cwd/Binary darin, kein
  `orchid-test-*.desktop` zeigt darauf, kein laufender T3-Thread arbeitet darin.
- Dann `git -C /home/leon/src/Eglantine worktree remove --force <pfad>`, lokalen Branch löschen
  (Tipp liegt auf dem Remote), `git worktree prune`.
- Worktrees mit ungesicherter Arbeit bleiben; deren alte `build*/`-Ordner dürfen weg (ccache baut neu).
- Test-Binaries in `~/Orchid/orchid-builds/` löschen, sobald Leon mit dem Test durch ist
  (zusammen mit den `orchid-test-*.desktop`-Shortcuts). Spieldaten (`.nup`, Dumps) nicht.
- Nicht anfassen: Haupt-Checkout `~/src/Eglantine` und dessen `build/`, Worktrees fremder Repos
  (z.B. `~/Development/Eglantine-Official`, Djihads80), den eigenen laufenden Worktree.

## Orchid: alle Emulator-Tests laufen auf dem PC

Gilt für alle Orchid-Agents und Subagents (Leon 2026-11-10). Jeder Test, der den
Emulator startet (Performance-/fps-Messungen, headless Boots, Repro-Läufe, A/B-Vergleiche,
Screenshots, Logs), läuft auf dem **PC** (RX 9070 XT), nicht auf dem Laptop. Dort immer
per `agent-run` starten (z.B. `agent-run ./build/bin/orchid -g <game>`), MCP `orchid` macht das schon.
Eigenen Emulator nach dem Test beenden.

- Der Laptop (`leon@192.168.150.206`, Tailscale `100.111.165.26`) führt keine Emulator-Tests
  mehr aus. Die alte `.126` ist ungültig.
- Performance und Sound testet der Agent selbst am PC, auch wenn Leon zu Hause ist
  (Leon 2026-11-10). Kurze Läufe, danach Emulator beenden.
- **Sound prüfen (Aussetzer = Lücken in der Wellenform):** Spiel mit `PULSE_SINK=orchid_test`
  starten, damit nichts aus Leons Lautsprechern kommt und nur der Emulator aufgenommen wird:
  ```sh
  M=$(pactl load-module module-null-sink sink_name=orchid_test)
  parec -d orchid_test.monitor --file-format=wav rec.wav &   # vor dem Spielstart, danach kill
  # ... PULSE_SINK=orchid_test agent-run ./build/bin/orchid -g <game> ...
  ffmpeg -hide_banner -i rec.wav -af silencedetect=n=-60dB:d=0.02 -f null - 2>&1 | grep silence_
  pactl unload-module $M
  ```
  Jede Stille ab 20 ms mitten in durchgehender Musik/Effekten ist ein Aussetzer (Underrun).
  Stille am Anfang, an Ladebildschirmen und in echten Spielpausen ist normal: immer gegen
  einen Baseline-Lauf (letztes gutes Build, gleiche Szene) vergleichen und zusätzlich das
  Emulator-Log auf Audio-Underruns prüfen. Das findet Lücken, nicht falsche Tonhöhe, Tempo
  oder fehlende Instrumente; dafür Spektrogramm/Länge gegen die Baseline vergleichen.
  Die Methode wurde mit einem Testton mit eingefügter 80-ms-Lücke verifiziert, noch nicht mit Orchid.
- **Builds verteilen:** Ist der Laptop gerade frei (kein `orchid`/`eden-cli`, kein anderer Build),
  darf dort kompiliert werden, um den PC zu entlasten. Prüfen z.B. mit
  `ssh leon@100.111.165.26 'pgrep -x orchid || pgrep -x eden-cli || pgrep -x ninja'` (keine Ausgabe = frei).
  Pro Maschine nur ein voller Build gleichzeitig; sonst am PC bauen (`build-scope`/`agent-run`).
  Das fertige Binary oder AppImage per `scp` auf den PC holen und mit `ldd` prüfen.
