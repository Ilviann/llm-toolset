# Blueprint reference scanner

`UnrealMCPBlueprintReferenceScanner` is a private native collaborator shared by Blueprint inspection and mutation. It accepts one already-resolved Blueprint plus a typed variable, function graph, local scope, macro graph, or custom-event target and returns `FScanResult`; it never accepts JSON and never resolves model-supplied names by itself.

`FScanResult` carries the authoritative referenced flag, exact loaded-node count, unresolved-reference flag, truncation flag, and at most `MaxVariableReferences` typed node records. Records contain bounded graph/node identities, node class, and display title and are sorted by graph then node identity. Mutation uses the typed flags/count directly for reject-only policy. `Encode` is called only when inspection or a mutation result crosses the JSON wire boundary.
