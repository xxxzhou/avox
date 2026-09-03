# engines

Thin engine wrappers over the avox C API / C++ core.

| target | form | plan |
|--------|------|------|
| Godot 4 | GDExtension (avox_godot) | M2 — open source, Asset Store |
| Unity | native package + C# shim | M3 — marketplace |
| Unreal | plugin | M3+ |

Wrapper principle: engine code stays dumb; all media logic lives in avox core/plugins.
