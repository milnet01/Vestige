# Biblical Tabernacle (Tent of Meeting) - Complete Specifications

Comprehensive reference document for 3D modeling of the Tabernacle based on Exodus 25-40.
All dimensions are given in cubits first, with metric conversions using both the standard
cubit (~44.5 cm / ~17.5 in) and the long cubit (~52.5 cm / ~20.7 in).

---

## Table of Contents

1. [Cubit Conversion Reference](#1-cubit-conversion-reference)
2. [Overall Layout and Orientation](#2-overall-layout-and-orientation)
3. [Outer Courtyard](#3-outer-courtyard)
4. [The Tabernacle Structure (Tent)](#4-the-tabernacle-structure-tent)
5. [Boards/Frames and Silver Sockets](#5-boardsframes-and-silver-sockets)
6. [Crossbars](#6-crossbars)
7. [Four Layers of Covering](#7-four-layers-of-covering)
8. [The Veil (Paroketh)](#8-the-veil-paroketh)
9. [The Entrance Screen (Masak)](#9-the-entrance-screen-masak)
10. [Holy Place Dimensions](#10-holy-place-dimensions)
11. [Holy of Holies Dimensions](#11-holy-of-holies-dimensions)
12. [Furniture - Ark of the Covenant](#12-furniture---ark-of-the-covenant)
13. [Furniture - Table of Showbread](#13-furniture---table-of-showbread)
14. [Furniture - Golden Lampstand (Menorah)](#14-furniture---golden-lampstand-menorah)
15. [Furniture - Altar of Incense (Golden Altar)](#15-furniture---altar-of-incense-golden-altar)
16. [Furniture - Bronze Altar (Altar of Burnt Offering)](#16-furniture---bronze-altar-altar-of-burnt-offering)
17. [Furniture - Bronze Laver](#17-furniture---bronze-laver)
18. [Complete Materials List](#18-complete-materials-list)
19. [Gemstones of the High Priest's Breastplate](#19-gemstones-of-the-high-priests-breastplate)
20. [Scholarly Notes and Debates](#20-scholarly-notes-and-debates)
21. [Sources](#21-sources)

---

## 1. Cubit Conversion Reference

The "cubit" (Hebrew: ammah) was the primary unit of length in the biblical world, measured
from the elbow to the tip of the middle finger. There is scholarly debate about the exact length:

### Cubit Variants

| Cubit Type | Length (cm) | Length (m) | Length (in) | Length (ft) | Source |
|---|---|---|---|---|---|
| **Short/Common Hebrew Cubit** | ~44.5 cm | ~0.445 m | ~17.5 in | ~1.46 ft | Archaeological evidence from Israel |
| **Long/Royal Cubit (Egyptian)** | ~52.4 cm | ~0.524 m | ~20.6 in | ~1.72 ft | Surviving Egyptian cubit rods (523.5-529.2 mm) |
| **Talmudic Standard** | ~54 cm | ~0.54 m | ~21.3 in | ~1.77 ft | 6 handbreadths x ~9 cm each |
| **Rabbi Avraham Chaim Naeh** | 48 cm | 0.48 m | 18.9 in | 1.57 ft | Rabbinic authority |
| **Chazon Ish (Karelitz)** | 57.6 cm | 0.576 m | 22.7 in | 1.89 ft | Rabbinic authority |

### Structure of the Cubit

- **Common cubit:** 6 palms x 4 fingers = 24 digits
- **Royal cubit:** 7 palms x 4 fingers = 28 digits (one extra palm)
- **1 palm (handbreadth):** ~7.4 cm (common) to ~7.5 cm (royal)
- **1 finger (digit):** ~1.85 cm to ~1.87 cm

### Scholarly Consensus for the Tabernacle

Most biblical scholars and the Timna Park life-size replica use approximately **44.5 cm (17.5 in, ~1.5 ft)** per cubit for the Tabernacle, which is the short/common cubit. This is the most widely used value in scholarly reconstructions. Archaeological evidence from St. Etienne burial caves in Jerusalem confirms both the 44.5 cm and 52.5 cm cubits were in use during the biblical period. We still do not know definitively which cubit Moses used.

**For 3D modeling purposes, we will provide conversions using the standard cubit of 44.5 cm (0.445 m) as the primary conversion, with the long cubit of 52.5 cm (0.525 m) noted as an alternative.**

### Quick Conversion Table (Standard Cubit = 0.445 m)

| Cubits | Meters (std) | Meters (long) | Feet (std) |
|---|---|---|---|
| 1 | 0.445 | 0.525 | 1.46 |
| 1.5 | 0.668 | 0.788 | 2.19 |
| 2 | 0.890 | 1.050 | 2.92 |
| 2.5 | 1.113 | 1.313 | 3.65 |
| 3 | 1.335 | 1.575 | 4.38 |
| 4 | 1.780 | 2.100 | 5.84 |
| 5 | 2.225 | 2.625 | 7.30 |
| 10 | 4.450 | 5.250 | 14.60 |
| 15 | 6.675 | 7.875 | 21.90 |
| 20 | 8.900 | 10.500 | 29.20 |
| 28 | 12.460 | 14.700 | 40.88 |
| 30 | 13.350 | 15.750 | 43.80 |
| 50 | 22.250 | 26.250 | 73.00 |
| 100 | 44.500 | 52.500 | 146.00 |

---

## 2. Overall Layout and Orientation

**Verse Reference:** Exodus 27:9-19, Exodus 38:9-20, Numbers 3:38

### Orientation

- **The entrance faces EAST.** Worshippers entered facing west.
- This was deliberate -- in contrast to pagan sun worshippers who faced east toward the rising sun.
- The Holy of Holies was at the **west** end.

### Layout (East to West, as one would walk through)

```
                    NORTH
    +--------------------------------------------------+
    |                                                  |
    |   OUTER COURTYARD (100 x 50 cubits)              |
    |                                                  |
    |        [Bronze Altar]                            |
    |                                                  |
    |        [Bronze Laver]                            |
    |                                                  |
    |   +----------------------------------+           |
    |   |  TABERNACLE (30 x 10 cubits)     |           |
    |   |                                  |           |
    |   | ENTRANCE   HOLY PLACE   | HOLY   |           |
    |   | SCREEN     (20 x 10)    | OF     |           |
    |   | (east)                  | HOLIES |           |
    |   |                         | (10x10)|           |
    |   |  [Lampstand] [Incense]  | [Ark]  |           |
    |   |  (south)     (center)   |        |           |
    |   |  [Table]     [VEIL]     |        |           |
    |   |  (north)                |        |           |
    |   +----------------------------------+           |
    |                                                  |
    +--------------------------------------------------+
          GATE (east, 20 cubits wide)
                    SOUTH
```

### Furniture Placement Within (Exodus 40:1-33)

**Outer Courtyard (east half):**
- Bronze Altar of Burnt Offering -- near the entrance gate (Exodus 40:6, 40:29)
- Bronze Laver -- between the altar and the tent entrance (Exodus 40:7, 40:30)

**Holy Place (the larger front room, 20 x 10 cubits):**
- Table of Showbread -- on the **north** side (right side when facing west) (Exodus 40:22)
- Golden Lampstand -- on the **south** side (left side when facing west), opposite the table (Exodus 40:24)
- Altar of Incense (Golden Altar) -- in the **center**, in front of the veil (Exodus 40:5, 40:26)

**Holy of Holies (the inner room, 10 x 10 x 10 cubit cube):**
- Ark of the Covenant with Mercy Seat -- the sole item (Exodus 40:3, 40:20-21)

---

## 3. Outer Courtyard

**Verse Reference:** Exodus 27:9-19, Exodus 38:9-20

### Overall Dimensions

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length (N & S sides)** | 100 | 44.50 | 52.50 |
| **Width (E & W sides)** | 50 | 22.25 | 26.25 |
| **Height of hangings** | 5 | 2.23 | 2.63 |
| **Total area** | 5,000 sq cubits | ~990 sq m | ~1,378 sq m |

### Pillar/Post Count and Distribution

| Side | Length (cubits) | Number of Pillars | Number of Bronze Sockets | Spacing |
|---|---|---|---|---|
| **South** | 100 | 20 | 20 | 5 cubits apart |
| **North** | 100 | 20 | 20 | 5 cubits apart |
| **West** | 50 | 10 | 10 | 5 cubits apart |
| **East (gate side)** | 50 | 10 | 10 | see below |
| **TOTAL** | 300 | **60** | **60** | -- |

### East Side (Gate Side) Breakdown (Exodus 27:14-16)

- South of gate: 15 cubits of hangings, **3 pillars**, 3 sockets
- North of gate: 15 cubits of hangings, **3 pillars**, 3 sockets
- Gate/Screen: 20 cubits wide, **4 pillars**, 4 sockets
- Total east side: 50 cubits, 10 pillars, 10 sockets

### Pillar Construction

- **Material:** Acacia wood (implied from context)
- **Height:** 5 cubits (2.23 m / 2.63 m) -- same as hangings
- **Hooks:** Silver (Exodus 27:10)
- **Bands/Fillets:** Silver (Exodus 27:10)
- **Sockets/Bases:** Bronze (Exodus 27:10)
- **Caps/Tops:** Overlaid with silver (Exodus 38:17)

### Hangings (Curtain Walls)

- **Material:** Fine twisted linen (white) (Exodus 27:9)
- **Height:** 5 cubits (Exodus 27:18)
- **Color:** White (natural linen)

### The Gate (Exodus 27:16)

- **Width:** 20 cubits (8.90 m / 10.50 m)
- **Height:** 5 cubits (2.23 m / 2.63 m)
- **Area:** 100 square cubits
- **Material:** Blue, purple, and scarlet yarn with fine twisted linen
- **Workmanship:** The work of a weaver/embroiderer
- **Supported by:** 4 pillars on 4 bronze sockets

### Pegs/Stakes

- All pegs for the courtyard: **Bronze** (Exodus 27:19)

---

## 4. The Tabernacle Structure (Tent)

**Verse Reference:** Exodus 26:1-37, Exodus 36:8-38

### Overall External Dimensions

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length (E-W)** | 30 | 13.35 | 15.75 |
| **Width (N-S)** | 10 | 4.45 | 5.25 |
| **Height** | 10 | 4.45 | 5.25 |

Note: These are the interior dimensions based on 20 boards x 1.5 cubits = 30 cubits for the
long walls. The width is 10 cubits based on the boards (6 boards x 1.5 cubits = 9 cubits +
corner boards). There is scholarly debate about exact external dimensions depending on board
thickness assumptions (see Section 20).

### Internal Division

- **Holy Place:** 20 cubits long x 10 cubits wide x 10 cubits high (east/front portion)
- **Holy of Holies:** 10 cubits long x 10 cubits wide x 10 cubits high (west/rear portion)
- Separated by the **Veil** (Paroketh)

---

## 5. Boards/Frames and Silver Sockets

**Verse Reference:** Exodus 26:15-25, Exodus 36:20-34

### Board Dimensions

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Height** | 10 | 4.45 | 5.25 |
| **Width** | 1.5 | 0.668 | 0.788 |
| **Thickness** | Not specified | See scholarly debate | -- |

### Board Distribution

| Side | Number of Boards | Wall Length (cubits) |
|---|---|---|
| **South** | 20 | 30 (20 x 1.5) |
| **North** | 20 | 30 (20 x 1.5) |
| **West (rear)** | 6 + 2 corner = **8** | ~10 (see note) |
| **East (entrance)** | 0 (open, covered by screen) | -- |
| **TOTAL** | **48** | -- |

### Board Construction Details

- **Material:** Acacia wood, overlaid with gold (Exodus 26:29)
- **Tenons:** Each board had **2 tenons** (projections) at the base for fitting into sockets (Exodus 26:17)
- **Connection:** Boards set in order one against another, tenons fitting into silver sockets
- **Corner Boards (2):** For the two back corners; coupled together at bottom and top, each fitted into a single ring (Exodus 26:24). Scholarly debate exists on whether these were L-shaped or standard width.

### Silver Sockets (Adanim)

**Verse Reference:** Exodus 26:19-25, Exodus 38:27

| Component | Sockets | Silver per Socket |
|---|---|---|
| South wall (20 boards x 2) | 40 | 1 talent each |
| North wall (20 boards x 2) | 40 | 1 talent each |
| West wall (8 boards x 2) | 16 | 1 talent each |
| Veil pillars (4 pillars x 1) | 4 | 1 talent each |
| **TOTAL** | **100** | **100 talents of silver** |

- Each socket weighed **1 talent** (~34 kg / ~75 lb) (Exodus 38:27)
- Total silver for sockets: 100 talents
- Additional 1,775 shekels of silver were used for hooks, caps, and bands of the pillars (Exodus 38:28)

### Thickness Debate

The Bible does not specify board thickness. Scholarly positions:
- **Josephus:** 4 fingers thick (~7.4 cm / ~2.9 in)
- **Babylonian Talmud (Shabbat 98a):** 1 cubit thick (~44.5 cm)
- **Modern minimalist scholars:** Negligibly thin (structurally questionable for 10-cubit-tall boards)
- **For 3D modeling:** Josephus's 4 fingers (~7.5 cm) is a reasonable middle ground

---

## 6. Crossbars

**Verse Reference:** Exodus 26:26-30, Exodus 36:31-34

### Configuration

- **5 crossbars** for the south side frames
- **5 crossbars** for the north side frames
- **5 crossbars** for the west (rear) side frames
- **Total: 15 crossbars**

### Construction

- **Material:** Acacia wood, overlaid with gold (Exodus 26:29)
- **Rings:** Gold rings on the boards served as holders for the bars (Exodus 26:29)
- **The middle bar:** Ran from end to end through the center of the boards, halfway up (Exodus 26:28). On the long walls, this would be ~30 cubits (13.35 m) long.

### Arrangement

The Bible does not give specific dimensions for the bars. The middle bar of each wall ran the full length; the other four bars (two upper, two lower) presumably ran partial lengths. Some scholars suggest the other four bars were arranged as two pairs, each spanning half the wall length.

---

## 7. Four Layers of Covering

**Verse Reference:** Exodus 26:1-14, Exodus 36:8-19

The Tabernacle had four covering layers, from innermost (visible from inside) to outermost:

### Layer 1: Fine Linen Curtains (Innermost) -- Exodus 26:1-6

| Detail | Value |
|---|---|
| **Number of curtains** | 10 |
| **Individual curtain size** | 28 cubits long x 4 cubits wide |
| **Assembly** | Two sets of 5 curtains sewn together |
| **Each set dimensions** | 28 cubits x 20 cubits |
| **Total combined size** | **28 cubits x 40 cubits** (12.46 x 17.80 m std) |
| **Joining method** | 50 blue loops on each set edge, joined by 50 gold clasps |
| **Material** | Fine twisted linen with blue, purple, and scarlet yarn |
| **Design** | Cherubim woven into the fabric (artistic/skilled work) |

**Draping:** The 28-cubit width draped over the 10-cubit-wide, 10-cubit-high structure:
- 10 cubits across the roof
- 9 cubits down each side (leaving 1 cubit gap at the bottom on each side)

The 40-cubit length ran front to back:
- 10 cubits for the rear (west) wall, hanging down to the ground
- 30 cubits across the roof and remaining length

### Layer 2: Goat Hair Curtains -- Exodus 26:7-13

| Detail | Value |
|---|---|
| **Number of curtains** | 11 |
| **Individual curtain size** | 30 cubits long x 4 cubits wide |
| **Assembly** | One set of 5 + one set of 6 |
| **Total combined size** | **30 cubits x 44 cubits** (13.35 x 19.58 m std) |
| **Joining method** | 50 bronze clasps |
| **Material** | Goat hair (likely black or dark brown) |
| **Extra coverage** | 2 cubits wider and 4 cubits longer than linen layer |

**Draping:**
- 30 cubits wide (vs 28 for linen) = 1 extra cubit hanging down on each side
- 44 cubits long (vs 40 for linen) = extra coverage at front and rear
- The 6th curtain of the front set was doubled over at the front of the tent (Exodus 26:9)
- Extra 2 cubits at the rear hung over the back (Exodus 26:12)

### Layer 3: Ram Skins Dyed Red -- Exodus 26:14a

| Detail | Value |
|---|---|
| **Material** | Ram skins, dyed red (like morocco leather) |
| **Dimensions** | **Not specified in the Bible** |
| **Character** | Wool removed, tanned and dyed red; durable leather |

### Layer 4: Tachash Skins (Outermost) -- Exodus 26:14b

| Detail | Value |
|---|---|
| **Material** | "Tachash" skins (translation disputed) |
| **Dimensions** | **Not specified in the Bible** |
| **Translations** | Badger skins (KJV), sea cow/dugong skins, porpoise skins, fine leather |
| **Character** | Durable, waterproof outer protective layer |

---

## 8. The Veil (Paroketh)

**Verse Reference:** Exodus 26:31-33, Exodus 36:35-36

The veil separated the Holy Place from the Holy of Holies.

| Detail | Value |
|---|---|
| **Implied dimensions** | 10 cubits wide x 10 cubits high (matching the interior cross-section) |
| **Area** | 100 square cubits |
| **Material** | Blue, purple, and scarlet yarn with fine twisted linen |
| **Design** | Cherubim woven into the fabric (skilled work) |
| **Support** | **4 pillars** of acacia wood, overlaid with gold |
| **Pillar hooks** | Gold |
| **Pillar sockets** | **4 silver sockets** (1 talent each) |

---

## 9. The Entrance Screen (Masak)

**Verse Reference:** Exodus 26:36-37, Exodus 36:37-38

The screen covered the entrance (east side) of the Tabernacle.

| Detail | Value |
|---|---|
| **Implied dimensions** | 10 cubits wide x 10 cubits high (matching entrance opening) |
| **Area** | 100 square cubits |
| **Material** | Blue, purple, and scarlet yarn with fine twisted linen |
| **Design** | The work of a weaver/embroiderer (NOT cherubim -- simpler than the veil) |
| **Support** | **5 pillars** of acacia wood, overlaid with gold |
| **Pillar hooks** | Gold |
| **Pillar sockets** | **5 bronze sockets** (NOT silver -- unlike the veil) |

### Note on the Three Entrances

All three entrances (courtyard gate, tabernacle screen, veil) each had an area of **100 square cubits**:
- Courtyard gate: 20 cubits wide x 5 cubits high = 100 sq cubits
- Tabernacle entrance screen: 10 cubits wide x 10 cubits high = 100 sq cubits
- Veil: 10 cubits wide x 10 cubits high = 100 sq cubits

---

## 10. Holy Place Dimensions

**Verse Reference:** Derived from Exodus 26:33, the veil placement

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length (E-W)** | 20 | 8.90 | 10.50 |
| **Width (N-S)** | 10 | 4.45 | 5.25 |
| **Height** | 10 | 4.45 | 5.25 |

**Contains:**
- Table of Showbread (north side)
- Golden Lampstand/Menorah (south side)
- Altar of Incense (center, before the veil)

---

## 11. Holy of Holies Dimensions

**Verse Reference:** Exodus 26:33-34

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length (E-W)** | 10 | 4.45 | 5.25 |
| **Width (N-S)** | 10 | 4.45 | 5.25 |
| **Height** | 10 | 4.45 | 5.25 |

This is a **perfect cube** -- 10 x 10 x 10 cubits.

**Contains:**
- Ark of the Covenant with Mercy Seat (sole furnishing)

---

## 12. Furniture - Ark of the Covenant

**Verse Reference:** Exodus 25:10-22, Exodus 37:1-9

### The Ark (Chest)

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length** | 2.5 | 1.113 | 1.313 |
| **Width** | 1.5 | 0.668 | 0.788 |
| **Height** | 1.5 | 0.668 | 0.788 |

### Construction

- **Material:** Acacia wood, overlaid with pure gold **inside and out** (Exodus 25:11)
- **Gold molding/crown:** Around the top edge (Exodus 25:11)
- **Rings:** 4 gold rings, cast on the 4 feet/corners (Exodus 25:12)
- **Poles:** Acacia wood, overlaid with gold, inserted through the rings (Exodus 25:13-14)
- **Poles must never be removed** from the rings (Exodus 25:15)

### Contents of the Ark

- Two stone tablets of the Testimony/Law (Exodus 25:16, 40:20)
- A golden jar of manna (Exodus 16:33-34, Hebrews 9:4)
- Aaron's rod that budded (Numbers 17:10, Hebrews 9:4)

### The Mercy Seat (Kapporeth) -- Exodus 25:17-22

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length** | 2.5 | 1.113 | 1.313 |
| **Width** | 1.5 | 0.668 | 0.788 |

- **Material:** Pure gold (one piece of hammered/beaten work)
- Sits on top of the Ark as a lid/cover

### The Cherubim -- Exodus 25:18-20

- **Number:** 2, one at each end of the Mercy Seat
- **Material:** Gold, hammered work, of one piece with the Mercy Seat
- **Position:** Facing each other, faces turned toward the Mercy Seat
- **Wings:** Spread upward, overshadowing/covering the Mercy Seat
- **Specific dimensions:** Not given in the Bible
- **Location:** One at each end of the 2.5-cubit Mercy Seat

---

## 13. Furniture - Table of Showbread

**Verse Reference:** Exodus 25:23-30, Exodus 37:10-16

### Table Dimensions

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length** | 2 | 0.890 | 1.050 |
| **Width** | 1 | 0.445 | 0.525 |
| **Height** | 1.5 | 0.668 | 0.788 |

### Construction

- **Material:** Acacia wood, overlaid with pure gold (Exodus 25:24)
- **Gold molding/crown:** Around the top edge (Exodus 25:24)
- **Rim/Border:** A handbreadth wide (~7.5 cm / ~3 in) around the edge, with a gold molding on the rim (Exodus 25:25) -- this served as a raised edge to prevent items from falling off
- **Rings:** 4 gold rings at the 4 corners, near the legs (Exodus 25:26-27)
- **Poles:** Acacia wood, overlaid with gold, for carrying (Exodus 25:28)

### Vessels/Utensils (Exodus 25:29)

All made of pure gold:
- **Plates/Dishes** (for the bread)
- **Cups/Spoons** (for frankincense)
- **Pitchers/Flagons** (for drink offerings)
- **Bowls** (for drink offerings)

### The Showbread (Bread of the Presence) -- Leviticus 24:5-9

- **Number of loaves:** 12 (one for each tribe of Israel)
- **Arrangement:** 2 rows (or "piles/stacks") of 6 loaves each
- **Loaf material:** Fine flour
- **Each loaf:** Made from 2/10ths of an ephah of fine flour (Leviticus 24:5)
- **Frankincense:** Pure frankincense placed on each row/pile
- **Refreshed:** Every Sabbath day
- **Eaten by:** Aaron and his sons only, in a holy place

### Placement

- **Location:** North side of the Holy Place (Exodus 40:22)

---

## 14. Furniture - Golden Lampstand (Menorah)

**Verse Reference:** Exodus 25:31-40, Exodus 37:17-24

### Dimensions

**The Bible does NOT give specific height, width, or depth measurements for the Menorah.** Instead, God prescribed the amount of material: **1 talent of pure gold** (~34 kg / ~75 lb) for the lampstand and all its utensils (Exodus 25:39).

**Traditional/Scholarly Estimates:**
- **Height:** ~5 feet (1.5 m) -- based on Jewish tradition and the Talmud
- **Width:** ~3.5 feet (1.1 m) -- from branch tip to branch tip
- These are estimates, not biblical measurements.

### Structure

- **Central shaft (trunk):** The main vertical element
- **Branches:** 6 branches extending from the central shaft -- 3 on each side (Exodus 25:32)
- **Total lamp positions:** 7 (one on central shaft + one on each of 6 branches)
- **Construction:** One piece of hammered/beaten work of pure gold (Exodus 25:36)

### Decorative Elements (per branch) -- Exodus 25:33-34

Each of the 6 branches had:
- **3 cups** shaped like almond blossoms, each with a bulb (knob/capital) and a flower

The central shaft had:
- **4 cups** shaped like almond blossoms, each with a bulb and flower

### Utensils (Exodus 25:38)

- **Tongs/Snuffers:** Pure gold
- **Trays/Snuff dishes:** Pure gold

### Total Weight

- **1 talent of pure gold** for the lampstand AND all utensils (Exodus 25:39)
- 1 talent approximately = 34 kg (75 lb)

### Placement

- **Location:** South side of the Holy Place (Exodus 40:24), opposite the Table of Showbread

### Oil

- Pure beaten olive oil (Exodus 27:20)
- Lamps tended by Aaron from evening to morning continually (Exodus 27:21)

---

## 15. Furniture - Altar of Incense (Golden Altar)

**Verse Reference:** Exodus 30:1-10, Exodus 37:25-28

### Dimensions

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length** | 1 | 0.445 | 0.525 |
| **Width** | 1 | 0.445 | 0.525 |
| **Height** | 2 | 0.890 | 1.050 |
| **Shape** | Square | -- | -- |

### Construction

- **Material:** Acacia wood, overlaid with pure gold -- top, sides, and horns (Exodus 30:3)
- **Gold molding/crown:** Around the top (Exodus 30:3)
- **Horns:** 4 horns, one at each corner, of one piece with the altar (Exodus 30:2)
- **Rings:** 2 gold rings, under the molding on two opposite sides (Exodus 30:4)
- **Poles:** Acacia wood, overlaid with gold (Exodus 30:5)

### Function

- Aaron burned fragrant incense on it every morning and every evening (Exodus 30:7-8)
- Once a year: atonement with blood of the sin offering on its horns (Exodus 30:10)
- Called "most holy to the LORD" (Exodus 30:10)

### Placement

- **Location:** Center of the Holy Place, in front of the veil, before the Ark (Exodus 40:5, 40:26)

---

## 16. Furniture - Bronze Altar (Altar of Burnt Offering)

**Verse Reference:** Exodus 27:1-8, Exodus 38:1-7

### Dimensions

| Measurement | Cubits | Meters (std) | Meters (long) |
|---|---|---|---|
| **Length** | 5 | 2.225 | 2.625 |
| **Width** | 5 | 2.225 | 2.625 |
| **Height** | 3 | 1.335 | 1.575 |
| **Shape** | Square | -- | -- |

### Construction

- **Material:** Acacia wood, overlaid with bronze (Exodus 27:2)
- **Interior:** Hollow with boards (Exodus 27:8)
- **Horns:** 4 horns, one at each corner, of one piece with the altar (Exodus 27:2)

### Bronze Grating/Network (Exodus 27:4-5)

- A **grating of bronze network** under the rim of the altar
- Positioned **halfway** up the altar (at the midpoint, ~1.5 cubits from the ground)
- **4 bronze rings** at the four corners of the grating (Exodus 27:4)

### Carrying Apparatus

- **Poles:** Acacia wood, overlaid with bronze (Exodus 27:6)
- Inserted through the rings on two sides (Exodus 27:7)

### Utensils (Exodus 27:3)

All made of bronze:
- **Pails/Pans** -- for removing ashes
- **Shovels**
- **Basins/Bowls** -- for receiving blood
- **Forks/Flesh hooks**
- **Firepans/Censers**

### Placement

- **Location:** Outer courtyard, in front of the entrance to the Tabernacle (Exodus 40:6, 40:29)

---

## 17. Furniture - Bronze Laver

**Verse Reference:** Exodus 30:17-21, Exodus 38:8

### Dimensions

**The Bible gives NO specific dimensions for the Bronze Laver.** This is unique among the tabernacle furnishings -- all other items have specific measurements.

### Construction

- **Material:** Bronze, made from the **mirrors of the serving women** who assembled at the doorway of the tent of meeting (Exodus 38:8)
- **Components:** A basin (laver) and a base/pedestal (stand), both of bronze (Exodus 30:18)
- **Shape:** The Hebrew word "kiyor" (basin) and "ken" (base) suggest a large urn or vase shape on a pedestal. Likely round based on the Hebrew term "kikkar."

### Scholarly Estimates for Size

While no biblical dimensions exist, various reconstructions (including the Timna Park model) estimate:
- **Height (total with base):** ~3 feet (0.9 m)
- **Basin diameter:** ~2-3 feet (0.6-0.9 m)
- These are scholarly estimates, NOT biblical specifications.

### Function

- Priests (Aaron and his sons) washed their hands and feet before:
  - Entering the tent of meeting (Exodus 30:20)
  - Approaching the altar to minister (Exodus 30:20)
- Failure to wash = death (Exodus 30:20-21)

### Placement

- **Location:** Outer courtyard, between the tent of meeting and the bronze altar (Exodus 30:18, 40:30)

---

## 18. Complete Materials List

**Verse Reference:** Exodus 25:3-7, Exodus 35:4-9, Exodus 38:24-31

### Metals

| Metal | Usage | Quantity (where recorded) |
|---|---|---|
| **Gold** | Ark, Mercy Seat, Table, Lampstand, Altar of Incense, board overlays, rings, clasps, hooks | 29 talents + 730 shekels (Exodus 38:24) |
| **Silver** | Sockets for boards and veil, pillar hooks/caps/bands, courtyard pillar overlays | 100 talents + 1,775 shekels (Exodus 38:25-28) |
| **Bronze (Copper)** | Bronze Altar, Bronze Laver, courtyard sockets, pegs/stakes, clasps for goat-hair curtains | 70 talents + 2,400 shekels (Exodus 38:29) |

### Wood

- **Acacia wood** (Hebrew: shittim) -- the only wood specified
  - Used for: Ark, Table, boards/frames, crossbars, Altar of Incense, Bronze Altar, all poles/staves, entrance pillars, veil pillars, courtyard pillars

### Fabrics and Threads

| Material | Color/Description | Usage |
|---|---|---|
| **Blue yarn** (tekhelet) | Blue-violet, from murex shellfish dye | Veil, screens, curtains, priestly garments |
| **Purple yarn** (argaman) | Purple-red, from murex snail dye (Phoenician) | Veil, screens, curtains, priestly garments |
| **Scarlet yarn** (tola'at shani) | Bright red, from coccus ilicis worm | Veil, screens, curtains, priestly garments |
| **Fine twisted linen** (shesh) | White, Egyptian-quality linen | Curtains, hangings, priestly garments |
| **Goat hair** | Dark (black/brown), coarse | Second covering layer (11 curtains) |

### Skins/Hides

| Material | Description | Usage |
|---|---|---|
| **Ram skins dyed red** | Tanned leather, dyed red | Third covering layer |
| **Tachash skins** | Debated: badger, sea cow, dugong, porpoise, or fine leather | Fourth (outermost) covering layer |

### Oil and Spices

| Material | Usage | Verse |
|---|---|---|
| **Olive oil** (pure, beaten) | Lampstand fuel, anointing oil base | Exodus 27:20 |
| **Spices for anointing oil** | Myrrh, cinnamon, calamus, cassia | Exodus 30:23-25 |
| **Spices for incense** | Stacte, onycha, galbanum, pure frankincense | Exodus 30:34 |

### Gemstones

| Material | Usage | Verse |
|---|---|---|
| **Onyx stones** (2) | Set in the ephod shoulder pieces, engraved with 12 tribe names | Exodus 25:7, 28:9-12 |
| **Setting stones** (12) | For the high priest's breastplate, one per tribe | Exodus 25:7, 28:17-20 |

---

## 19. Gemstones of the High Priest's Breastplate

**Verse Reference:** Exodus 28:17-20, Exodus 39:10-13

The breastplate held 12 stones in 4 rows of 3, each engraved with a tribe name.

**Note:** The exact identification of ancient Hebrew gemstone names is debated among scholars. Different Bible translations use different English names. The Hebrew names are listed here with common translations.

| Row | Position | Hebrew Name | Common Translation (KJV/NKJV) | Alternative IDs |
|---|---|---|---|---|
| **Row 1** | 1 | Odem | Sardius/Ruby | Carnelian |
| **Row 1** | 2 | Pitdah | Topaz | Peridot, Chrysolite |
| **Row 1** | 3 | Bareketh | Carbuncle/Emerald | Beryl, Green feldspar |
| **Row 2** | 4 | Nophek | Emerald | Turquoise, Garnet |
| **Row 2** | 5 | Sappir | Sapphire | Lapis lazuli |
| **Row 2** | 6 | Yahalom | Diamond | Moonstone, Onyx |
| **Row 3** | 7 | Leshem | Ligure/Jacinth | Opal, Amber |
| **Row 3** | 8 | Shevo | Agate | Agate |
| **Row 3** | 9 | Achlamah | Amethyst | Amethyst |
| **Row 4** | 10 | Tarshish | Beryl/Chrysolite | Aquamarine, Yellow jasper |
| **Row 4** | 11 | Shoham | Onyx | Malachite, Sardonyx |
| **Row 4** | 12 | Yashpheh | Jasper | Green jasper |

Each stone was set in a gold filigree setting (Exodus 28:20).

---

## 20. Scholarly Notes and Debates

### Board Thickness and Interior Dimensions

The Bible gives height (10 cubits) and width (1.5 cubits) for each board but **no thickness**. This omission has generated centuries of scholarly debate, as the thickness directly affects the internal dimensions:

- **If boards are thin (~4 fingers / 7.5 cm per Josephus):** Interior space is essentially 30 x 10 cubits
- **If boards are 1 cubit thick (Talmud, Shabbat 98a):** Interior space would be reduced to 28 x 8 cubits
- **The Timna Park replica and most modern reconstructions** use relatively thin boards

### Corner Board Configuration

Three scholarly positions exist for the 2 corner boards at the west wall:
1. **Standard width boards (Propp):** Add 1.5 cubits each to the rear wall
2. **Overlapping pieces (Rabbinic tradition):** Add only 0.5 cubits each
3. **L-shaped corner pieces (Homan):** Contribute to both long and short walls

### Frame vs. Solid Wall Debate

Scholars debate whether the Hebrew word "qeresh" (traditionally "board/plank") means:
- **Solid planks** -- making solid walls with fabric draped over them
- **Open frames** -- making a lattice-like structure that the fabric could be seen through from inside

The frame interpretation was popularized by archaeologist A.R.S. Kennedy and has gained wide acceptance, as solid planks would be extremely heavy and would obscure the beautiful cherubim-woven inner linen curtains.

### Curtain Draping

The precise method of draping the rectangular curtains over the rectangular structure creates geometric challenges at corners. Propp notes this creates an "ill-fitting garment" effect. Some scholars suggest the curtains were cut into a cruciform shape rather than left rectangular, though this is not stated in the Bible.

### Menorah Height

The Bible gives no height for the Menorah -- only the weight (1 talent of pure gold). The Talmud (Menachot 28b) specifies 18 handbreadths (approximately 3 cubits or ~1.34 m with the standard cubit). Other Jewish traditions suggest approximately 5 feet (1.5 m). The Arch of Titus in Rome depicts a menorah from the Second Temple (not the Tabernacle), providing some visual reference.

### Cubit Used for the Tabernacle

The Bible does not specify which cubit was used. Most scholars default to the shorter common cubit (~44.5 cm) for the wilderness Tabernacle, as the longer royal cubit (~52.5 cm) is more associated with later construction (Solomon's Temple may have used the longer cubit -- see 2 Chronicles 3:3's reference to "cubits of the old standard"). This remains debated.

---

## 21. Sources

### Primary Biblical References (by Exodus chapter)

| Chapter | Content |
|---|---|
| **Exodus 25:1-9** | Materials list and offerings |
| **Exodus 25:10-22** | Ark of the Covenant and Mercy Seat |
| **Exodus 25:23-30** | Table of Showbread |
| **Exodus 25:31-40** | Golden Lampstand (Menorah) |
| **Exodus 26:1-6** | Linen curtains (1st covering) |
| **Exodus 26:7-13** | Goat hair curtains (2nd covering) |
| **Exodus 26:14** | Ram skins and tachash skins (3rd & 4th coverings) |
| **Exodus 26:15-25** | Boards, tenons, silver sockets |
| **Exodus 26:26-30** | Crossbars |
| **Exodus 26:31-33** | The Veil |
| **Exodus 26:36-37** | The Entrance Screen |
| **Exodus 27:1-8** | Bronze Altar |
| **Exodus 27:9-19** | Outer Courtyard |
| **Exodus 27:20-21** | Oil for the Lampstand |
| **Exodus 28:17-20** | Breastplate gemstones |
| **Exodus 30:1-10** | Altar of Incense |
| **Exodus 30:17-21** | Bronze Laver |
| **Exodus 30:23-25** | Anointing oil recipe |
| **Exodus 30:34-38** | Incense recipe |
| **Exodus 35-39** | Construction account (parallel to 25-30) |
| **Exodus 38:24-31** | Metal quantities used |
| **Exodus 40:1-33** | Assembly and furniture placement |
| **Leviticus 24:5-9** | Showbread arrangement |

### Web Sources Consulted

- [CATHOLIC ENCYCLOPEDIA: Tabernacle](https://www.newadvent.org/cathen/14424b.htm)
- [Exodus 27:9-21 - The Court of the Tabernacle (Superior Word)](https://superiorword.org/exodus-27-9-21-the-court-of-the-tabernacle/)
- [Dimensions of the Tabernacle (Bible Students Daily)](https://biblestudentsdaily.com/tag/dimensions-of-the-tabernacle/)
- [Cubit - Wikipedia](https://en.wikipedia.org/wiki/Cubit)
- [Measurements in the Bible - Evidence at St. Etienne (Biblical Archaeology Society)](https://library.biblicalarchaeology.org/sidebar/measurements-in-the-bible-evidence-at-st-etienne-for-the-length-of-the-cubit-and-the-reed/)
- [The Cubit: A History and Measurement Commentary (Journal of Anthropology, 2014)](https://onlinelibrary.wiley.com/doi/10.1155/2014/489757)
- [Exodus 26 - Study Guide by David Guzik (Blue Letter Bible)](https://www.blueletterbible.org/comm/guzik_david/study-guide/exodus/exodus-26.cfm)
- [Diagram of the Tabernacle and Basic Layout (Good Seed)](https://www.goodseed.com/diagram-of-the-tabernacle-and-basic-layout.html)
- [Tabernacle Coverings Study (Bible Students Daily)](https://biblestudentsdaily.com/2017/01/02/study-8-the-tabernacle-coverings/)
- [Building the Tabernacle in Your Mind (TheTorah.com)](https://www.thetorah.com/article/building-the-tabernacle-in-your-mind)
- [Timna Park Tabernacle Replica (HolyLandSite.com)](https://www.holylandsite.com/timna-tabernacle)
- [The Tabernacle Replica at Timna (Danny The Digger)](https://dannythedigger.com/tabernacle/)
- [Tabernacle Model (BiblePlaces.com)](https://www.bibleplaces.com/tabernacle/)
- [Exodus 27:1-8 - The Brazen Altar (Superior Word)](https://superiorword.org/exodus-27-1-8-the-brazen-altar/)
- [What was the bronze laver? (GotQuestions.org)](https://www.gotquestions.org/bronze-laver.html)
- [Bronze Laver - Wikipedia](https://en.wikipedia.org/wiki/Bronze_laver)
- [Mercy Seat - Wikipedia](https://en.wikipedia.org/wiki/Mercy_seat)
- [Showbread - Wikipedia](https://en.wikipedia.org/wiki/Showbread)
- [Exodus 25:31-40 - Menorah Commentary (TheBibleSays.com)](https://thebiblesays.com/en/commentary/exo+25:31)
- [What Were the Gemstones of Aaron's Breastplate? (International Gem Society)](https://www.gemsociety.org/article/what-were-the-gemstones-of-the-breastplate-of-aaron/)
- [7 Classes of Furniture of the Holy Tabernacle (Agape Bible Study)](https://www.agapebiblestudy.com/charts/7%20Classes%20of%20furniture%20of%20the%20Holy%20Tabernacle.htm)
- [Exodus 25:10-22 Commentary (TheBibleSays.com)](https://thebiblesays.com/en/commentary/exod/exod-25/exodus-2510-22)
- [Solomonic Cubits (Armstrong Institute)](https://armstronginstitute.org/1021-solomonic-cubits)
- [Tabernacle - Wikipedia](https://en.wikipedia.org/wiki/Tabernacle)

---

## Quick Reference: All Dimensions Summary Table

| Item | L (cubits) | W (cubits) | H (cubits) | L (m, std) | W (m, std) | H (m, std) | Verse |
|---|---|---|---|---|---|---|---|
| **Outer Courtyard** | 100 | 50 | 5 (fence) | 44.50 | 22.25 | 2.23 | Ex 27:9-18 |
| **Courtyard Gate** | 20 (wide) | -- | 5 | 8.90 | -- | 2.23 | Ex 27:16 |
| **Tabernacle (overall)** | 30 | 10 | 10 | 13.35 | 4.45 | 4.45 | Ex 26:15-25 |
| **Holy Place** | 20 | 10 | 10 | 8.90 | 4.45 | 4.45 | Ex 26:33 |
| **Holy of Holies** | 10 | 10 | 10 | 4.45 | 4.45 | 4.45 | Ex 26:33 |
| **Ark of the Covenant** | 2.5 | 1.5 | 1.5 | 1.11 | 0.67 | 0.67 | Ex 25:10 |
| **Mercy Seat** | 2.5 | 1.5 | -- | 1.11 | 0.67 | -- | Ex 25:17 |
| **Table of Showbread** | 2 | 1 | 1.5 | 0.89 | 0.45 | 0.67 | Ex 25:23 |
| **Lampstand (Menorah)** | -- | -- | -- | ~1.1 est. | -- | ~1.5 est. | Ex 25:31-39 |
| **Altar of Incense** | 1 | 1 | 2 | 0.45 | 0.45 | 0.89 | Ex 30:1-2 |
| **Bronze Altar** | 5 | 5 | 3 | 2.23 | 2.23 | 1.34 | Ex 27:1 |
| **Bronze Laver** | -- | -- | -- | Not specified | -- | -- | Ex 30:18 |
| **Boards (each)** | -- | 1.5 | 10 | -- | 0.67 | 4.45 | Ex 26:16 |

### Metal Totals (Exodus 38:24-31)

| Metal | Talents | Shekels | Approx. Weight |
|---|---|---|---|
| **Gold** | 29 | 730 | ~1,000 kg (2,200 lb) |
| **Silver** | 100 | 1,775 | ~3,420 kg (7,540 lb) |
| **Bronze** | 70 | 2,400 | ~2,400 kg (5,290 lb) |
