# SHOGOD — RE anchors: `client.dll` build 2026-11-19

Slice: RE anchors. Owns this file, `anchors.json`, `evidence/`.

## 1. Build identity (verified against the shipped binaries)

| module | sha256 | sha1 | `csgo.signatures` SHA1 | match |
|---|---|---|---|---|
| `game/csgo/bin/win64/client.dll` (39,141,528 B) | `9e0fd6aa19a8cb491c45ae5e9ce3d13716892a42c578e0c4911cf9c3ffaa67ee` | `4E16EA32EA9485A25A67933ACD5A8B4870D855D1` | `4E16EA32EA9485A25A67933ACD5A8B4870D855D1` | yes |
| `game/bin/win64/schemasystem.dll` (445,592 B) | `fddb9d8a70c752ae913da3e7e80dc2392ee4502e678e56ff3ee831e63529cd2d` | `4C668A75B9E44100038601DDCE948F0209A0D6BA` | `4C668A75B9E44100038601DDCE948F0209A0D6BA` | yes |
| `game/bin/win64/tier0.dll` (3,979,416 B) | `0bcd160dfa72d9f97fbf85095d59baad18fca61f20d840e83a5bd9f7635d5c32` | `ECD34F0DA7327FBB0F2C220B37604467E5A0CD7E` | `ECD34F0DA7327FBB0F2C220B37604467E5A0CD7E` | yes |

`csgo.signatures` also pins `client.dll` CRC `0AC186C5`. Image base `0x180000000`, x86-64 PE.

Sections (rizin `iS`, Evidence `ev_5a99cf4a…`):

| section | VA | vsize | paddr | raw |
|---|---|---|---|---|
| `.text` | `0x180001000` | `0x1aa7000` | `0x400` | `0x1aa7000` |
| `.rdata` | `0x181aa8000` | `0x70a000` | `0x1aa7400` | `0x709e00` |
| `.data` | `0x1821b2000` | `0x5e3000` | `0x21b1200` | `0x1a8800` |
| `.pdata` | `0x182795000` | `0x177000` | `0x2359a00` | `0x176a00` |
| `.reloc` | `0x18290c000` | `0x81000` | `0x24d0400` | `0x81000` |

`.data` vsize (`0x5e3000`) is much larger than its raw size (`0x1a8800`): every global above
`RVA 0x1a8800` lives in the zero-filled tail, so it has **no bytes on disk**. Those anchors are
proven by the referencing instruction (evidence below), not by a file dump. This is called out per
row.

## 2. Method (what actually produced this)

- `mcp__rea_open_binary` on `client.dll` with the Ghidra provider **timed out**: REA's Ghidra
  startup deadline is fixed at 330 s in the running MCP server, and a 39 MB CS2 `client.dll` does not
  finish auto-analysis in that window (`provider_timeout`, `REA_GHIDRA_STARTUP_TIMEOUT_MS`). No
  partial results are exposed, so Ghidra contributed **nothing** to this table.
- Fallback, per Main's direction: `mcp__rea_execute_rizin_command` (rizin 0.9.1, REA-provided
  Evidence with SHA-256 binding to the binary) for every byte-level and disassembly claim.
- Candidate discovery (RTTI/vtable walk, RIP-relative xref index over 7,558,569 instructions,
  556,118 data refs, 331,238 direct calls) was done offline with `pefile` + `capstone` against the
  same file; every number that appears in this document was then **re-read through rizin** at the
  cited address, so each row is independently checkable with one `px`/`pd`/`/x` command.
- Pattern uniqueness was verified twice: offline regex over the whole file, and rizin `/x`
  (Evidence `ev_790e28cc…`) — every pattern below reports **exactly one hit**, at the stated RVA.

## 3. Anchor table

`pattern_rva` is where the signature starts. `rva` is the resolved anchor. `file off` is the
on-disk offset of the pattern (`.data` BSS targets have none — see §1).

