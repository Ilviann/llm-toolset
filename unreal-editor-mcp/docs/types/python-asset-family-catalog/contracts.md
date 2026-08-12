# Python asset-family catalog contracts

## Entries and publications

`AssetFamilyPublication` has one stable non-empty `family_id` and either built-in tool publications or one exact companion identity with a positive schema revision and shipped contributions. `ToolPublication` binds one exact three-field MCP tool definition to `read` or `mutation` access, an optional native command, one `bridge`, `capabilities`, or `lifecycle` handler, one `json` or `safe_yaml` result handler, and finite exact native-command requirements.

`CompanionContribution` binds an exact tool, operation, access, and input-schema branch. `IntegratedSections` binds an exact native contribution to a non-empty unique semantic-section tuple. Companion entries define only Python-approved schemas and mappings; native records cannot add or replace them.

## Validation and freeze

`StaticAssetFamilyCatalog` consumes all entries once, rejects empty or duplicate family IDs, tool names, companion identities, contribution keys, and conflicting native command mappings, then exposes tuple entries and read-only mappings. It also rejects malformed public tool definitions, access classes, handlers, result policies, schema revisions, bridge mappings, native requirements, and integrated-section sets. There is no runtime registration or late mutation path.

## Composition

`compose` receives exact Boolean writable and lifecycle configuration plus optional native capabilities. It walks entries in shipped order, excludes mutation publications in readonly mode, excludes the local lifecycle publication when disabled, and intersects required commands when native `commands` is present. A missing command field means native availability is not yet authoritative; a present malformed or overlarge list satisfies no native requirement.

Composition deep-copies every published schema and returns an immutable name-to-publication mapping for server dispatch. Ready companion records are bounded to 64 records and 32 contributions each and require exact companion API, schema revision, identity, operation, access, and Python catalog matches. Companion mutation branches additionally require writable mode. Unknown tools or now-dormant integrated inspection routes add nothing.

## Dispatch and result handling

The server validates arguments against the composed schema, then dispatches through the selected publication. `bridge` handlers call the exact catalog command, `capabilities` composes local and native state, and `lifecycle` calls the configured local lifecycle collaborator. A `safe_yaml` result policy transforms only successful `asset_inspect` output at the final MCP text boundary; `json` preserves existing structured output for every other tool.

[Catalog index](index.md) · [Architecture](../../architecture/python-asset-family-catalog.md)
