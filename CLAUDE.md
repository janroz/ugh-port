# UGH – reimplementace DOS hry v C++

Originál: `ugh/UGH.EXE` (Borland C++ 1991, 16bit real mode, DOS MZ). Originální soubory nikdy neměnit.
Analýza přes REA plugin (Ghidra 12.1.4), porovnávání chování v `dosbox-x`.

## Stack

- C++23, CMake + Ninja, SDL 3 (`find_package(SDL3)`).
- Kompilátor: Apple clang / Homebrew LLVM (libc++).

## Pravidla paměťové bezpečnosti (závazná)

### Vlastnictví a životnost
- Žádné `new` / `delete` / `malloc` / `free` / `realloc`. Dynamická alokace jen přes kontejnery (`std::vector`, `std::string`, …) nebo `std::make_unique`.
- Vlastnictví vyjadřuje typ: hodnota > `std::unique_ptr` > `std::shared_ptr` (jen při skutečně sdíleném vlastnictví, zdůvodnit).
- Holé ukazatele a reference jsou vždy nevlastnící. Nevlastnící pohled na data = `std::span` / `std::string_view`; nikdy je neukládat déle, než žije vlastník.
- Každý SDL zdroj (`SDL_Window`, `SDL_Renderer`, `SDL_Texture`, `SDL_AudioStream`, …) okamžitě zabalit do `std::unique_ptr` s deleterem (RAII). Žádné ruční `SDL_Destroy*` mimo deleter.
- Žádné vracení referencí/pohledů na lokální nebo dočasné objekty.

### Meze a přístup k datům
- Žádná C pole – `std::array`. Žádná aritmetika ukazatelů – indexy nad `std::span`.
- `operator[]` na `std::vector`/`std::span`/`std::array` je díky libc++ hardening kontrolovaný i v Release (při chybě program bezpečně skončí), proto se preferuje před výjimkami házejícím `.at()`.
- Binární data z originálu (grafika, palety, úrovně, `UGH!.HI`) parsovat výhradně přes bezpečnou čtecí vrstvu (bounds-checked reader vracející `std::expected`). Žádný `reinterpret_cast` / přetypování bufferu na struct; little-endian hodnoty skládat z bajtů nebo `std::bit_cast`.
- Indexy z dat (offsety, rozměry sprite, ID dlaždic) před použitím validovat.

### Typy a čísla
- Žádné C-style casty; jen `static_cast` (a `std::bit_cast`). `reinterpret_cast` / `const_cast` zakázány.
- Kde emulujeme chování originálu, používat přesné typy (`std::uint8_t`, `std::int16_t`, …) a přetečení/wraparound dělat explicitně, ne spoléhat na UB u signed typů.
- Všechny proměnné inicializovat. Žádné zužující konverze bez explicitního castu.

### Ostatní
- Žádné `goto`, žádný globální měnitelný stav – stav hry v objektech předávaných explicitně.
- Očekávané chyby (chybějící soubor, poškozená data) přes `std::expected`; žádné výjimky v herní smyčce.
- `const` a `[[nodiscard]]` všude, kde to dává smysl.

## Vynucení
- Warningy: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnull-dereference -Werror`.
- libc++ hardening: Debug `_LIBCPP_HARDENING_MODE_DEBUG`, Release `_LIBCPP_HARDENING_MODE_FAST` (kontrola mezí `operator[]`).
- Debug build s AddressSanitizer + UndefinedBehaviorSanitizer.
- `clang-tidy` (konfigurace v `.clang-tidy`) musí projít bez nálezů; výjimky jen s `// NOLINT(<check>)` a zdůvodněním.
  Spuštění: `run-clang-tidy -p build -extra-arg=-isysroot$(xcrun --show-sdk-path) "^$PWD/(src|tools|tests)/"`
  (regex ukotvit na projekt, jinak zachytí i `build/_deps/nuked_opl3-src`)
  (Homebrew LLVM v `/opt/homebrew/opt/llvm/bin`; bez `-isysroot` nenajde systémové hlavičky).

