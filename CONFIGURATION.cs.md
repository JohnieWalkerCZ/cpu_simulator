# Konfigurace simulátoru procesoru

Český přehled souborů JSON, kterými se popisuje procesor, instrukční sada, mikroprogramy a periferie. Názvy klíčů a hodnoty určené simulátoru zůstávají v angličtině; v příkladech je proto neměňte ani nepřekládejte. Úplný anglický popis je v [CONFIGURATION.md](CONFIGURATION.md). Hlavní návod k sestavení a použití je v [README.cs.md](README.cs.md).

## 1. Základní struktura

Konfigurace je kořenový objekt JSON. Běžně obsahuje `name`, `data_bus`, `address_bus`, `memory`, `registers`, `alu`, `instruction_set` a volitelně `peripherals`.

```json
{
  "name": "ExampleISA",
  "data_bus": { "width": 8 },
  "address_bus": { "width": 16 },
  "memory": { "size": 65536 },
  "registers": { "general_purpose": [], "special": [] },
  "alu": { "flags": [], "operations": [] },
  "instruction_set": { "instructions": [] },
  "peripherals": []
}
```

`name` je popisný název. `data_bus.width` a `address_bus.width` jsou šířky sběrnic v bitech; povolené jsou násobky 4 od 4 do 128. Datová šířka ovlivňuje výchozí šířku ALU a přístupů k registrům, adresová šířka určuje rozsah adres a velikost polí typu `address` v kódování.

## 2. Paměť

Objekt `memory` může obsahovat:

- `size` — velikost hlavní datové paměti v bajtech.
- `architecture` — `von_neumann` (výchozí, společná paměť) nebo `harvard` (oddělená instrukční a datová paměť).
- `endianness` — `little` (nejméně významný bajt na nejnižší adrese) nebo `big`.
- `segments` — pole oblastí s vlastními oprávněními. Bez něj se použije jeden segment pokrývající paměť.

Segment obsahuje `name`, `start`, `end`, `R`, `W` a `X`. Adresy mohou být celá čísla nebo řetězce, například `"0x8000"`. `R`, `W` a `X` povolují čtení, zápis a vykonávání. Nedovolený přístup vyvolá chybu paměti.

## 3. Registry

`registers.general_purpose` a `registers.special` jsou pole definic registrů. Každý registr má `name`, `width` a volitelně `initial`. Speciální registr může mít `role`: `program_counter`/`pc`, `stack_pointer`/`sp` nebo `status_flags`/`flags`.

Podregistry se zapisují rekurzivně do `sub_registers`. Každý má vlastní `name` a `width`, a buď `offset` pro souvislý výřez bitů, nebo `mask` (celé číslo či řetězec) pro výběr bitů. Při použití masky musí šířka podregistru odpovídat počtu jedničkových bitů masky. Zápis do podregistru mění pouze odpovídající bity rodiče.

Definice registru může také obsahovat `is_coproc`, `coproc_id` a `coproc_reg_id`, pokud je registr součástí koprocesoru.

## 4. ALU a příznaky

`alu.flags` definuje příznaky ve stavovém registru `$FLAGS`. Příznak má `name`, index bitu `bit` (číslováno od nuly) a volitelný `type` nebo `expression`.

`alu.operations` obsahuje operace ALU. Každá operace má `name` a buď `expression`, nebo `program`; může mít také `code`, `flag_rules` a `latency` (počet taktů, výchozí hodnota 1). Výraz pracuje s operandy `a`, `b`, `c`; výsledek se použije v šířce určené výstupem mikrooperace. Podporovány jsou aritmetické, bitové, posunové, porovnávací (`==`, `<`, …) a logické (`&&`, `||`) operátory.

`flag_rules` mapuje názvy příznaků na pravidla pro konkrétní operaci: `result_zero`, `result_negative`, `carry_add`, `carry_sub`, `overflow_add`, `overflow_sub`, vlastní logický výraz používající `res`, nebo prázdný řetězec pro příznak beze změny.

### Program ALU

Pole `program` může být pole řádků nebo jeden víceřádkový řetězec. Mini-jazyk podporuje přiřazení, `if`/`else`, `while`, `for name in start..end`, `return` a komentáře `//`. Kromě `a`, `b`, `c` lze číst `width`. Program běží jako jedna ALU operace; `latency` určuje její viditelnou délku. Bez `return` vrací nulu. Smyčka je omezena na milion kroků.

```json
{
  "name": "MUL",
  "program": [
    "acc = 0;",
    "for i in 0..width {",
    "  if ((b >> i) & 1) { acc = acc + (a << i); }",
    "}",
    "return acc;"
  ],
  "flag_rules": { "Z": "result_zero" }
}
```

