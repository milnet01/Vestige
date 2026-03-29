# Tabernacle 3D Research Document

**Date:** 2026-03-27
**Purpose:** Practical research for building a real-time 3D Tabernacle (Tent of Meeting) in the Vestige engine (OpenGL 4.5)

---

## 1. Existing 3D Models and References

### 1.1 Sketchfab (Best Free Source)

**The Desert Tabernacle Collection** by `thedeserttabernacle` -- the single best free resource. A complete collection of 9 individual models, all CC BY-NC 4.0:

| Model | Triangles | Vertices | Free? | License |
|-------|-----------|----------|-------|---------|
| Biblical Tabernacle Mishkan (complete) | 243.5k | 137.9k | Yes | CC BY-NC 4.0 |
| Lampstand Menorah | 45.8k | 17.1k | Yes | CC BY-NC 4.0 |
| Ark Of The Covenant: Alternative | - | - | Yes | CC BY-NC 4.0 |
| The Ten Commandments | - | - | Yes | CC BY-NC 4.0 |
| Framework Of The Tabernacle Tent | - | - | Yes | CC BY-NC 4.0 |
| Altar Of Burnt Offering | - | - | Yes | CC BY-NC 4.0 |
| Table Of Shewbread | - | - | Yes | CC BY-NC 4.0 |
| Altar Of Incense | - | - | Yes | CC BY-NC 4.0 |
| Copper Laver | - | - | Yes | CC BY-NC 4.0 |

- Collection URL: https://sketchfab.com/thedeserttabernacle/collections/the-desert-tabernacle-mishkan-a96af8e601aa4d0083ac669908acde66
- All Sketchfab downloads include glTF export automatically
- **IMPORTANT:** CC BY-NC 4.0 means non-commercial only. If shipping on Steam, these cannot be used directly -- they are reference/study models only.

**Davide Specchi Collection** (13 models including Ark, Menorah, Table of Showbread, Brazen Altar, Incense Altar):
- https://sketchfab.com/Davide.Specchi/collections/tabernacle-720f7f5afa914cf286de5aee1f21212a
- Check individual licenses per model

**Ark of the Covenant** by VHM777 -- 205.2k tris, 103k verts, CC BY 4.0 (commercial OK with attribution):
- https://sketchfab.com/3d-models/the-ark-of-the-covenant-d8fb87c24f3f40edaf564d770101552c
- 1,864 downloads, well-reviewed

**Tabernacle** by bibel-in-3d.de -- 2.7M triangles, 1.4M vertices:
- https://sketchfab.com/3d-models/tabernacle-57e4cd4ad59a45c78c8e1b81c7c23d9b
- Very high poly, would need decimation for real-time use
- License not confirmed -- check before use

**Tabernacle** by jamethy -- simpler model:
- https://sketchfab.com/3d-models/tabernacle-2c9627a1b3334e0c8fb48078376b94c9

### 1.2 CGTrader

**Furniture of the Tabernacle of Moses** -- $16 for collection of 4 pieces (each $3 individually):
- Bronze Lavers, Menorahs, Incense Altar, Table of Showbread
- Formats: BLEND, OBJ, FBX, glTF/GLB, DAE, STL
- Made in Blender 3.01-3.5.1
- Low-poly, real-world scale, includes albedo/specular/normal maps
- UV-unwrapped, non-overlapping
- URL: https://www.cgtrader.com/3d-model-collections/furniture-of-the-tabernacle-of-moses

### 1.3 Other Sources

- **TurboSquid**: Free Altar of Incense in OBJ/MAX formats: https://www.turbosquid.com/Search/3D-Models/free/tabernacle
- **Cults3D**: Very cheap STL models ($1-2 each) designed for 3D printing, would need conversion: https://cults3d.com/en/tags/tabernacle
- **Ancient Bible Models**: STL files for Incense Table, Showbread Table, Copper Altar: https://www.ancientbiblemodels.com/product-page/tabernacle-3dfiles-for-3d-printer

### 1.4 Existing Interactive Projects (Study References)

- **Immersive Tabernacle App** (iOS/Android) -- open-world walkthrough, scholarly research, animations. Best existing reference for what a first-person Tabernacle experience looks like: https://immersivehistory.com/tabernacle-of-moses-a-walk-through-exodus/
- **Tabernacle of Moses VR** (Meta Quest) -- VR experience with hundreds of info points and tour guide
- **Moses' Tabernacle VR Simulator** by TRYHARD STUDIOS (itch.io) -- free, Windows/Android
- **Virtual Reality Bible Museum** -- walk through the Tabernacle: https://vrbm.org/