## Sestavení a testy
- `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`
- `build/ugh [cesta/UGH.EXE]`: EXE hledá v pořadí argument → kopie v `SDL_GetPrefPath("Play Byte", "UGH")` → vedle
  programu / v cwd (`UGH.EXE`, `ugh/UGH.EXE`) → dialog; ověří SHA-256 (`kGameExeSha256`) a nalezený soubor zkopíruje do
  té složky (`src/platform/game_exe.cpp`). Tam jsou i rekordy (UGH!.HI, XOR 0xFF jako originál), nikdy vedle originálu;
  `ugh-snap` je nenačítá ani neukládá. Uživatelská dokumentace: `README.md` (anglicky)., `build/ugh-extract ugh/UGH.EXE <výstup>` exportuje PNG.
- `build/ugh-snap ugh/UGH.EXE <výstup> 100:01 320=shot …` – headless běh po 70Hz snímcích: `N:XX` stiskne klávesu
  (set-1 make code hex), `N=shot` uloží PNG. Slouží k vizuální kontrole bez okna.
  Dále `N+XX` / `N-XX` drží / pouští klávesu, `e0XX` = rozšířená klávesa (šipky `e048/e050/e04b/e04d`),
  `trace=OD-DO` vypíše stav hráče 1 po snímcích. V zsh psát `${f}:e048` (jinak `$f:e` = modifikátor).
- `tests/play_tests.cpp` – skriptované hry (menu → úroveň 1 → fyzika). Očekávané hodnoty jsou zatím regresní
  hodnoty portu; po ověření proti originálu v DOSBox-X se z nich stanou referenční.

## Architektura portu
- `src/vga/` – emulace VGA (4 roviny, map mask, latch write mode 1, start address, split screen, DAC). Grafické rutiny
  originálu (`src/game/video.cpp`) se přepisují 1:1 nad ní, včetně přepínání stránek a pozadí.
- `src/runtime/task.hpp` – coroutines `Task<T>` + `FrameClock`: blokující čekání originálu na vertical retrace
  = `co_await clock.nextFrame()`. Parametry coroutines nikdy jako reference (jen hodnoty / ukazatele, `this`).
- `src/game/game.cpp` – řídicí tok hry; každá metoda odpovídá rutině originálu (adresa v komentáři).
- `DataSegment` – měnitelná kopie DGROUP; texty a tabulky se používají na původních offsetech (`layout::ds`).
- Herní proměnné žijí v `DataSegment` na původních adresách, přístup jen přes pojmenované `ByteVar`/`WordVar`/`WordArray`
  v `src/game/vars.hpp` (zachová nulování rozsahů, výchozí hodnoty z EXE a 16bit wraparound). Neznámý význam = jméno
  s adresou (`kUnknown27c8`), přejmenovat až po ověření.
- Herní kód: `play.cpp` (0c07 a úroveň), `physics.cpp` (1095 let, 1457 kolize, 4567 klávesnice, 3a67 plošiny),
  `status.cpp` (stavový panel). Nedokončené části označené `TODO(game|music)`.
- Postavy (`passengers.cpp`): stav = adresa rutiny originálu uložená v DS, typové záznamy v DS obsahují adresy
  následných rutin. `passengerRoutine()` mapuje adresu → metodu; skoky (`JMP [SI+n]`) vrací metoda jako
  `PassengerJump` a provede je trampolína v `runPassenger()` (žádná rekurze). Neznámá adresa = hlášení na stderr.
- Skriptované sekvence (`sequence.cpp`, 113b:08d5): obě dema v atrakci, úvodní přelet před levelem a závěr po posledním
  levelu jsou jeden přehrávač – herci se skripty v DS (sprite + krok dx/dy, smyčky `-n … 0x8000`) nad mapou levelu,
  pole cestujících se pro ně recyklují. Jakákoli klávesa sekvenci ukončí (testy tak přelet přeskakují).
