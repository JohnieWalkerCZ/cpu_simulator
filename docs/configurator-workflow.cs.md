# Grafický konfigurátor: postup tvorby procesoru

[English](configurator-workflow.md)

Tento postup navazuje na [ovládání konfigurátoru](configurator-guide.cs.md). Pro první seznámení otevřete `configs/8bit.json` a uložte kopii pomocí **Save As**.

## 1. Systém, registry a paměť

Na kartě **System** nastavte název a šířky datové a adresové sběrnice. Na kartě **Registers** přidejte registry a přiřaďte odpovídající role programovému čítači, zásobníku a příznakům. Podregistry mohou vybírat souvislý rozsah pomocí offsetu nebo jednotlivé bity pomocí masky. Překryvy zkontrolujte na bitovém přehledu.

Na kartě **Memory** zvolte společnou nebo oddělenou instrukční a datovou paměť, pořadí bajtů a segmenty. U každého segmentu nastavte rozsah a oprávnění R, W a X. Podrobnosti a omezení polí jsou v [referenci konfigurace](../CONFIGURATION.cs.md).

## 2. ALU a instrukce

Na kartě **ALU** definujte příznaky a operace. Operace může používat výraz nebo program; její latence určuje počet taktů operace. Vstupy `a`, `b` a `c` se připojí argumenty mikrooperace instrukce.

Na kartě **Instructions** přidejte instrukci, nastavte opcode a kódování a přidejte mikrooperace. Editor synchronizuje opcode s první položkou kódování. Po výběru akce nabízí odpovídající argumenty, registry a operandy; pro literál nebo vlastní hodnotu lze zvolit příslušnou možnost. Při změně akce se odstraní argumenty, které k nové akci nepatří.

Náhled instrukce zobrazuje operandy, mikrooperace a časování. Režimy latence jsou `dynamic`, `strict`, `bottleneck` a `additive`. Bez nastavené instrukční latence se použije `dynamic`. Pro přesný význam polí a argumentů použijte [referenci mikrooperací](../CONFIGURATION.md#53-microop).

## 3. Periferie

Na kartě **Peripherals** přidejte zařízení a jeho registry. Parametry a vnitřní stav se upravují jako řádky `key = value`. Chování `on_read`, `on_write` a `tick_behavior` lze upravit v režimu **Blocks** nebo **Text**.

V blokovém editoru vložte `if`, `assign` nebo `call` z palety; bloky lze přetahovat i přesouvat tlačítky. Podmínka může obsahovat větve then a else. Neznámé typy uzlů zůstávají zachované a zobrazují se šedě; upravit je lze v textovém režimu. Tlačítka příkladů vloží ukázkovou periferii nebo háček.

## 4. Kontrola a spuštění

Otevřete **Problems**, projděte hlášené chyby a kliknutím přejděte na odpovídající pole. Po opravě uložte soubor a použijte **Launch Simulator**. V simulátoru lze konfiguraci vyzkoušet s assemblerovým programem a sledovat registry, paměť, mikrooperace a periferie.