### 1.5 Recommendation

For a commercial Steam product, the best approach is:
1. Use the free Sketchfab models as **reference/study** to understand proportions and layout
2. Purchase the CGTrader collection ($16) for real assets with proper licensing
3. Model remaining pieces yourself or commission them, using the references
4. The Ark by VHM777 (CC BY 4.0) can be used commercially with attribution

---

## 2. Reference Images and Reconstructions

### 2.1 Timna Park Replica (Israel)

The single best physical reference. A life-sized replica in the Negev desert, 20 miles north of Eilat.

- **Official site:** https://www.parktimna.co.il/en/227/
- **Dedicated tabernacle site:** https://tabernacle.co.il/
- Accurate in dimensions, colors, and layout (not original materials)
- Includes exact models of: altar, copper sink, table of showbread, menorah, ark of the covenant, priestly vestments
- Built in a desert setting that matches the biblical context
- **360-degree panoramas** of the interior available at: https://www.360cities.net/image/tabernacleholyplace

### 2.2 BiblePlaces.com

- **195 high-resolution photographs** of the Timna tabernacle model
- Free for educational use, generous copyright policy
- Includes detail shots of individual furniture pieces
- URL: https://www.bibleplaces.com/tabernaclemore/
- PhotoShelter gallery: https://bibleplaces.photoshelter.com/gallery/Tabernacle-Model/G0000M.NcWAvpIhs/

### 2.3 FreeBibleimages (Jeremy Park / Bible-Scenes.com)

Detailed 3D rendered scenes of every Tabernacle component, free under CC BY 4.0:

- Complete walkthrough: https://www.freebibleimages.org/illustrations/bs-tabernacle-walkthrough/
- Tabernacle structure (boards/frames): https://www.freebibleimages.org/illustrations/bs-tabernacle-structure/
- Courtyard: https://www.freebibleimages.org/illustrations/bs-tabernacle-courtyard/
- Four coverings: https://www.freebibleimages.org/illustrations/bs-tabernacle-covering/
- Lampstand (Menorah): https://freebibleimages.org/illustrations/bs-tabernacle-lampstand/
- Table of Showbread: https://www.freebibleimages.org/illustrations/bs-tabernacle-table/
- Altar: https://www.freebibleimages.org/illustrations/bs-tabernacle-altar/

### 2.4 Scholarly Diagrams

- **Bethany Bible Church** -- detailed architectural analysis with diagrams: https://www.bethanybiblechurch.com/2017/08/exodus-26-the-architecture-of-the-tabernacle/
- **The Tabernacle Man (Dr. Terry Harman)** -- extremely detailed scholarly research on each component: https://www.thetabernacleman.com/
- **Amazing Sanctuary** -- individual furniture analysis with scripture references: https://www.amazingsanctuary.com/the-furniture-of-the-tabernacle/

---

## 3. Dimensions Reference (for 3D Modeling)

### 3.1 Cubit Conversion

There is no absolute scholarly consensus. The two main values:
- **Standard/Common cubit:** ~45.7 cm (18 inches) -- most widely used
- **Royal/Long cubit:** ~52.5 cm (20.6 inches) -- mentioned in Ezekiel

**Recommendation for the engine:** Use 45.7 cm per cubit (0.457 m). This is the most commonly accepted value and matches the majority of reconstructions including Timna Park. All dimensions below use this value.

### 3.2 Outer Courtyard

| Dimension | Cubits | Meters | Feet |
|-----------|--------|--------|------|
| Length | 100 | 45.7 | 150 |
| Width | 50 | 22.85 | 75 |
| Curtain height | 5 | 2.285 | 7.5 |

- Entrance on the east side, 20 cubits wide (9.14m)
- Perimeter curtains: fine white linen on bronze posts
- 60 bronze posts with silver hooks and bands

### 3.3 Tabernacle Structure (The Tent)

| Dimension | Cubits | Meters | Feet |
|-----------|--------|--------|------|
| Length | 30 | 13.71 | 45 |
| Width | 10 | 4.57 | 15 |
| Height | 10 | 4.57 | 15 |

- **Holy Place:** 20 x 10 x 10 cubits (9.14 x 4.57 x 4.57 m)
- **Holy of Holies:** 10 x 10 x 10 cubits -- a perfect cube (4.57 x 4.57 x 4.57 m)
- Entrance faces east
- 48 acacia wood frames (qereshim), each 10 cubits tall x 1.5 cubits wide
- Frames overlaid with gold, set in silver sockets
- 5 crossbars per side (one continuous middle bar)