- Nepřátelé (`enemies.cpp`) stejně jako postavy (`EnemyJump` nese i slot kamene, který je zasáhl).
  Voda a déšť (`water.cpp`); vzor hladiny se dekóduje za běhu z rozbaleného kódu originálu (`decodeWaterSurface`),
  generátor náhodných čísel startuje ze stavu v `CS:4ef7` (originál ho nikdy neinicializuje → déšť je deterministický).
- `ugh-snap` umí `N=text:HESLO` (zadání hesla přes BIOS) a `N=hover` (drží stroj ve vzduchu) – rychlý vizuální test
  libovolné úrovně: `100:01 400:01 700:3c 720=text:HESLO 800:3b 1080:39 1170=hover 1300=shot`.
- Zvuk (`src/sound/`): port knihovny ze segmentů 1878 (C rozhraní) a 1664 (ovladač AdLib) + multiplexer časovače
  1a32; proměnné knihovny emulované v `DataSegment` segmentu 1664. Hudba/efekty = formát ADLX (AHDR/IMAP/TRCK).
  Čas zvuku běží jen přes `AdlibDriver::render()` – host ho volá každý snímek (`SampleClock`), i v testech a ugh-snap,
  jinak se zastavování hudby s fadem (originál na něj aktivně čeká) nikdy nedokončí. OPL emuluje Nuked-OPL3
  (FetchContent, LGPL-2.1). Kvirky originálu: počet hudebních kanálů `1664:022e` je vždy 0 → fade hudby je neslyšný
  a kanály po efektu se do hudby nevrací (ztichnou do další noty); hlasitost hudby je cca −22 dB.
- Výpisy oblastí bez funkcí v Ghidře: `analysis/dumps/` (lineární disassembly; pozor na místa, kde sweep začal
  uprostřed instrukce – např. `8B 44 xx` = `MOV AX,[SI+xx]`).
- Kolize = pixely stránky pozadí ve VRAM s nastaveným bitem 7 (pevné dlaždice kreslí barvami 128–255). Proto se
  kreslení pozadí musí chovat přesně jako originál, jinak se změní fyzika.
- Klávesnice: host posílá skutečné bajty (prefix E0 u rozšířených kláves), `Game::serviceKeyboard()` je zpracuje
  tabulkou z `DS:281c` jako ISR originálu; host ji volá před každým snímkem.
  Mac nemá Insert ani pravý Ctrl (střelba hráče 1) → `platform/keyboard.cpp` posílá za pravý Option/Cmd `E0 1D`.
- Ověřování věrnosti: originál v `dosbox-x` (`-c`, `-time-limit`, debugger viz `-helpdebug`); nikdy nespouštět
  `dosbox-x` bez parametrů z terminálu (otevře okno a čeká).

## Analýza originálu
- `analysis/export/` – dekompilát (`decompiled.c`), assembler (`listing.asm`), funkce, řetězce (Ghidra headless export,
  skripty v `analysis/ghidra_scripts/`). `analysis/dump.sh <seg> <od> <do>` vypíše libovolný rozsah včetně kódu mimo funkce.
- Adresy v `listing.asm` u skoků/volání jsou lineární vůči segmentu 1000 (`CALL 0x1000:5873` = `113b:44c3`).
- Ověřené struktury dat a jejich umístění: `src/data/layout.hpp`.
- Ghidra načítá EXE od segmentu 1000: bajt `seg:off` je v souboru na `0xa00 + (seg − 0x1000) * 16 + off`
  (DS 6c09 → 0x5c090 + 0xa00). Far ukazatele v `CS:4610..4633` (banky spritů, adresář, …) jsou v EXE předvyplněné
  a segmenty doplní relokace – „nulový“ ukazatel tam nehledat podle listingu, ale v souboru + relokační tabulce.
