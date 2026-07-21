# Normalized inspection query and collectors

`FInspectionQuery` is the validated internal form of one initial inspection request. It contains the canonical object path, inherited-content flag, deduplicated section set, stable identity filters, and targeted property-name set. Cursor requests remain owned and validated by the inspector facade and replay the retained normalized arguments against an expected snapshot.

The builder resolves and loads only the requested asset, captures dirty/compile state once, and passes one record array plus one fingerprint array through overview/component/default, member, function/local, macro, custom-event, and graph collectors. Each collector owns its family encoding and exact not-found behavior. The builder enforces the shared structural bound, verifies non-mutation, and hashes the complete fingerprint once after all requested sections have observed the same structure.