### 3.4 Furniture Dimensions

**Ark of the Covenant** (Holy of Holies) -- Exodus 25:10-22

| Dimension | Cubits | Meters | Feet |
|-----------|--------|--------|------|
| Length | 2.5 | 1.14 | 3.75 |
| Width | 1.5 | 0.685 | 2.25 |
| Height | 1.5 | 0.685 | 2.25 |

- Acacia wood overlaid with pure gold (inside and out)
- Gold crown/molding around the top
- Gold mercy seat (kapporeth) on top, same footprint
- Two gold cherubim on the mercy seat, wings outstretched and touching
- Two gold-covered carrying poles through gold rings

**Table of Showbread** (Holy Place, north side) -- Exodus 25:23-30

| Dimension | Cubits | Meters | Feet |
|-----------|--------|--------|------|
| Length | 2 | 0.914 | 3 |
| Width | 1 | 0.457 | 1.5 |
| Height | 1.5 | 0.685 | 2.25 |

- Acacia wood overlaid with pure gold
- Gold crown/molding around the top
- Carried 12 loaves in two stacks of 6

**Golden Lampstand / Menorah** (Holy Place, south side) -- Exodus 25:31-40

- No explicit width/height dimensions given in scripture
- Rabbinic sources: 18 handbreadths (3 cubits) high = ~1.37m (4.5 ft)
- Made of one talent of pure gold (~34 kg / 75 lbs)
- 7 branches: central shaft + 3 pairs curving upward
- Each branch decorated with 3 cups shaped like almond blossoms (cup, knob, flower)
- Central shaft: 4 sets of almond blossom decorations

**Altar of Incense** (Holy Place, before the veil) -- Exodus 30:1-10

| Dimension | Cubits | Meters | Feet |
|-----------|--------|--------|------|
| Length | 1 | 0.457 | 1.5 |
| Width | 1 | 0.457 | 1.5 |
| Height | 2 | 0.914 | 3 |

- Square, acacia wood overlaid with pure gold
- Four horns on the corners
- Gold crown/molding around the top

**Bronze Altar / Altar of Burnt Offering** (Courtyard) -- Exodus 27:1-8

| Dimension | Cubits | Meters | Feet |
|-----------|--------|--------|------|
| Length | 5 | 2.285 | 7.5 |
| Width | 5 | 2.285 | 7.5 |
| Height | 3 | 1.371 | 4.5 |

- Square, acacia wood overlaid with bronze
- Four horns on the corners
- Bronze grating/grate inside
- Carried by bronze-covered acacia poles

**Bronze Laver** (Courtyard, between altar and tent) -- Exodus 30:18-21

- **No dimensions given in scripture** -- unique among all Tabernacle furnishings
- Made from bronze mirrors of the serving women (Exodus 38:8)
- Reasonable estimate: ~0.6-0.9m diameter basin on a pedestal ~0.9-1.2m tall

### 3.5 Four Coverings (Roof Layers, Inside to Outside)

1. **Inner curtain:** Fine linen, blue/purple/scarlet with embroidered cherubim
   - 10 curtains, each 28 x 4 cubits, joined in two sets of 5
   - Total: ~12.8 x 9.14m
2. **Goat hair covering:** 11 curtains, each 30 x 4 cubits
   - Slightly larger than the inner curtain (overlaps)
3. **Ram skins dyed red:** No dimensions given
4. **Outer covering (tachash/dugong/badger skins):** No dimensions given

---

## 4. Textures Needed and Sources

### 4.1 PBR Texture Sources (All Free, CC0)

| Source | URL | License | Notes |
|--------|-----|---------|-------|
| Poly Haven | https://polyhaven.com/textures | CC0 | Up to 8K, complete PBR maps |
| ambientCG | https://ambientcg.com | CC0 | 2000+ materials, up to 8K |
| FreePBR | https://freepbr.com | Free, commercial OK | 2048x2048, metalness/roughness workflow |
| 3D Textures | https://3dtextures.me | CC0 | Includes displacement maps |
| ShareTextures | https://www.sharetextures.com | CC0 | Up to 4K |
| TextureCan | https://www.texturecan.com | CC0 | Good metal selection |
| cgbookcase | https://www.cgbookcase.com | CC0 | Bronze textures available |

### 4.2 Material-Specific Texture Plan

