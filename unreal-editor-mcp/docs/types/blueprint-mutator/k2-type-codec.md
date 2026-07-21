# Canonical K2 member type and default codec

Member-variable inspection and mutation share one bounded K2 vocabulary. A type record contains `category`, `container`, optional `subcategory`, optional `type_object`, and, for maps only, `value_type`. Supported categories are `boolean`, `byte`, `int`, `int64`, `real`, `name`, `string`, `text`, `enum`, `struct`, `object`, `class`, `softobject`, and `softclass`. Real values require `float` or `double`; enum, struct, object, and class families require one live compatible Unreal object path. Containers are `none`, `array`, `set`, or `map`; nested containers are unavailable.

Defaults are tagged objects rather than untyped Unreal serialization strings:

- `{kind:"engine_default"}` selects the type's default value.
- `{kind:"literal",value:...}` carries one Boolean, finite number, bounded text/name/enum value, or other supported scalar literal.
- `{kind:"reference",path:"..."}` carries one compatible hard/soft object or class path; an empty path is null.
- `{kind:"array"|"set",items:[...]}` carries at most 64 scalar/reference atoms.
- `{kind:"map",entries:[{key:...,value:...}]}` carries at most 64 scalar/reference pairs.

The native codec resolves type objects and references against live K2 capabilities, rejects incompatible types/defaults, and converts accepted values to Unreal's canonical property text internally. Non-default arbitrary struct literals remain explicitly unsupported. Inspection reconstructs tagged defaults from the variable description before compile and from the generated-class CDO after Unreal migrates compiled defaults there.