## 5. Instrukční sada a mikroprogramy

`instruction_set.instructions` je pole instrukcí. Volitelné `opcode_width` určuje šířku úvodního operačního kódu (výchozí 8 bitů, u užší sběrnice šířka sběrnice; rozsah 1–16 a nejvýše šířka datové sběrnice).

Instrukce má `name`, `opcode`, `encoding` a `microcode`; volitelně také `latency` a `latency_mode`.

- `encoding` je posloupnost číselných konstant a tokenů operandů. Běžné registry: `dest`, `src`, `addr_reg`; okamžité hodnoty: `imm8`, `imm16`, `address`, `offset`, `imm`. První konstanta zpravidla odpovídá operačnímu kódu. Pole `address` má šířku adresové sběrnice. Celé zakódované slovo nesmí překročit 128 bitů.
- `latency_mode`: `dynamic` (sekvenční mikrooperace se zohledněním latence ALU), `strict`/`fixed` (přesný rozpočet taktů, slučování nebo doplnění operací), `bottleneck`/`max` (maximum instrukční latence a skutečné délky operací) nebo `additive` (pevná režie po mikrooperacích). Bez nastavené instrukční `latency` se režim nepoužije a načte se `dynamic`.
- `microcode` je posloupnost objektů s `action` a argumenty. Dostupné akce: `copy`, `alu`, `mem_read`, `mem_write`, `port_read`, `port_write`, `coproc_read`, `coproc_write`, `branch`, `halt`.

Argumenty mikrooperace se zapisují jako vlastnosti stejného objektu (např. `source`, `dest`, `op`, `a`, `b`, `out`, `addr`, `data`). Odkazy na registry mají podobu `$R0`, `$PC`, `$SP` nebo `$FLAGS`; `#42` označuje číselný literál. `@dest` odkazuje na operand instrukce, například registr nebo adresu. Přesné argumenty závisí na `action`; doporučujeme použít průvodce v grafickém editoru.

## 6. Periferie mapované do paměti

Volitelné pole `peripherals` deklaruje zařízení MMIO. Periferie obvykle definuje `name`, adresní rozsah, parametry, `state`, registry a háčky `on_read`, `on_write` nebo `tick_behavior`. Registry zařízení mohou obsahovat vlastní adresu, šířku a háček pro čtení či zápis. Přesný tvar adresních polí a registrů vychází z dodaných konfigurací a anglické referenční příručky.

Háčky používají jednoduchý AST: podmíněné uzly `if`, přiřazení `assign` a volání `call`. Výrazy podporují aritmetické a bitové operátory, porovnání, logiku, závorky, literály v desítkové, šestnáctkové (`0x...`) a binární (`0b...`) podobě a identifikátory registrů či stavových proměnných. Ve výrazech je dostupné `value` (při čtení aktuální hodnota, při zápisu zapisovaná hodnota; při `tick_behavior` není k dispozici).

Mezi vestavěná volání ve výrazech patří `sys_read(addr)` a `host_pop_char()`. Uzly `call` mohou spouštět vedlejší účinky, například `trigger_interrupt`, `sys_write` a `host_print`. Vstupy a přesné parametry se liší podle typu periferie; použijte příklad v `configs/` a zachovejte strukturu jeho JSON.

## 7. Grafický editor konfigurací

Program `cpu_configurator` lze sestavit cílem `cpu_configurator` a spustit bez argumentu (nová šablona) nebo s cestou ke konfiguraci:

```bash
./build/cpu_configurator configs/8bit.json
```

Editor nabízí karty System, Registers, Memory, ALU, Instructions, Problems a Peripherals a také zobrazení surového JSON. Změny validuje stejnými pravidly jako simulátor; export a spuštění simulátoru jsou při neplatné konfiguraci zakázané.

Instrukční karta zobrazuje assemblerový tvar, kódování, mikrooperace a výslednou latenci. Průvodce mikrooperacemi ukazuje argumenty podle zvolené akce. Diagnostika uvádí problém i návrh opravy a kliknutím přejde na odpovídající položku. Periferní háčky lze editovat textově nebo blokově. Další funkce zahrnují podregistry s přehledem bitů, undo/redo (až 50 kroků), potvrzení mazání a opuštění neuložených změn, duplikování položek, klávesové zkratky a seznam posledních souborů.

Editor ukládá JSON s odsazením dvou mezer; pořadí klíčů a zalomení řádků se mohou změnit, hodnoty dokumentu zůstávají zachovány. Při použití grafického editoru se řiďte také nápovědou u polí a validací v aplikaci.
