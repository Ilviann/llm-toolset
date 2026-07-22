# Bounded reflected row-value codec

Top-level row `values` is an object of at most 64 live authored field names. Supported values are:

- Boolean, finite numeric, name/string/text, and exact enum-name scalars;
- compatible hard/soft object or class references as `{kind:"reference",path:"/…"}`, with an empty path for null;
- arrays as JSON arrays;
- sets as `{kind:"set",items:[…]}`;
- maps as `{kind:"map",entries:[{key:…,value:…}]}`; and
- common, native, or user-defined structs as `{kind:"struct",fields:{…}}`.

Containers hold at most 64 items. Nested values have a maximum depth of four, each nested struct has at most 64 fields, one operation touches at most 64 rows, and inspection refuses tables above the 2,048-row scan ceiling. Numeric writes must be finite, integral for integer properties, within the reflected property's range, and exactly representable in JSON's safe integer range.

Every field is resolved against the live `FProperty`. References must resolve to a compatible visible packageable object/class and must not be transient or editor-only. Instanced references, delegates, interfaces, arbitrary UObject graphs, raw import text, and properties outside the codec reject explicitly. `preserve_unspecified: true` begins from the existing row; otherwise staging begins from the row struct's live defaults.