**Acacia Wood (boards, furniture bases)**
- Poly Haven: wood textures section (warm brown hardwoods) -- https://polyhaven.com/textures/wood
- Poliigon: "Acacia Wood Board Texture, Warm Brown" -- https://www.poliigon.com/texture/acacia-wood-board-texture-warm-brown/3691 (check if free tier)
- ambientCG: Wood026 and similar warm brown wood textures -- https://ambientcg.com/list?q=wood
- Key properties: warm golden-brown, visible grain, moderate roughness (0.4-0.6)

**Gold (Ark, Menorah, Altar of Incense overlay, Table overlay)**
- FreePBR: "Hammered Gold" -- https://freepbr.com/product/hammered-gold-pbr/ (perfect for beaten gold overlay)
- FreePBR: "Scuffed Gold" -- https://freepbr.com/product/scuffed-gold-pbr-metal-material/ (for worn areas)
- PBR values for gold: Base Color RGB approximately (1.0, 0.77, 0.31), metallic = 1.0, roughness = 0.2-0.5
- For beaten/hammered gold: increase roughness to 0.3-0.6, use normal map for hammer marks

**Bronze (Altar, Laver, courtyard posts, tent pegs)**
- FreePBR: "Bronze PBR Material" -- https://freepbr.com/product/bronze-pbr-material/
- aiTextured: "Aged Bronze" with patina/verdigris -- https://aitextured.com/textures/metal/aged-bronze-texture-seamless.html
- cgbookcase: "Bronze 01" -- https://www.cgbookcase.com/textures/bronze-01
- PBR values for bronze: Base Color RGB approximately (0.93, 0.62, 0.52) -- between copper and brass, metallic = 1.0, roughness = 0.3-0.5
- For polished bronze: roughness 0.15-0.3
- For weathered bronze: shift color toward greenish, roughness 0.5-0.8

**Silver (sockets for boards)**
- PBR values: Base Color RGB approximately (0.99, 0.99, 0.97), metallic = 1.0, roughness = 0.2-0.4

**Fine Linen (inner curtain, courtyard hangings)**
- Poly Haven: fabric textures -- https://polyhaven.com/textures/fabric
- ambientCG: Fabric019 and similar -- https://ambientcg.com/view?id=Fabric019
- FreePBR: cloth/fabric category -- https://freepbr.com/c/cloth-fabric/
- Base: off-white/cream color with visible weave pattern
- Key properties: high roughness (0.7-0.9), non-metallic, subtle normal map for weave

**Embroidered Linen (inner curtain with cherubim)**
- No off-the-shelf texture exists for this. Approach:
  1. Start with a base linen texture
  2. Create custom diffuse/albedo map with blue, purple, and scarlet cherubim pattern painted over it
  3. Generate a custom normal/height map to give embroidery raised-thread appearance
  4. Use Parallax Occlusion Mapping (POM) for close-up depth illusion
- Colors per scripture: blue (sky/heavenly), purple (royalty), scarlet (sacrifice/blood)
- The exact cherubim pattern is not specified in scripture; Josephus described it as "a panorama of the heavens"

**Goat Hair Cloth (second tent covering)**
- Use coarse woven fabric texture in dark brown/black
- Bedouin tents use blended camel/goat hair: tightly woven, three-ply textile
- Properties: high roughness (0.8-0.95), non-metallic, strong normal map for coarse weave
- Add micro-fiber fuzz via roughness variation for softness around folds

**Ram Skins Dyed Red (third covering)**
- Use red leather/morocco leather texture
- Poly Haven or FreePBR leather textures, tinted to deep red-brown
- Properties: moderate roughness (0.4-0.7), non-metallic, visible grain pattern

**Tachash/Dugong Skins (outer covering)**
- Identity disputed: dugong, seal, porpoise, or "fine leather"
- Use a dark, tough leather texture -- grayish-blue to dark brown
- Scholarly consensus leans toward a marine animal hide: tough, waterproof, dull color
- Properties: high roughness (0.6-0.9), non-metallic, thick hide texture

**Desert Sand (ground terrain)**
- Poly Haven: sand terrain section -- https://polyhaven.com/textures/terrain/sand
- ambientCG: Ground037 and similar -- https://ambientcg.com/view?id=Ground037
- Properties: very high roughness (0.85-1.0), warm tan/beige, displacement map for dunes/ripples

### 4.3 HDRI Environment Maps

For the desert exterior lighting:
- **Poly Haven Namaqualand/Goegap** -- 16K, clear desert sun, hard shadows, warm sandy tones: https://polyhaven.com/a/goegap
- Poly Haven outdoor HDRIs: https://polyhaven.com/hdris/outdoor
- All CC0, available in HDR and EXR, up to 16K resolution
- Key characteristics: intense directional sunlight, clear blue sky, warm ground bounce