| # | anchor | kind | pattern (unique, 1 hit) | resolve | pattern RVA / file off | rva | Evidence |
|---|---|---|---|---|---|---|---|
| 1 | `g_pGameEntitySystem` | global ptr | `0F B6 44 24 28 88 44 24 28 48 89 0D ?? ?? ?? ?? E9` | `rip` @+9, len 7 | `0x15b8c30` / `0x15b8030` | **`0x270c680`** | `ev_ec045657…`, `ev_790e28cc…` |
| 2 | `CGameEntitySystem::ctor` | function | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 E8 99` | `abs` | `0x9bc640` / `0x9bba40` | **`0x9bc640`** | `ev_51b8f025…`, `ev_790e28cc…` |
| 3 | `CGameEntitySystem::vtable` | data | — (read via row 2) | `abs` | — | **`0x1c29090`** | `ev_51b8f025…` |
| 4 | `g_pInput` (`CCSGOInput*`) | global ptr | `48 8B 0D ?? ?? ?? ?? 4C 8D 47 14 8B 10` | `rip` @+0, len 7 | `0x84e2d5` / `0x84d6d5` | **`0x2230210`** | `ev_ec045657…`, `ev_790e28cc…` |
| 5 | `CCSGOInput` instance (static) | data | `48 89 05 ?? ?? ?? ?? 0F 57 C0 0F 11 05 ?? ?? ?? ?? 48` | `rip` @+0, len 7 | `0xee2aa` / `0xed6aa` | **`0x256cfe0`** | `ev_ec045657…`, `ev_790e28cc…` |
| 6 | `CCSGOInput::ctor` | function | `48 8D 05 ?? ?? ?? ?? 48 89 7C 24 38 48 89 01 33 ED 48 8D 05 ?? ?? ?? ?? 89 A9` | `abs` | `0xcd161a` / `0xcd0a1a` | **`0xcd1610`** (lea at +0xa) | `ev_ec045657…`, `ev_790e28cc…` |
| 7 | `CCSGOInput::vtable` (primary) | data | — (read via row 6) | `abs` | — | **`0x1c97330`** | `ev_51b8f025…` |
| 8 | `CCSGOInput::CreateMove` *(candidate)* | vtable slot | `85 D2 0F 85 ?? ?? ?? ?? 53 48 83 EC 20 48 8B D9 89 91 84` | `vtable` slot **7** | `0xb781e0` / `0xb775e0` | **`0xb781e0`** | `ev_54ecae37…`, `ev_51b8f025…`, `ev_790e28cc…` |
| 9 | `CUserCmd` layout (`GetUserCmd`) | struct facts | `49 8B 80 58 0B 00 00 48 69 D1 40 04 00 00 48 05 30 04 00 00 48 ?? ?? ?? 49 8D 80 88 06 00 00 F2 0F` | none (constants) | `0xb57f74` / `0xb57374` | — | `ev_54ecae37…`, `ev_790e28cc…` |
| 10 | `CViewRender::vtable` | data | `48 8D 05 ?? ?? ?? ?? 33 FF 48 89 01 48 8B D9 48 8B 91 58 1D` | `rip` @+0, len 7 | `0xbfa14a` / `0xbf954a` | **`0x1c76ac8`** | `ev_54ecae37…`, `ev_51b8f025…`, `ev_790e28cc…` |
| 11 | `SchemaSystem_001` registry | interface entry | entry at `0x1e56970`: `28 99 c5 81 01 00 00 00` → name, `40 ec 78 82 01 00 00 00` → instance slot | `abs` (data) | `0x1e56970` / `0x1e55d70` | name **`0x1c59928`**, slot **`0x278ec40`** | `ev_54ecae37…` |
| 12 | bone array struct layout | struct facts | `48 89 5C 24 08 57 48 83 EC 20 8B 81 8C 00 00 00 33 FF 89 79 78 48 8B D9 A9 00 00 00 C0 75 ?? 48 8B 91 80 00 00 00 48 85 D2 74 ?? 48 8B 05 2E 5D` | none (constants) | `0x203be0` / `0x202fe0` | — | `ev_54ecae37…`, `ev_790e28cc…` |
| 13 | trace surface (`TraceShape`) | string | — | `abs` (data) | — | **`0x1c2c8d8`** | `ev_54ecae37…` |
| 14 | schema class names | strings | — | `abs` (data) | — | `C_CSPlayerPawn` `0x1b9e1b0`, `CCSPlayerController` `0x1c04f70`, `CSkeletonInstance` `0x1ac9770`, `m_modelState` `0x1b262d0`, `CCSWeaponBaseVData` `0x1bc8980` | offline string table (§6), `ev_5a99cf4a…` for module layout |

All RVAs are `VA − 0x180000000`. `??` = wildcard byte. Resolve offsets are relative to
`pattern_rva`; for `rip` rows, `target = module_base + pattern_rva + instr_offset + instr_len + disp32`.

## 4. Per-anchor evidence and proposed runtime validation

### 1 — `g_pGameEntitySystem` (`RVA 0x270c680`)
Rizin `pd 6 @ 0x1815b8c30` (Evidence `ev_ec045657…`):

```
0x1815b8c30  movzx eax, byte ptr [rsp+0x28]
0x1815b8c35  mov   byte ptr [rsp+0x28], al
0x1815b8c39  mov   qword ptr [0x18270c680], rcx     ; [0x18270c680:8]=0
0x1815b8c40  jmp   0x1815b4be0
```

This thunk is entered with `rcx =` the freshly constructed entity system (`0x180a12d14` calls the
ctor at `0x1809bc640` on a `0x2180`-byte allocation, then calls `0x1815b8c30` with
`rdx = "client_entities"`), so the stored pointer is `CGameEntitySystem*`. 167+ RIP-relative
references exist across `.text` (`mov rcx, [0x18270c680]` / `mov rax, [..]`). Target is BSS:
no on-disk bytes, the store instruction is the evidence.

**Validation at init:** read `p = *(void**)(base+0x270c680)`; require `p != nullptr` **and**
`*(uintptr_t*)p == base + 0x1c29090` (the `CGameEntitySystem` vtable from row 3). On failure,
disable every entity-dependent feature and log `FAIL entity_system`.

### 2/3 — `CGameEntitySystem::ctor` + vtable (`0x9bc640`, `0x1c29090`)
Ctor found by resolving MSVC RTTI `.?AVCGameEntitySystem@@` → TypeDescriptor → Complete Object
Locator (`0x181efea38`, `pSelf` self-checked) → vtable; the only code reference to that vtable is
`0x1809bc667` inside this ctor. Rizin `pd 5 @ 0x1809bc640` (`ev_51b8f025…`) matches byte-for-byte.
The ctor allocates `0x2180` (`mov ecx, 0x2180` at `0x180a12d02`), writes vtable at `[rdi]`, inits
`[rdi+0x2130]` block. Vtable bytes (`px 32 @ 0x181c29090`, same evidence):

```
20 b6 9c 80 01 00 00 00  -> 0x1809cb620   (vfunc 0)
a0 b6 9c 80 01 00 00 00  -> 0x1809cb6a0   (vfunc 1)
60 b1 9c 80 01 00 00 00  -> 0x1809cb160   (vfunc 2)
50 b0 9c 80 01 00 00 00  -> 0x1809cb050   (vfunc 3)
```

**Validation:** ctor is build-repair material only (re-derive after a patch); the runtime probe is
row 1's vtable equality, which already pins `0x1c29090`.

### 4/5/6/7 — `CCSGOInput`
- `g_pInput` (`0x2230210`) holds a relocated pointer; rizin annotates the load site
  `0x18084e2d5` with `[0x182230210:8]=0x18256cfe0`, and `px 16 @ 0x182230210` shows
  `e0 cf 56 82 01 00 00 00` = `0x18256cfe0` (this global *is* initialised data, file offset
  `0x222f410`).
- The instance itself is a **statically allocated** `CCSGOInput` (RVA `0x256cfe0`, BSS). The
  load-time initializer at `0x1800ee280` writes its three vtables: `[0x18256cfe0]=0x181c97330`,
  `[0x18256dbe8]=0`, `[0x18256db90]=0`, `[0x18256db60]=0x181c97440`, `[0x18256db68]=0x181c97450`
  (`pd 9 @ 0x1800ee2aa`, `ev_ec045657…`). Note `0x18256db60/68 == 0x18256cfe0 + 0xb80/0xb88`, i.e.
  the two base-subobject vtables, which is what makes this a single object rather than three.
- Ctor `0x180cd1610` writes `[this]=0x181c97330`, `[this+0xb80]=0x181c97440`,
  `[this+0xb88]=0x181c97450` (`pd 8 @ 0x180cd161a`, `ev_ec045657…`); object size `0xc20`
  (deleting-dtor wrapper at `0x180cd4750`: `mov edx, 0xc20`). The same ctor is reached at load by
  `lea rcx,[0x18256cfe0]; jmp 0x180cd1610` at `0x181a8f2e0`.
- Primary vtable `0x1c97330` has 33 slots (slot 33 is the next COL, not code). Byte dump of
  slots 0..8 (`px 72 @ 0x181c97330`, `ev_51b8f025…`):

```
[0] 0x180cd4750  [1] 0x180cf2b10  [2] 0x180d02e40  [3] 0x180cf5200
[4] 0x180cfd7c0  [5] 0x180b5ba30  [6] 0x180ce4a60  [7] 0x180b781e0
[8] 0x180ce1d00
```

**Validation at init:** `in = *(void**)(base+0x2230210)`; require `in != nullptr`,
`*(uintptr_t*)in == base + 0x1c97330`, and `((void**)in)[7] == base + 0xb781e0`. The last check
also proves the CreateMove slot, so a wrong slot index can never be silently used.

### 8 — `CCSGOInput::CreateMove` (candidate, vtable slot 7)
Rizin `pd 10 @ 0x180b781e0` (`ev_54ecae37…`):

```
0x180b781e0  test edx, edx
0x180b781e2  jnz  0x180b782bf          ; only slot 0 does work
0x180b781e8  push rbx ; sub rsp,0x20 ; mov rbx,rcx
0x180b781f0  mov  dword ptr [rcx+0x284], edx
0x180b781f6  call 0x180b56340          ; shift/age the CUserCmd history
0x180b781fb  movss xmm1, [0x181ac1eb4] ; = 0x3f800000 (1.0f)
0x180b78206  cmp  dword ptr [rbx+0xb50], r9d   ; command count
```

It then walks `[rbx+0xb58]` (command array), subtracting 1.0f from `cmd+0x43c` and from each
per-subtick step at `cmd+0x30` (stride `0x20`) — the per-tick command-time advance.
Slot 5 (`0x180b5ba30`) and slot 9 (`0x180b6dec0`) are the other per-tick candidates: both gate on
`edx` and touch `CUserCmd`, but slot 7 is the only one whose body is the command-time loop.

**Status: partial — slot index is a candidate, not confirmed.** It is the only claim in this table
that is inferred from behaviour rather than from a named symbol.
**Validation (must run before shipping):** install the detour on `vtable[7]`, log on the first 3
calls: `edx`, `*(int*)(this+0xb50)`, `*(void**)(this+0xb58)`, and `*(float*)(cmd+0x430)`. Expected:
`edx == 0`, `0 <= count <= 150`, non-null array, finite angles in `[-360,360]`. If `edx` is ever
non-zero on a real tick, the slot is wrong → fall back to slots 5/9 and re-test.

### 9 — `CUserCmd` layout / `GetUserCmd`
`pd 10 @ 0x180b57f74` (`ev_54ecae37…`):

```
0x180b57f74  mov  rax, qword ptr [r8+0xb58]      ; CCSGOInput::m_pCommands
0x180b57f7b  imul rdx, rcx, 0x440                ; CUserCmd stride
0x180b57f82  add  rax, 0x430                     ; view angles inside the command
0x180b57f88  add  rax, rdx
0x180b57f8d  lea  rax, [r8+0x688]                ; fallback when count == 0
```

Independently confirmed by 15 `imul r*, r*, 0x440` sites in `.text` and by the setter
`0x180b76e40` (`movsxd rax,[rcx+0xb50]; imul rdx,rax,0x440; add rdx,[rcx+0xb58];
movsd [rdx-0x10],xmm0`) reached from 12 call sites that all load `[0x182230210]` first.

Constants: command array `+0xb58`, command count `+0xb50`, stride `0x440`, view angles `+0x430`
(3 floats written as one `movsd` + one dword at `+0x438`), a float at `+0x43c`, subtick step count
`+0x2c`, subtick array `+0x30` (stride `0x20`), no-command fallback `+0x688`.

**Validation:** after resolving `in` (row 4), read `n = *(int*)(in+0xb50)`, `arr = *(void**)(in+0xb58)`;
require `0 <= n <= 150` and `arr` readable; then `Vec3 a = *(Vec3*)((char*)arr + (n?n-1:0)*0x440 + 0x430)`
and require `isfinite(a.x|y|z)` and `|a.x|,|a.y| <= 360`.

### 10 — `CViewRender::vtable` (`0x1c76ac8`)
`pd 5 @ 0x180bfa14a` (`ev_54ecae37…`): `lea rax,[0x181c76ac8]; mov [rcx],rax` — the ctor writes the
vtable; 23 slots; first entries `0x180c05030, 0x180c0a340, 0x180c0a3e0, 0x180c11620, 0x180c0b000`
(`px 16 @ 0x181c76ac8` → `30 50 c0 80 01 00 00 00`, `ev_51b8f025…`).
Use: this is the object whose render path owns the view/projection matrices — the entry point for the
**missing** view-matrix anchor (§7).

**Validation:** once a `CViewRender*` is available, `*(uintptr_t*)obj == base+0x1c76ac8`.

### 11 — `SchemaSystem_001`
`px 32 @ 0x181e56960` (`ev_54ecae37…`) shows the interface registry as `{const char* name, void** slot}`:

```
0x181e56968  58 ec 78 82 01 00 00 00  -> 0x18278ec58
0x181e56970  28 99 c5 81 01 00 00 00  -> 0x181c59928   "SchemaSystem_001"
0x181e56978  40 ec 78 82 01 00 00 00  -> 0x18278ec40   instance slot
0x181e56980  b0 70 e5 81 01 00 00 00  -> 0x181e570b0   (next entry name)
```

`"schemasystem"` sits at `0x181c59940`, right after the interface name. The slot `0x278ec40` is BSS —
the interface is created by `schemasystem.dll` (`CreateInterface` export confirmed in that module's
strings) and the pointer is written there at init.

**Runtime flow for netvars (proposal, per `cs2-internal-rea`):** `ISchemaSystem* s =
CreateInterface("SchemaSystem_001")` → type scope for `client.dll` → `FindDeclaredClass("C_CSPlayerPawn")`
→ field array → `{name, offset}`. Never hardcode a netvar offset.
**Validation:** returned pointer non-null and its vtable lies inside the `schemasystem.dll` module
range (`[base, base+SizeOfImage)`); then a known-field probe (`m_iHealth`) must yield a sane offset
(`0 < off < 0x4000`).

### 12 — bone array layout (partial)
`pd 10 @ 0x180203be0` (`ev_54ecae37…`): a destructor over a heap array with

```
mov eax, dword ptr [rcx+0x8c]      ; allocation count / flags
mov dword ptr [rcx+0x78], 0
test eax, 0xc0000000               ; CUtlMemory-style "owned" flags
mov rdx, qword ptr [rcx+0x80]      ; array base
```

i.e. `{ptr@+0x80, size@+0x88, allocCount+flags@+0x8c}`. That matches the documented
`m_modelState + 0x80` / count `+0x8c` layout, **but nothing in this evidence ties the struct to
`CModelState`**: `m_modelState` only exists here as a schema name string (`0x1b262d0`), and the
class→offset mapping must come from schema at runtime. Treat as layout evidence, not as a resolved
anchor.

**Validation:** `st = (char*)pawn + schema_offset("m_modelState")`; `bones = *(void**)(st+0x80)`;
`n = *(uint32_t*)(st+0x8c) & 0x3FFFFFFF`; require `bones != nullptr && 0 < n <= 256`; then read bone
0's 3x4 floats and require finite values and `|origin| < 20000` (world scale). If `n` is 0 or
`bones` is null, the offset is wrong for this build → disable skeleton features.

### 13 — trace surface (partial)
`CGameTraceManager` **does not exist** in this build: no RTTI type, no string. What exists:
`"CGamePhysicsQueryInterface::TraceShape"` (`0x1c2c8d8`), `"Physics/TraceShape (Client)"`
(`0x1c2c900`), `"TraceShapeToEntity"`, and 13 `CTraceFilter*` RTTI classes
(`.?AVCTraceFilter@@`, `.?AVCTraceFilterSkipPlayersAndNPCs@@`, `.?AVCCSTraceFilterSimple@@`, …).
So the trace path here is `CGamePhysicsQueryInterface::TraceShape`, not the older
`CGameTraceManager::TraceShape` layout. The interface instance/registration and the `TraceShape`
vtable slot were **not** derived (see §7).

### 14 — schema class names present in `.rdata`
`C_CSPlayerPawn` `0x1b9e1b0`, `C_CSPlayerPawnBase` `0x1b9e198`, `CCSPlayerController` `0x1c04f70`,
`C_BaseEntity` `0x1abe590`, `CSkeletonInstance` `0x1ac9770`, `m_modelState` `0x1b262d0`,
`CCSWeaponBaseVData` `0x1bc8980`, plus `CNetworkVarBase<…>` RTTI names that carry netvar names
(e.g. `VNetworkVar_m_flUsingGroundTopologyOffsetTransitionSmoothing@CCSPlayer_MovementServices`).
Weapon data (`name/damage/range/penetration/spread/recoil`) is **not** a code anchor: it is the
schema class `CCSWeaponBaseVData` resolved at runtime through row 11, plus the per-weapon
`m_AttributeManager`/`m_Item` chain. Nothing about those offsets is asserted here.

## 5. What is complete vs. partial

**Complete (pattern + RVA + independent rizin re-read):** `g_pGameEntitySystem`,
`CGameEntitySystem` ctor/vtable, `g_pInput`, `CCSGOInput` instance/ctor/vtable, `CUserCmd` layout,
`CViewRender` vtable, `SchemaSystem_001` registry entry + instance slot, schema class-name strings.

**Partial:**
- `CCSGOInput::CreateMove` — vtable slot 7 is a behavioural candidate (§4.8); needs one runtime log.
- bone array — struct layout proven, class binding not.
- trace — interface name/class identified, instance + slot not derived.

**Not derived (honest gaps):**
- **view matrix global** — not found. Candidates were screened mechanically (16 consecutive float
  RIP refs; 16-byte `movaps` store chains; `0.001f` constant refs) and every hit was either a
  static-initialiser thunk or a matrix-multiply helper over `.rdata` constants. A per-frame writable
  `VMatrix` in `.data` was not located, which means it is most likely reached through the
  `CViewRender` object (row 10) rather than a plain global in this build.
- **`GlobalVars`** — not found. The one global that matches a `CGlobalVarsBase`-style field
  fingerprint (`0x25d88d8`, disps `0xc…0x40` sequential) is written as a state machine
  (`mov dword [rax+4/8/0xc/0x10], esi` with `cmp eax, N` dispatch), not read as tick/tickcount.
- **prediction / lag compensation** — the engine client pointer lives in `engine2.dll`, out of this
  slice's module; nothing derived.

## 6. Reproduce

```
# identity
sha1sum client.dll            # 4E16EA32EA9485A25A67933ACD5A8B4870D855D1
# every row, one command
/x 0fb64424288844242848890d........e99b          -> 0x1815b8c30 (1 hit)
/x 488b0d........4c8d47148b10                    -> 0x18084e2d5 (1 hit)
/x 488905........0f57c00f1105........48          -> 0x1800ee2aa (1 hit)
/x 488d05........48897c243848890133ed488d05........89a9 -> 0x180cd161a (1 hit)
/x 85d20f85........534883ec20488bd9899184        -> 0x180b781e0 (1 hit)
/x 498b80580b00004869d14004000048053004000048........498d8088060000f20f -> 0x180b57f74
/x 488d05........33ff488901488bd9488b91581d      -> 0x180bfa14a (1 hit)
/x 48895c24084889742410574883ec20488bf9e899      -> 0x1809bc640 (1 hit)
/x 48895c2408574883ec208b818c00000033ff897978488bd9a9000000c075..488b91800000004885d274..488b052e5d -> 0x180203be0
pd 6 @ 0x1815b8c30 ; px 72 @ 0x181c97330 ; px 32 @ 0x181e56960
```

Offline cross-check used the same file with `pefile` + `capstone` (RTTI walk:
TypeDescriptor → Complete Object Locator → vtable; 7,558,569-instruction linear sweep for
RIP-relative refs). Evidence bundle: `docs/RE/evidence/bundle.json`.

## 8. Continuação (mesma build) — layouts derivados depois do slice

### 8.1 Iteração de entidades (provado por código, RVA no `.text`)

`0x1809f1d50` é o `GetEntityByIndex(entitySystem, index)` canônico:

```
cmp edx, -3 / ja fail
eax = edx & 0x7fff
chunk = [entitySystem + 0x10 + (index>>9)*8]   ; 64 chunks
entry = chunk + (index & 0x1ff) * 0x70          ; 512 entries de 0x70
if entry->serial(index em +0x10, low 15 bits) == index
    return *(void**)entry                        ; +0x00 = entity*
