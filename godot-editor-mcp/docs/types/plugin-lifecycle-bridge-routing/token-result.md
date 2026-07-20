# Type: token result

**Sources:** `token_store.gd`, `authenticated_startup.gd`

Internal success/failure dictionary returned by credential loading/creation. Success contains a validated bounded token that was read from durable storage or generated, written, and flushed. Failure contains a stable bounded error envelope and no usable credential.

`authenticated_startup.gd` invokes service/listener/discovery composition only for success. Never allow an in-memory generated token to start the bridge after persistence failure: the Python process could not authenticate to it.