---

## 5. 3D Modeling Approaches

### 5.1 The Menorah (Golden Lampstand)

The Menorah is the most geometrically complex piece. Modeling approaches:

**Approach A: Lathe/Revolution + Boolean (Recommended for quality)**
- Model the central shaft profile as a 2D spline curve, lathe/revolve it
- Model one branch as a curve, lathe its cross-section along the curve path
- Mirror the 3 branch pairs
- Model individual almond blossom decorations (cup + knob + flower) as separate pieces:
  - Cup: small bowl shape via lathe
  - Knob/bulb: sphere or egg shape
  - Flower: 5-6 petal shapes arranged radially, slight curve
- Place 3 decorations per branch (22 total: 3x6 branches + 4 on shaft)
- Apply gold PBR material

**Approach B: Use an existing model as base**
- Download a reference menorah (Sketchfab thedeserttabernacle has one at 45.8k tris)
- Re-topology if needed to optimize for real-time
- Replace materials with proper PBR gold

**Polygon budget:** Aim for 20k-50k triangles for the menorah. The almond blossoms add detail but can use normal maps for fine detail rather than geometry.

### 5.2 The Ark of the Covenant

**Structure:**
- Box: simple rectangular mesh, beveled edges, gold material
- Crown/molding: extruded ring around the top edge
- Carrying poles: cylinders through ring attachments on corners
- Mercy seat: flat slab matching the box top

**Cherubim on the Mercy Seat:**
- Most complex sub-element
- Reference: ancient Near Eastern art (Megiddo ivories, Byblos sarcophagus) shows winged figures
- Modeling: sculpt or use subdivision surface modeling for organic forms
- Wings should extend upward and toward center, tips nearly touching
- Faces looking toward each other and down toward the mercy seat
- Target: 5k-15k triangles per cherub
- The VHM777 Ark model on Sketchfab (CC BY 4.0, 205k tris) includes cherubim and is commercially usable with attribution

### 5.3 Curtains and Fabric

For the Tabernacle, curtains are **static set dressing**, not interactive physics objects. This is critical for 60 FPS performance.

**Recommended approach:**
1. Model curtain shapes in Blender using cloth simulation to get natural draping
2. Apply simulation, freeze the result as a static mesh
3. Export the baked mesh (no physics at runtime)
4. Apply fabric PBR textures with:
   - Diffuse/albedo for color and pattern
   - Normal map for weave texture and fold detail
   - Roughness map for fabric sheen variation
   - Optional: Parallax Occlusion Mapping for embroidery depth

**For the inner curtain with cherubim:**
- Model the curtain mesh with gentle draping folds
- Paint custom albedo texture with blue/purple/scarlet cherubim motifs
- Create matching normal/height maps so the embroidery appears raised
- The cherubim pattern is not precisely defined biblically, giving artistic freedom
- Josephus described it as depicting "a panorama of the heavens"

**For the courtyard linen hangings:**
- These are taut/flat between posts -- simple flat or very slightly billowing planes
- White linen texture, minimal displacement

**Performance note:** Static meshes with PBR textures are essentially free in terms of GPU cost. No cloth simulation = no per-frame overhead. Budget ~5k-10k triangles per curtain section.

### 5.4 The Board/Frame Structure

- 48 identical boards (qereshim) can be instanced -- model one board, instance 48 times
- Each board: 10 cubits tall (4.57m) x 1.5 cubits wide (0.685m)
- Simple rectangular shape with gold overlay
- Two tenons at the base fitting into silver sockets
- Crossbars connecting them: 5 per side, one continuous middle bar
- Very low poly -- each board can be as few as 8-20 triangles
- GPU instancing makes 48+ boards trivial for performance

---

## 6. Lighting Considerations

### 6.1 Exterior -- Desert Environment

**Sun (Directional Light):**
- Intense, harsh directional light from above
- Color temperature: ~5500-6000K (bright warm white)
- Produces hard, defined shadows
- Direction: for biblical accuracy, set time to mid-morning or mid-afternoon in a Sinai desert latitude (~29 degrees N)

**Ambient/Environment:**
- Use a desert HDRI for IBL (image-based lighting): Poly Haven Goegap recommended
- Warm sandy ground bounce (light reflecting off desert sand)
- Clear blue sky contributes cool fill light from above
- Very high contrast ratio between sun and shadow

**Implementation:**
- Directional light + shadow mapping (already in engine)
- HDRI cubemap for ambient/IBL
- Sand terrain should have high albedo contributing to ground bounce

