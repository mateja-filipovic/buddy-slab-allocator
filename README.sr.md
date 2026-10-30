<h1 align="center">Buddy-Slab alokator</h1>

<p align="center">
  Alokator memorije u stilu jezgra operativnog sistema, napisan u C-u: sistem ploča iznad sistema parnjaka.
</p>

<p align="center">
  <a href="README.md">English</a> | <b>Srpski</b>
</p>

<p align="center">
  <a href="#prevođenje">Prevođenje</a> ·
  <a href="#pokretanje">Pokretanje</a> ·
  <a href="#upotreba">Upotreba</a> ·
  <a href="#dizajn">Dizajn</a> ·
  <a href="#testovi">Testovi</a> ·
  <a href="#struktura-projekta">Struktura projekta</a>
</p>

---

Ovo je projekat sa predmeta Operativni sistemi 2 na Elektrotehničkom fakultetu Univerziteta u Beogradu. Alokator upravlja kontinualnim memorijskim prostorom koji mu se dodeli pri pokretanju, u blokovima od 4 KB. Sistem parnjaka (engl. buddy) vodi evidenciju o slobodnim blokovima, a sistem ploča (engl. slab) iznad njega dodeljuje objekte fiksne veličine iz keševa, sa opcionim konstruktorima i destruktorima, kao i male memorijske bafere veličine stepena dvojke kroz `kmalloc` i `kfree`. Javni interfejs u `inc/slab.h` prati zaglavlje dato uz postavku zadatka.

Projekat se prevodi i radi na macOS-u, Linux-u i Windows-u, a sadrži demo program i skup automatskih testova.

## Prevođenje

Potrebni su C prevodilac i CMake 3.16 ili noviji.

| Platforma | Prevodilac | Instalacija CMake-a |
|---|---|---|
| macOS | Xcode command line tools (`xcode-select --install`) | `brew install cmake` |
| Linux | gcc ili clang | `sudo apt install cmake` ili ekvivalent za vašu distribuciju |
| Windows | Visual Studio sa paketom Desktop development with C++ | `winget install Kitware.CMake` ili kroz Visual Studio installer |

```sh
git clone https://github.com/mateja-filipovic/Buddy-Slab-Allocator.git
cd Buddy-Slab-Allocator
cmake -S . -B build
cmake --build build
```

Na Windows-u CMake bira Visual Studio generator i izvršni fajlovi završavaju u `build/Debug/`. Folder se može otvoriti i direktno u Visual Studio-u ili CLion-u, oba prepoznaju `CMakeLists.txt` bez dodatnog podešavanja.

## Pokretanje

**Demo.** Kreira keš, alocira i oslobađa objekte, smanjuje keš i usput ispisuje njegovo stanje.

```sh
./build/demo            # macOS i Linux
build\Debug\demo.exe    # Windows
```

**Testovi.**

```sh
ctest --test-dir build --output-on-failure            # macOS i Linux
ctest --test-dir build --output-on-failure -C Debug   # Windows
```

## Upotreba

Alokatoru se jednom dodeli memorijski prostor, a zatim se kreiraju keševi za potrebne tipove objekata.

```c
#include "slab.h"

void *space = malloc(1000 * BLOCK_SIZE);
kmem_init(space, 1000);

kmem_cache_t *pcb_cache = kmem_cache_create("PCB", sizeof(struct pcb), pcb_ctor, pcb_dtor);

struct pcb *p = kmem_cache_alloc(pcb_cache);
kmem_cache_free(pcb_cache, p);

char *buf = kmalloc(300);   /* dolazi iz keša size-512 */
kfree(buf);

kmem_cache_shrink(pcb_cache);
kmem_cache_destroy(pcb_cache);
```

| Funkcija | Namena |
|---|---|
| `kmem_init(space, blocks)` | Inicijalizuje alokator nad `blocks` blokova od po `BLOCK_SIZE` bajtova počev od adrese `space` |
| `kmem_cache_create(name, size, ctor, dtor)` | Kreira keš za objekte veličine `size` bajtova; `ctor` i `dtor` mogu biti `NULL` |
| `kmem_cache_alloc(cache)` | Alocira jedan objekat |
| `kmem_cache_free(cache, obj)` | Vraća jedan objekat |
| `kmem_cache_shrink(cache)` | Oslobađa prazne ploče, vraća broj oslobođenih blokova |
| `kmem_cache_destroy(cache)` | Oslobađa sve ploče i sam keš |
| `kmalloc(size)` | Alocira mali bafer, zaokružen na stepen dvojke između 32 bajta i 128 KB |
| `kfree(ptr)` | Vraća mali bafer |
| `kmem_cache_info(cache)` | Ispisuje ime keša, veličinu objekta, broj blokova, broj ploča, broj objekata po ploči i popunjenost |
| `kmem_cache_error(cache)` | Ispisuje i vraća broj grešaka zabeleženih za keš |

