# Material Masters v1

Reference spec for the three `UMaterial` master assets that back the DsonToUnreal material pipeline. Author these in UE5; the plugin's `DsonMaterialBuilder` instances them per scene material.

**Repo location for masters:** `/DsonToUnreal/Materials/`
**Runtime MIC location (created by builder):** `/Game/DazImports/Materials/`
**Runtime texture location (created by importer):** `/Game/DazImports/Textures/`

---

## Conventions (all masters)

- **Parameter naming:**
  - `<Name>Color` — Vector3 for colours, default `(1, 1, 1)` unless noted
  - `<Name>Map` — Texture2D for maps, default engine white texture
  - `Use<Name>Map` — Scalar 0/1 toggle. Builder sets to `1` when an image is present, `0` when absent. Master lerps between sampled texture and constant accordingly. Avoids unset-texture branching tricks.
  - `<Name>Weight` / `<Name>Strength` — Scalar for weights and strengths
- **Texture sampling:** UV0 only for v1. G9 Multi-UDIM deferred.
- **sRGB on textures (set by `DsonTextureImporter`, not the master):**
  - sRGB **on**: diffuse, translucency colour, top coat colour, makeup base colour
  - sRGB **off**: roughness, bump, normal, AO, SSS-data-encoded, dual-lobe data
- **Default values for the `Use<Name>Map` toggles:** `0.0` — builder explicitly raises to `1.0` when it sets a texture.

---

## M_DazIrayUber

**Shading model:** Default Lit + Subsurface
**Blend mode:** Opaque
**Purpose:** G8/G8.1 and most legacy DAZ content using the Iray Uber shader.

| Group | Parameter | Type | Default |
|---|---|---|---|
| Diffuse | `DiffuseColor` | Vector3 | (1, 1, 1) |
| Diffuse | `DiffuseMap` | Texture2D | engine white |
| Diffuse | `UseDiffuseMap` | Scalar | 0 |
| Translucency | `TranslucencyColor` | Vector3 | (1, 1, 1) |
| Translucency | `TranslucencyMap` | Texture2D | engine white |
| Translucency | `UseTranslucencyMap` | Scalar | 0 |
| Glossy | `GlossyWeight` | Scalar | 0 |
| Glossy | `GlossyMap` | Texture2D | engine white |
| Glossy | `UseGlossyMap` | Scalar | 0 |
| Bump | `BumpStrength` | Scalar | 0 |
| Bump | `BumpMap` | Texture2D | engine white |
| Bump | `UseBumpMap` | Scalar | 0 |
| Top Coat | `TopCoatWeight` | Scalar | 0 |
| Top Coat | `TopCoatMap` | Texture2D | engine white |
| Top Coat | `UseTopCoatMap` | Scalar | 0 |
| Top Coat | `TopCoatColor` | Vector3 | (1, 1, 1) |
| Top Coat | `TopCoatColorMap` | Texture2D | engine white |
| Top Coat | `UseTopCoatColorMap` | Scalar | 0 |
| Normal | `NormalStrength` | Scalar | 1 |
| Normal | `NormalMap` | Texture2D | engine default normal |
| Normal | `UseNormalMap` | Scalar | 0 |

**Wiring notes:**
- Route `TranslucencyColor` × `TranslucencyMap` into the Subsurface Color input. Translucency on non-skin surfaces simply stays at default and contributes nothing visible at reasonable subsurface scattering values.
- `GlossyWeight` × `GlossyMap` feeds Specular (or Roughness inverse — pick whichever reads better against Iray reference; specular is more direct).
- **Bump is no longer derived in-shader (v1.5).** The in-shader height→normal path
  caused hard seams at DAZ material-zone UV islands (screen/texture-space derivative
  discontinuity). Bump is now baked to a tangent-space normal map at import (scaled
  by `Bump Strength`) and **combined** into the normal input; the
  `BumpStrength`/`BumpMap`/`UseBumpMap` inputs remain on the master but are **inert**
  (never set by the builder). See `Docs/Roadmap.md` → "IrayUber bump-map seam" for
  the decision, justification, and consequences.
- `TopCoat*` parameters drive Clear Coat. Requires Clear Coat shading model — if combining with Subsurface isn't viable, drop Clear Coat for v1 and just let `TopCoatColor` tint specular.

---

## M_DazPBRSkin

**Shading model:** Default Lit + Subsurface
**Blend mode:** Opaque
**Purpose:** PBRSkin surfaces (Laura and recent G9 character presets).