### 6.2 Holy Place Interior

The Holy Place had **no windows**. The only light source was the seven-branched Menorah.

**Menorah as Light Source:**
- 7 individual point lights (one per oil lamp on each branch)
- Or: 1-3 point lights as an approximation for performance
- Color: warm orange-gold, approximately (1.0, 0.7, 0.3) -- olive oil flame color
- Attenuation: use quadratic falloff, effective radius ~5-8 meters
- The room is only 9.14 x 4.57m, so the menorah would illuminate it reasonably well
- Menorah sits on the south side -- light falls more strongly on the south wall and toward the center

**Flame Rendering:**
- Billboard particles for each of the 7 flames (textured quads facing camera)
- Animated UV offset using Perlin noise for flicker
- Emissive material on the flame sprite (contributes to bloom)
- Subtle random intensity variation on the point lights for flicker effect

**Atmospheric Effects:**
- Subtle dust motes (particle system, very sparse, illuminated by the menorah)
- Possible light bloom/glow around the flames using HDR + bloom post-processing
- LearnOpenGL bloom tutorial: https://learnopengl.com/Advanced-Lighting/Bloom

**Color grading:** Warm tones, slight orange tint. The gold surfaces would catch and reflect the warm lamplight beautifully.

### 6.3 Holy of Holies

The Holy of Holies had **no light source at all**. It was entered once per year by the High Priest.

**Design Options:**

**Option A: Total darkness (historically accurate)**
- No lights in the room
- Player enters near-complete darkness
- Only light leaks through the veil from the Holy Place (very faint, diffused)
- This is atmospherically powerful but may be frustrating for gameplay

