# Library: text classification

**Source:** `rooted_files_mcp/filesystem.py`

`_reject_binary_name` denies a fixed set of binary/media extensions even if bytes might decode. `_reject_binary_bytes` denies common executable/archive/image/audio/document signatures and any NUL. `_decode_utf8` accepts UTF-8 with optional BOM stripping. `_validate_text_path` and `_read_text_bytes` require a regular file and enforce the 5 MiB bound before and during the bounded read.

Whole reads, ranged reads, existing-target writes, and replacement revalidation share this classification. Change extension, signature, encoding, or size policy for every path together.