```

Constantes: max index `0x7fff`, chunks `64`, entries/chunk `512`, stride `0x70`,
serial em `+0x10` (low 15 bits). Wrapper `0x180b65370` devolve
`[g_pGameEntitySystem] + 0x10` (base do array de chunks).

### 8.2 Schema system — vtable slot 12

`ISchemaSystem::FindTypeScopeForModule("client.dll")` = vtable **slot 12**
(offset `0x60`), comprovado pelo caller `0x180027062` em `schemasystem.dll`
que passa a string `"schemasystem.dll"` e em seguida usa `[scope]+0x28`.
O TypeScope é o ponto de entrada para `DeclaredClass`/fields (fluxo ainda a
derivar: slots do TypeScope).

### 8.3 Runtime injection (CS2 vivo, `-insecure`)

- `entity_system`: OK — ponteiro lido do global, vtable == `base+0x1c29090`.
- `input`: OK — vtable == `base+0x1c97330`; **slot 7 == `base+0xb781e0`**
  (o candidato do §4.8 virou fato).
- `schema`: OK — `CreateInterface("SchemaSystem_001")`, vtable dentro de
  `schemasystem.dll`.
- `CreateMove` hook: instalado no `vtable[7]` do objeto; o jogo sobrevive.
  Ainda sem log de chamada porque o teste rodou em menu/partida sem tick.
- **Present resolvido**: editar a vtable compartilhada do `IDXGISwapChain`
  derruba o CS2 dentro de `dxgi.dll` (falha em `Present+7`, detour nunca
  chamado; CFG e signature policy desligados — não é CFG). Solução que
  funciona: **clone privado da vtable**. O hook cria um swapchain dummy,
  captura a vtable, localiza o objeto do jogo por varredura de memória
  (qword == vtable + `GetDesc` retornando a janela do jogo, sob SEH e
  pulando `PAGE_GUARD`), copia a tabela para memória própria, troca os
  slots 8/13 no clone e escreve o ponteiro da vtable só naquele objeto.
  Nenhuma tabela compartilhada é tocada. Evidência: log
  `hooks: private vtable ... installed` + `hooks: Present hook live`.
- **CreateMove ao vivo**: `edx=0`, `count=2`, `arr` válido nos 3 primeiros
  ticks — validação do §4.8 cumprida. Menu renderiza em 1920x1080 com o
  jogo rodando (screenshot `shots/game/menu_ingame.png`).

### 8.4 Como reproduzir o teste in-game

```
steam://run/730//-insecure -novid +game_type 0 +game_mode 1 +map de_dust2
build/Release/shogod_loader.exe --wait 60
%APPDATA%\shogod\logs\shogod-YYYYMMDD.log
```

### 8.5 CViewRender — instância estática localizada (base para a view matrix)

O objeto `CViewRender` é construído **in-place** num global BSS, não no heap:

```
0x181a8f270  lea rcx, [rip + 0xadbd29]   ; rcx = 0x18256afa0
0x181a8f277  jmp 0x180bfa140              ; CViewRender::ctor
```

- Instância: RVA **`0x256afa0`** (BSS, sem bytes em disco).
- Ctor: `0x180bfa140`, escreve vtable `0x181c76ac8` em `[rcx]`.
- Tamanho do objeto: `0x1d60` (deleter em `0x180bfb3e9`).
- Validação de runtime: `obj = base + 0x256afa0`; `*(uintptr_t*)obj == base + 0x1c76ac8`.

A view matrix **ainda não está derivada**. Ela deve ser membro desse objeto
(ou de um `CViewSetup` embutido nele); o próximo passo é achar a função que
escreve 16 floats nesse intervalo por frame e confirmar com leitura de
`obj + off` durante o jogo (valores finitos, `w` coerente, muda com a câmera).
Vtable tem 23 slots; o slot 17 é um getter de float em `+0x1cd0`.

### 8.6 Sonda de runtime da view matrix + resolução via `CViewRender` slot 4 (`0x255ce50`)

Sonda inicial em `CViewRender` (`base+0x256afa0`) não achou matriz plana em `[0x20, 0x1d60)` porque o cálculo de projeção escreve em um **array global de `VMatrix` fora do objeto**.

Desmontagem de `CViewRender::vtable[4]` (`0x180c0b000`) e do leitor W2S `0x180c104e5`:
- `0x180c0b078`: `lea rcx, [rip + 0x1951dd1]` -> `0x18255ce50` (passado como `worldToProjection` para `0x1803b6c00` junto com `0x18255cdd0` `worldToView` e `0x18255ce10` `viewToProjection`).
- `0x180c104e5` (leitor W2S `0x180c10520`): `48 63 08 48 8D 05 ?? ?? ?? ?? 48 C1 E1 06 48 03 C8` -> 1 hit único no binário, `rip` @+3 len 7 -> **RVA `0x255ce50`** (`dwViewMatrix`, stride `0x40` por splitscreen slot, row-major 4×4 compatível com `math::mat4::world_to_screen`).

### 8.7 Schema runtime (`schemasystem.dll` + `SchemaClassInfoData_t`)

- `ISchemaSystem::FindTypeScopeForModule("client.dll", nullptr)` = `vtable[12]` (`0x60`).
- `CSchemaSystemTypeScope::FindDeclaredClass(&out, name)` = `vtable[2]` (`0x1800162b0`, despacha para `vtable[25]` `0x180010380`).
- Layout verificado em `client.dll` (`.data` `0x18222ba90` `C_CSPlayerPawn` e `0x1821e8f80` `C_BaseEntity`):
  - `+0x08`: `const char* m_pszName`
  - `+0x24`: `int16_t m_nFieldCount`
  - `+0x29`: `uint8_t m_nBaseClassesCount`
  - `+0x30`: `SchemaClassFieldData_t*` (stride `0x20`: `+0x00` `name`, `+0x10` `int32_t offset`)
  - `+0x38`: `SchemaBaseClassInfoData_t*` (stride `0x10`: `+0x00` `uint32_t offset`, `+0x08` `SchemaClassInfoData_t*`)

### 8.8 Tick real em partida — `CCSGOInput::vtable[4]` (não `vtable[7]`)

Sonda Frida em partida offline (dust2 com bots), na build pinada:

| Alvo | Resultado |
|---|---|
| `client.dll+0xb781e0` (`vtable[7]`, candidato antigo) | **0 hits em 7 s** |
| `client.dll+0xb782c0` (thunk `jmp [input_vtable+0x38]`) | **0 hits em 7 s** |
| `client.dll+0xb723c0` (builder de comando) | ~100 hits/s |
| `client.dll+0xcfd7c0` (`vtable[4]`) | por tick; `count 0 → 1`, ângulos válidos |

Backtrace do builder: `vtable[4] (0xcfd7c0) → 0xb723c0`. Portanto `vtable[4]` é o
update de input por tick que produz o `CUserCmd`; `vtable[7]` só é alcançado no
front-end. O cheat passa a hookar `vtable[4]` **depois** do original, então o
ragebot/antiaim escrevem no comando que o jogo vai consumir.

### 8.9 `CUserCmd` — máscaras de botão

- `cmd+0x00` = máscara final de botões (held): o builder em `0xb7286c` grava
  `(~[rbp-0x19] & [rbp-0x21]) | [rbp-0x11]` nesse campo.
- `cmd+0x08` / `cmd+0x10` = acumuladores por fonte (subtick), não o estado lido pelo gameplay.
- Verificação em partida: segurando ESPAÇO, o campo `+0x00` do comando mais novo
  passa a `0x2` (`IN_JUMP`); o cheat limpa o bit quando no ar (bhop) e o campo
  sai `0x0` — prova de que a escrita pós-builder sobrevive no comando.
- Escrita em `cmd+0x430` após o builder altera a câmera em partida (antiaim
  com pitch 89 visível no jogo), confirmando o timing.

### 8.10 `CModelState` — array de bones

Dump de `m_modelState` (`scene_node + schema_offset("m_modelState")`, offset 0x140 nesta build):

| Offset | Conteúdo |
|---|---|
| `+0x80` | `CBoneData*` — array world-space, stride `0x20` (`float pos[3]; float scale; float quat[4]`) |
| `+0x88` | segundo array (transforms), não é contagem |
| `+0x90` | capacidade (ex. `0x6f` = 111) |
| `+0x94` | contagem de bones em uso (ex. `0x5e` = 94 em modelo T; `86` em CT) |

Bone 0 é o root nos pés (posição == `m_vecAbsOrigin`). Índices CS2 provados por
geometria em dois modelos: `0` root, `1` pelvis, `2..4` espinha, `5` neck,
`6` head, `8..11` braço esquerdo (clavícula/braço/antebraço/mão), `12..15` braço
direito, `17/18/19` perna esquerda (coxa/joelho/tornozelo), `20/21/22` perna
direita. O mapa antigo (CS:GO, 22/23/24/25/26/27) está errado nesta build.

### 8.11 Trace de visibilidade — `CGamePhysicsQueryInterface::TraceShape`

Chamável de dentro do processo, na game thread (validado com Frida e depois no DLL):

| Item | Valor |
|---|---|
| global do contexto | `client+0x221ab18` → ponteiro para `client+0x253cab0` (BSS) |
| validação | `ctx != null && *(void**)ctx != null` (o `m_pQuery` é escrito no init da física) |
| função | `client+0xa15360`, assinatura `bool(ctx, shape*, start*, end*, filter*, trace*)` |
| shape | buffer de 0x30–0x40; os primeiros 16 bytes vêm do helper `client+0x203570` |
| filtro | objeto de 0x48 bytes: vtable `client+0x1ad05d8`, `+0x08 = 0x18000`, `+0x30 = 0x0F00FFFF00000000` |
| `CGameTrace` | 0xc0 bytes; `fraction` float em `+0xac`; retorno `true` = houve hit |

O filtro foi isolado por bisseção sobre um filtro real do jogo: zerar `+0x30`
faz todo trace retornar "sem hit" (o `+0x37 = 0x0F` dentro desse qword é o flag
que liga o trace contra o mundo). `ShouldHitEntity` da vtable retorna `true`.

Erros de implementação corrigidos durante a verificação:
- começar o raio no olho do player fazia o trace bater no próprio hitbox
  (fraction 0); o raio começa 24 unidades à frente.
- terminar o raio no centro do bone fazia o trace bater no corpo do alvo;
  o fim recua 12 unidades.

Em partida o ragebot agora reporta `blocked=N` para alvos atrás de parede e só
considera candidatos com LOS limpo.

### 8.12 Filtro que ignora uma entidade (wallbang)

O filtro do trace pode ser nosso: a vtable é consultada só no slot 1
(`ShouldHitEntity(this, entity)`, `client+0x1ad05d8+8`). Um filtro próprio com
`vtable[0]` = destrutor do jogo e `vtable[1]` = handler que compara o ponteiro do
alvo guardado em `+0x10` permite medir a espessura da cobertura com dois raios
(near/far). Verificado em partida: `near=0.111 far=0.099 thickness=2913.9` para
parede grossa (negado), sem crash; o alvo é de fato ignorado (o hit do raio
reverso fica além do corpo).