Svaki poziv je thread-safe. Neispravna oslobađanja, dvostruka oslobađanja i alokacije koje ne mogu da se zadovolje prijavljuju se kroz `kmem_cache_error` umesto da obore program.

## Dizajn

**Sistem parnjaka.** Slobodni blokovi se čuvaju u listama po redu veličine, za svaki stepen dvojke po jedna. Alokacija uzima najmanji slobodan blok koji odgovara i deli ga naniže; oslobađanje spaja blok sa njegovim parnjakom sve dok je i parnjak slobodan. Prvi blok prostora čuva evidenciju samog sistema parnjaka.

**Keševi i ploče.** Svaki keš drži tri liste ploča: prazne, delimično popunjene i pune. Ploča je niz blokova čija je dužina stepen dvojke; na njenom početku su zaglavlje ploče i bit-vektor slobodnih mesta, a iza njih objekti. Alokacija prvo bira delimično popunjene ploče, zatim prazne, a novu ploču pravi samo kada nijedna od njih ne postoji. Uzastopne ploče počinju objekte na različitim pomerajima u odnosu na keš liniju, tako da se objekti iz različitih ploča ne nadmeću za iste linije L1 keša.

**Konstruktori i destruktori.** Konstruktor se izvršava jednom po mestu kada se ploča kreira. Oslobođen objekat se ostavlja takav kakav je, pa se od korisnika očekuje da ga vrati u inicijalizovanom stanju. Destruktor se izvršava nad svakim mestom kada se ploča oslobađa, bilo smanjivanjem keša ili njegovim uništavanjem.

**Smanjivanje.** `kmem_cache_shrink` oslobađa prazne ploče samo ako keš nije rastao od prethodnog smanjivanja. Prvi poziv posle rasta samo resetuje tu zastavicu i ne oslobađa ništa, čime se sprečava da keš stalno naizmenično raste i smanjuje se.

**Mali baferi.** `kmalloc` opslužuje veličine od 32 bajta do 128 KB iz trinaest keševa, po jednog za svaki stepen dvojke. `kfree` pronalazi ploču kojoj bafer pripada preko tabele koja preslikava blok u ploču, a koju alokator drži unutar dodeljenog prostora, pa pretraga nije potrebna.

**Memorija.** Alokator koristi isključivo prostor koji mu je dodeljen. Evidencija sistema parnjaka, tabela blokova, deskriptori keševa i ploče dolaze iz tog prostora.

**Sinhronizacija.** Jedan katanac štiti ceo alokator: semafor na Windows-u, a pthread mutex na ostalim sistemima. Deo zavisan od platforme je nekoliko linija na vrhu fajla `src/slab.c`.

## Testovi

Testovi se nalaze u `tests/` i pokreću se kroz ctest. Svaki test je samostalan program koji ispisuje `PASS` ili `FAIL` liniju sa fajlom i linijom provere koja nije prošla.

| Test | Proverava |
|---|---|
| `buddy_split` | Polovine nastale deljenjem ulaze u listu slobodnih sa ispravnom vezom i posle ranije upotrebe |
| `size_class` | Zaokruživanje na stepen dvojke za `kmalloc` i najveći red sistema parnjaka |
| `double_free` | Drugo oslobađanje istog objekta se odbija i beleži kao greška |
| `destructor` | Konstruktor i destruktor se izvršavaju tačno jednom po mestu |
| `failed_slab` | Ploča koja ne može da se alocira ostavlja prostor netaknutim |
| `shrink` | Zastavica rasta i broj blokova koji smanjivanje vraća |
| `cache_free` | Oslobađanje u pogrešan keš, neporavnati pokazivači i premeštanje ploča između lista |
| `stress` | 20 000 nasumičnih alokacija i oslobađanja preko keševa i `kmalloc`-a, uz proveru sadržaja |

Za pokretanje pod sanitajzerima dodajte `-fsanitize=address,undefined` prevodiocu, na primer `cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"`.

## Struktura projekta

```
inc/params.h    veličina bloka i keš linije
inc/buddy.h     sistem parnjaka
inc/cache.h     strukture ploče i keša, evidencija ploča
inc/slab.h      javni interfejs, kako je dat uz postavku zadatka
src/buddy.c     alokacija, oslobađanje i spajanje u sistemu parnjaka
src/cache.c     raspored ploče, bit-vektor, tabela blok-ploča, liste ploča
src/slab.c      javni interfejs, sinhronizacija, keševi malih bafera
src/main.c      demo
tests/          ctest testovi
```

## Ograničenja

- Jedan katanac serijalizuje sve pozive. Katanac po kešu dozvolio bi veći paralelizam.
- `kfree` prihvata samo bafere koji su došli iz `kmalloc`-a. Objekti iz korisničkih keševa vraćaju se kroz `kmem_cache_free`.
- Objekti su spakovani na svoju tačnu veličinu, pa veličine koje nisu umnožak poravnanja platforme daju neporavnate objekte.
- Imena keševa duža od 29 karaktera se skraćuju.
