# Grafický konfigurátor: ovládání

[English](configurator-guide.md)

Konfigurátor upravuje JSON definici procesoru. Podrobný význam konfiguračních polí popisuje [česká příručka konfigurace](../CONFIGURATION.cs.md) a [úplná anglická reference](../CONFIGURATION.md). Postup při tvorbě architektury najdete v [průvodci konfigurací](configurator-workflow.cs.md).

## Spuštění

Z kořene projektu sestavte a spusťte editor:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cpu_configurator
./build/cpu_configurator
./build/cpu_configurator configs/8bit.json
```

Bez argumentu se otevře nová šablona. Cesta jako argument otevře existující konfiguraci. Požadavky na sestavení jsou v [README](../README.cs.md).

## Soubor a hlavní panel

- **New** vytvoří šablonu; tlačítka předloh načtou dodané konfigurace.
- **Open** otevře soubor; pod ním jsou dostupné poslední soubory.
- **Save** uloží dokument, **Save As** uloží pod jinou cestou a **Export JSON** exportuje platnou konfiguraci.
- **Launch Simulator** uloží konfiguraci a spustí simulátor. Cíl `cpu_sim` musí být sestaven vedle konfigurátoru. V simulátoru otevře konfiguraci zpět tlačítko **Edit in Configurator**.
- **Undo** a **Redo** vracejí a opakují změny, maximálně 50 kroků. Mazání a opuštění neuložených změn vyžadují potvrzení.

| Zkratka | Akce |
| --- | --- |
| Ctrl+S | Uložit |
| Ctrl+Z | Vrátit změnu |
| Ctrl+Shift+Z nebo Ctrl+Y | Opakovat změnu |
| Ctrl+D | Duplikovat vybranou položku aktivní karty |

Zkratky se nespouštějí při úpravě textového pole. Měřítko rozhraní lze změnit v horním panelu; nastavení a poslední soubory se ukládají do `~/.config/cpu_configurator/settings.txt`.

## Karty

- **System**: název a šířky sběrnic.
- **Registers**: fyzické registry a jejich podregistry. Barevný přehled bitů ukazuje obsazení i překryvy. Seznam lze filtrovat a položky duplikovat.
- **Memory**: architektura paměti, pořadí bajtů a segmenty s oprávněními.
- **ALU**: příznaky a operace definované výrazem nebo programem; operace mají vlastní latenci.
- **Instructions**: instrukce, jejich kódování a mikrooperace. Náhled ukazuje assemblerový tvar, operandy a výslednou latenci.
- **Problems**: diagnostika celé konfigurace. Kliknutí na problém přejde na příslušnou položku.
- **Peripherals**: periferie, registry, stav a chování při čtení, zápisu a tiku.
- **JSON**: přímá úprava dokumentu. **Apply JSON** použije text; **Reload from document** obnoví text z aktuálního dokumentu.

U složitějších polí zobrazí symbol `(?)` krátkou nápovědu. Stavový řádek shrnuje platnost konfigurace. Export a spuštění simulátoru jsou při neplatné konfiguraci zablokované.

Uložením se JSON znovu naformátuje s odsazením dvou mezer; pořadí klíčů a zalomení se mohou změnit.