| Group | Parameter | Type | Default |
|---|---|---|---|
| Diffuse | `DiffuseColor` | Vector3 | (1, 1, 1) |
| Diffuse | `DiffuseMap` | Texture2D | engine white |
| Diffuse | `UseDiffuseMap` | Scalar | 0 |
| Translucency | `TranslucencyColor` | Vector3 | (1, 1, 1) |
| Translucency | `TranslucencyMap` | Texture2D | engine white |
| Translucency | `UseTranslucencyMap` | Scalar | 0 |
| Translucency | `TranslucencyWeight` | Scalar | 0 |
| Specular | `SpecularRoughness` | Scalar | 0.5 |
| Specular | `SpecularRoughnessMap` | Texture2D | engine white |
| Specular | `UseSpecularRoughnessMap` | Scalar | 0 |
| Specular | `SpecularRoughnessMult` | Scalar | 1 |
| Dual Lobe | `DualLobeWeight` | Scalar | 0 |
| Dual Lobe | `DualLobeMap` | Texture2D | engine white |
| Dual Lobe | `UseDualLobeMap` | Scalar | 0 |
| Detail Normal | `DetailNormalStrength` | Scalar | 1 |
| Detail Normal | `DetailNormalMap` | Texture2D | engine default normal |
| Detail Normal | `UseDetailNormalMap` | Scalar | 0 |
| AO | `AOWeight` | Scalar | 1 |
| AO | `AOMap` | Texture2D | engine white |
| AO | `UseAOMap` | Scalar | 0 |
| Top Coat | `TopCoatWeight` | Scalar | 0 |
| Top Coat | `TopCoatRoughness` | Scalar | 0.5 |
| Top Coat | `TopCoatBumpWeight` | Scalar | 0 |

**Wiring notes:**
- PBRSkin uses Detail Normal Map exclusively (Bump Enable is empirically false on Laura). The single normal input on the master is `DetailNormalMap`; don't waste a bump path here.
- `TranslucencyColor` × `TranslucencyMap` × `TranslucencyWeight` → Subsurface Color (weight controls subsurface contribution).
- `SpecularRoughness` × `SpecularRoughnessMap` × `SpecularRoughnessMult` → Roughness input. The Mult is per-surface tuning from DAZ; multiply through after the map sample.
- `DualLobeWeight` × `DualLobeMap` is the second specular lobe contribution. For v1, blend a second specular evaluation at lower roughness and lerp by dual lobe weight, or — simpler — drop to a single lobe and let dual lobe drive a slight roughness reduction. Pick whichever reads acceptably; this is a known approximation.
- `AOWeight` × `AOMap` → multiply with Base Color, or feed AO input if using a workflow that exposes it.
- `TopCoat*` is scalar-only here (no colour, no map) — drives Clear Coat strength, roughness, and the clear-coat-specific bump. If Clear Coat + Subsurface conflicts, treat top coat as a roughness modifier in v1.

---

## M_DazDefault

**Shading model:** Default Lit
**Blend mode:** Opaque
**Purpose:** Fallback for mesh sections without a matching scene-material override. Mimics DAZ Studio's matte grey unloaded-figure appearance.

| Parameter | Type | Default |
|---|---|---|
| `DiffuseColor` | Vector3 | (0.5, 0.5, 0.5) |

Roughness baked at `1.0`. Metallic baked at `0`. No textures, no other parameters.

---

## Builder contract

The `DsonMaterialBuilder` will:

1. Run shader detection (URL fragment first, then `shader_type`).
2. Pick the right master per scene material.
3. Create a `UMaterialInstanceConstant` parented to that master.
4. For each channel on the scene material:
   - Look up `channel.id` in the master's mapping table.
   - If binding has a `ColorParam` and the channel has colour: `SetVectorParameterValue`.
   - If binding has a `ScalarParam` and the channel has a scalar value: `SetScalarParameterValue`.
   - If binding has a `TextureParam` and `image_url` is non-empty: import via `DsonTextureImporter`, `SetTextureParameterValue`, set the `UseFlag` to `1.0`.
5. Unmatched mesh sections → `M_DazDefault`.

Channels on a scene material that don't appear in the mapping table for its shader are silently ignored in v1. Logging at verbose level is fine; warnings would be noise given how many Iray Uber channels we deliberately skip.

---

## Open follow-ups (post-v1)

- Eye/cornea/eye-moisture treatment (translucent master, likely `M_DazEyeMoisture`).
- Subsurface Profile pipeline (per-character `USubsurfaceProfile` assets instead of inline subsurface).
- PBRSkin makeup, transmission, sub surface (`SSS Direction`, `Sub Surface Enable`), metallic flakes mappings.
- G9 Multi-UDIM (per-tile textures, UV set parsing).
- Bump path for PBRSkin if a preset is ever found that uses it.
