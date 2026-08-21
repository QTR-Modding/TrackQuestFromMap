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

## Native hook conflicts

Another DLL that replaces any of the same three reviewed direct calls is a hard
conflict. Version 0.2.2 checks the complete signatures and decoded original
targets before writing. It allocates all branch islands first, verifies each
write, and restores all original calls if a transaction cannot complete.

## Runtime compatibility

The 0.2.2 DLL supports only Steam Starfield 1.16.244.0 with SFSE 0.2.21 and the
matching Address Library. It is layout-dependent and refuses other runtimes.

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

Galaxy, system, orbital/planet overview, and other map views do not share the
Surface Map's native row layout or ownership path. Each requires an independent
data-flow trace and its own pull request. New support should reuse only proven
generic primitives such as main-thread activation and transactional hook
installation.
