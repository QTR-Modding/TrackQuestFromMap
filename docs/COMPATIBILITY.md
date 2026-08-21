# Compatibility

## Payload boundary

The release contains one SFSE DLL. It does not ship or overwrite:

- `surfacemap.swf` or any other interface file;
- ESM, ESP, or ESL records;
- Papyrus scripts;
- INI files; or
- Address Library files.

It stores no save data.

## UI compatibility

The plugin reads these public Surface Map concepts at runtime:

- the visible Surface Map container;
- its map and marker-container children;
- direct marker display objects;
- vanilla hover visibility clips; and
- raw marker-data fields for type, handle, flags, and text.

A UI replacer is compatible when it preserves that public hierarchy and
contract. Missing, differently typed, oversized, or ambiguous data fails open
to normal input. No UI replacer is patched by this project.

Galaxy/System support additionally reads the public `Markers_mc` root,
`SystemMarkerContainer_mc` and `BodyMarkerContainer_mc`, marker `bodyID`, and
the standard `MissionIconContainer` inactive glyph/nameplate hierarchy. The
inactive glyph is hit-tested at the Stage cursor. A replacer may change art,
timelines, layout, frame rate, or unrelated menu behavior while preserving
that public contract. If it replaces the contract, Galaxy/System activation
fails open and vanilla input continues.

## Native hook conflicts

Another DLL that replaces any of the same thirteen reviewed direct calls is a
hard conflict. The 0.3.0 candidate checks complete signatures and decoded
original targets before writing. It allocates all branch islands first,
verifies each write, and restores all original calls if a transaction cannot
complete. Input is installed last, after both ownership paths are ready.

## Runtime compatibility

Both the released 0.2.2 DLL and 0.3.0 candidate support only Steam Starfield
1.16.244.0 with SFSE 0.2.21 and the matching Address Library. They are
layout-dependent and refuse other runtimes.

Adding a runtime requires fresh proof for every relocation, signature, decoded
target, ABI, structure offset, vtable, lock assumption, and caller path. Do not
copy IDs or offsets forward because Address Library contains a number for a new
runtime.

## Known limitation

An ordinary location can aggregate multiple quest owners. Its exposed active
flag is an aggregate OR state. If one owner is active while the visible owner is
inactive, 0.2.2 declines the mod action rather than risk choosing incorrectly.
Vanilla input continues. This limitation does not apply to a standalone large
marker, whose active flag and single owner are per quest.

## Future maps

Galaxy and System views use their independently traced shared quest-target
tree; they do not reuse Surface rows. Orbital/planet overview and other map
menus remain unsupported and require their own data-flow trace and pull
request. New support should reuse only proven generic primitives such as
main-thread activation and transactional hook installation.
