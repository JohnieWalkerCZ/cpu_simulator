# Přizpůsobitelný simulátor mikroprogramovaného procesoru

Datově řízené prostředí v C++ pro návrh a simulaci vlastních procesorových architektur. Architekturu, instrukční sadu, mikroprogramy i periferní zařízení lze definovat v souborech JSON. Projekt obsahuje assembler, vykonávací jádro a grafické rozhraní založené na Dear ImGui.

**Dokumentace:** [Konfigurace procesoru](CONFIGURATION.cs.md) · [Configuration reference (English)](CONFIGURATION.md) · [README (English)](README.md)

## Funkce

- Šířka datové i adresové sběrnice od 4 do 128 bitů po násobcích čtyř.
- Fyzické registry s hierarchickými podregistry, bitovými posuny a maskami.
- Von Neumannova i Harvardská paměť, nastavitelné pořadí bajtů a ochrana segmentů pro čtení (`R`), zápis (`W`) a vykonávání (`X`).
- ALU definovaná výrazy nebo krátkými programy; konfigurovatelná pravidla příznaků.
- Mikroprogramované instrukce, režimy latence, vícenásobné operace a vektorizovaná přerušení.
- Periferie mapované do paměti (MMIO), jejich registry, stav a reakce na čtení, zápis nebo tik hodin.
- Grafický simulátor, assembler a editor konfigurací.

## Požadavky a sestavení

Je potřeba CMake 3.20+, kompilátor a CMake toolchain s podporou nastaveného standardu C++26, vývojové knihovny SDL2, OpenGL a přístup k síti pro stažení `nlohmann/json` a Dear ImGui při první konfiguraci.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Hlavní programy `cpu_sim` a `cpu_configurator` se vytvoří ve výstupním adresáři sestavení. Konkrétní umístění závisí na generátoru CMake.

## Grafický editor konfigurací

Editor otevře prázdnou šablonu nebo existující soubor:

```bash
./build/cpu_configurator
./build/cpu_configurator configs/8bit.json
```

Karty pokrývají systém, registry, paměť, ALU, instrukce a periferie. Editor kontroluje konfiguraci průběžně podle pravidel simulátoru. Nabízí také surový JSON, náhled instrukcí, mikrooperací a diagnostiku s návrhy oprav. Úpravy podporují vrácení a opakování (`Ctrl+Z`, `Ctrl+Shift+Z` nebo `Ctrl+Y`); neuložené změny a mazání vyžadují potvrzení.

Tlačítko **Launch Simulator** uloží konfiguraci a spustí simulátor. V simulátoru lze tlačítkem **Edit in Configurator** otevřít právě načtený soubor v editoru.

Podrobnosti o polích editoru, blocích periferních háčků, latenci a dalších funkcích jsou v [příručce editoru a konfigurace](CONFIGURATION.cs.md#7-grafický-editor-konfigurací).

## Spuštění simulátoru

Předejte programu cestu ke konfiguraci procesoru:

```bash
./build/cpu_sim configs/8bit.json
```

Ovládací panel umožňuje spouštět procesor po instrukcích nebo po taktech. Další panely zobrazují mikrooperace, assembler, registry, paměť a periferie. Předpřipravené konfigurace jsou ve složce `configs/`.

## Testy

Testovací sada pokrývá konfigurace, ALU, assembler, dekodér, registry, paměť a integraci procesoru. Po sestavení ji lze spustit z kořene projektu:

```bash
ctest --test-dir build --output-on-failure
```

## Diagramy

Zdrojové diagramy ve formátu Mermaid jsou ve složce `diagrams/`. PNG lze vygenerovat pomocí Mermaid CLI:

```bash
npm install -g @mermaid-js/mermaid-cli
cd diagrams
chmod +x generate_images.sh
./generate_images.sh
```