**Option B: Shekinah Glory (theologically significant)**
- The Shekinah (God's visible presence) appeared as luminous cloud above the Ark
- Implement as a soft, warm-white volumetric light centered above the mercy seat
- Techniques:
  - Volumetric fog/light scattering with a point or spot light
  - Post-process god rays (screen-space radial blur) -- GPU Gems 3 technique
  - Emissive bloom around the mercy seat area
  - Color: warm white with slight golden tint, (1.0, 0.95, 0.85)
- NVIDIA GPU Gems reference: https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process
- OpenGL implementation: https://github.com/math-araujo/screen-space-godrays (OpenGL 4.5)

**Option C: Gameplay compromise**
- Very dim ambient light so the player can see the Ark and cherubim
- Perhaps triggered: room starts dark, then the Shekinah manifests as the player approaches

**Recommendation:** Option C gives the best player experience -- the transition from darkness to divine light is dramatic and memorable. Start in near-darkness with only veil-filtered light, then gradually manifest the Shekinah as the player enters.

### 6.4 Lighting Implementation Priorities

For the Vestige engine's existing renderer:

1. **Directional light** -- already implemented (sun/shadows)
2. **Point lights** -- needed for Menorah (7 flames) and potentially Shekinah
3. **HDR + Bloom** -- needed for flame glow and Shekinah effect
4. **Volumetric lighting** -- optional enhancement for Shekinah and dust motes
5. **HDRI/IBL** -- for outdoor environment ambient lighting

---

## 7. PBR Material Reference Values

### 7.1 Metal Values (from physicallybased.info)

| Metal | Base Color (Linear RGB) | Metallic | Roughness Range |
|-------|------------------------|----------|-----------------|
| Gold | (1.0, 0.77, 0.31) | 1.0 | 0.2-0.5 |
| Copper | (0.93, 0.62, 0.52) | 1.0 | 0.2-0.5 |
| Bronze* | ~(0.90, 0.65, 0.45) | 1.0 | 0.3-0.6 |
| Silver | (0.99, 0.99, 0.97) | 1.0 | 0.2-0.4 |
| Brass | (0.91, 0.78, 0.42) | 1.0 | 0.3-0.5 |
| Iron | (0.53, 0.51, 0.49) | 1.0 | 0.4-0.7 |

*Bronze is not in the standard database; value interpolated between copper and brass.

### 7.2 Non-Metal Values

| Material | Base Color | Metallic | Roughness |
|----------|-----------|----------|-----------|
| White linen | (0.85, 0.82, 0.78) | 0.0 | 0.7-0.9 |
| Acacia wood | (0.55, 0.35, 0.15) | 0.0 | 0.4-0.6 |
| Red-dyed leather | (0.45, 0.12, 0.08) | 0.0 | 0.5-0.7 |
| Dark hide (tachash) | (0.15, 0.13, 0.12) | 0.0 | 0.6-0.9 |
| Goat hair cloth | (0.12, 0.10, 0.08) | 0.0 | 0.8-0.95 |
| Desert sand | (0.76, 0.60, 0.42) | 0.0 | 0.85-1.0 |

---

## 8. Asset Production Pipeline Recommendation

### Phase 1: Layout and Blockout
1. Build courtyard walls as simple planes with white linen texture
2. Place bronze post cylinders at correct spacing
3. Block out the tent structure with placeholder boards
4. Position placeholder boxes for each furniture piece at correct dimensions
5. Verify all measurements against the cubit table

### Phase 2: Terrain and Environment
1. Create desert sand terrain with PBR sand texture from Poly Haven
2. Apply desert HDRI (Goegap from Poly Haven) for skybox and IBL
3. Set up directional light for desert sun with shadow mapping

### Phase 3: Structure
1. Model one acacia board, instance 48 times (GPU instancing)
2. Add crossbars
3. Apply gold-overlaid-wood material (wood base with gold overlay areas)
4. Add silver sockets at bases
5. Drape the four coverings as pre-baked static meshes

### Phase 4: Major Furniture
1. Ark of the Covenant (consider starting with VHM777 CC BY model, or model from scratch)
2. Bronze Altar (simple geometry, bronze material)
3. Bronze Laver (lathe-modeled basin and pedestal)
4. Table of Showbread (box with legs, gold overlay, bread loaves on top)
5. Altar of Incense (box with horns, gold overlay)
6. Menorah (most complex -- lathe + ornamental detail)

### Phase 5: Lighting
1. Implement point lights for the 7 Menorah flames
2. Add flame billboard particles
3. Implement HDR + bloom for flame glow
4. Set up Holy of Holies lighting (Shekinah effect)
5. Fine-tune exterior desert lighting

### Phase 6: Polish
1. Dust mote particles in interior
2. Embroidered curtain detail textures
3. Priestly garment details if showing characters
4. Audio ambience (wind, flame crackle)

---

## Sources

### 3D Models
- [The Desert Tabernacle Collection (Sketchfab)](https://sketchfab.com/thedeserttabernacle/collections/the-desert-tabernacle-mishkan-a96af8e601aa4d0083ac669908acde66)
- [Biblical Tabernacle Mishkan (Sketchfab)](https://sketchfab.com/3d-models/biblical-tabernacle-mishkan-41d3c771c13a4cbcbc10353536ffec91)
- [Ark of the Covenant by VHM777 (Sketchfab)](https://sketchfab.com/3d-models/the-ark-of-the-covenant-d8fb87c24f3f40edaf564d770101552c)
- [Lampstand Menorah (Sketchfab)](https://sketchfab.com/3d-models/lampstand-menorah-of-the-tabernacle-5a3ae34b9f744163bb63222577f5e238)
- [Davide Specchi Tabernacle Collection (Sketchfab)](https://sketchfab.com/Davide.Specchi/collections/tabernacle-720f7f5afa914cf286de5aee1f21212a)
- [Tabernacle by bibel-in-3d.de (Sketchfab)](https://sketchfab.com/3d-models/tabernacle-57e4cd4ad59a45c78c8e1b81c7c23d9b)
- [Furniture of the Tabernacle of Moses (CGTrader)](https://www.cgtrader.com/3d-model-collections/furniture-of-the-tabernacle-of-moses)
- [Altar of Incense by PrayInc (Sketchfab)](https://sketchfab.com/3d-models/the-altar-of-incense-0ba655228851494f9f934e0955f8b2e6)
- [Cherub from Ark (Sketchfab)](https://sketchfab.com/3d-models/cherub-from-the-ark-of-the-covenant-b2713372c17b415aa49b36ba1ed0ab2d)

### Reference Images and Reconstructions
- [Timna Park Tabernacle (Official)](https://www.parktimna.co.il/en/227/)
- [Timna Park Tabernacle (tabernacle.co.il)](https://tabernacle.co.il/)
- [360-degree Panorama of Tabernacle Interior (360Cities)](https://www.360cities.net/image/tabernacleholyplace)
- [BiblePlaces.com Tabernacle Photos](https://www.bibleplaces.com/tabernaclemore/)
- [FreeBibleimages Tabernacle Walkthrough](https://www.freebibleimages.org/illustrations/bs-tabernacle-walkthrough/)
- [The Tabernacle Man (Dr. Terry Harman)](https://www.thetabernacleman.com/)
- [Amazing Sanctuary](https://www.amazingsanctuary.com/the-furniture-of-the-tabernacle/the-ark-of-the-covenant)

### Dimensions and Scholarly Sources
- [Tabernacle Architecture (Bethany Bible Church)](https://www.bethanybiblechurch.com/2017/08/exodus-26-the-architecture-of-the-tabernacle/)
- [Weights and Lengths of Moses' Tabernacle](http://www.altriocchi.com/H_ENG/pen4/ttm_eng/weights_lenghts.html)
- [Dimensions in the Tabernacle and Temple (Torah.org)](https://torah.org/learning/dimensions-in-the-tabernacle-and-temple/)
- [Cubit History (Wiley)](https://onlinelibrary.wiley.com/doi/10.1155/2014/489757)
- [Cubit (Wikipedia)](https://en.wikipedia.org/wiki/Cubit)
- [Exodus 25 (USCCB)](https://bible.usccb.org/bible/exodus/25)

### PBR Textures
- [Poly Haven Textures](https://polyhaven.com/textures)
- [ambientCG](https://ambientcg.com/)
- [FreePBR Hammered Gold](https://freepbr.com/product/hammered-gold-pbr/)
- [FreePBR Scuffed Gold](https://freepbr.com/product/scuffed-gold-pbr-metal-material/)
- [FreePBR Bronze](https://freepbr.com/product/bronze-pbr-material/)
- [Aged Bronze (aiTextured)](https://aitextured.com/textures/metal/aged-bronze-texture-seamless.html)
- [Bronze 01 (cgbookcase)](https://www.cgbookcase.com/textures/bronze-01)
- [Physically Based Values Database](https://physicallybased.info)
- [Poliigon Acacia Wood](https://www.poliigon.com/texture/acacia-wood-board-texture-warm-brown/3691)
- [Poly Haven Sand Terrain](https://polyhaven.com/textures/terrain/sand)
- [Poly Haven Desert HDRI (Goegap)](https://polyhaven.com/a/goegap)

### Rendering Techniques
- [LearnOpenGL PBR Theory](https://learnopengl.com/PBR/Theory)
- [LearnOpenGL Bloom](https://learnopengl.com/Advanced-Lighting/Bloom)
- [LearnOpenGL Parallax Mapping](https://learnopengl.com/Advanced-Lighting/Parallax-Mapping)
- [LearnOpenGL Light Casters](https://learnopengl.com/Lighting/Light-casters)
- [GPU Gems 3: Volumetric Light Scattering (NVIDIA)](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process)
- [Screen-Space Godrays OpenGL 4.5 (GitHub)](https://github.com/math-araujo/screen-space-godrays)
- [GLSL Godrays Module (GitHub)](https://github.com/Erkaman/glsl-godrays)
- [Volumetric Light Scattering Shader (Fabien Sanglard)](https://fabiensanglard.net/lightScattering/)
- [GPU Cloth Simulation (GitHub)](https://github.com/likangning93/GPU_cloth)
- [PBR GLSL Shader (GitHub Gist)](https://gist.github.com/galek/53557375251e1a942dfa)

### Existing Interactive Tabernacle Projects
- [Immersive Tabernacle App](https://immersivehistory.com/tabernacle-of-moses-a-walk-through-exodus/)
- [Tabernacle of Moses VR (Meta Quest)](https://www.meta.com/experiences/tabernacle-of-moses/6329487790458233/)
- [Moses' Tabernacle VR Simulator (itch.io)](https://tryhard-studios.itch.io/arquitectura-y-realidad-virtual-tabernaculo)
- [Virtual Reality Bible Museum](https://vrbm.org/)

### Biblical and Historical References
- [Tabernacle Coverings (The Tabernacle Man)](https://www.thetabernacleman.com/post/coverings-of-the-tabernacle-of-moses-part-3-ram-skins-dyed-red-exodus-26-14-dr-terry-harman)
- [Cherubim Embroidered Covering (The Scriptures UK)](https://the-scriptures.co.uk/studies/topical-bible-studies/bible-studies-by-mike-glover/old-testament-studies/the-tabernacle-2/cherubim-embroidered-covering/)
- [Mercy Seat (Wikipedia)](https://en.wikipedia.org/wiki/Mercy_seat)
- [Temple Menorah (Wikipedia)](https://en.wikipedia.org/wiki/Temple_menorah)
- [Of Badger Skins and Dugong Hides (BAS Library)](https://library.biblicalarchaeology.org/sidebar/of-badger-skins-and-dugong-hides-a-translators-guide-to-tabernacle-covers/)
- [Shekinah Glory (GotQuestions)](https://www.gotquestions.org/shekinah-glory.html)
