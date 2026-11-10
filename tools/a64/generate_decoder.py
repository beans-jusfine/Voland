#!/usr/bin/env python3
from pathlib import Path
import gzip

# Keep the generated decoder logic in a pinned, compressed source artifact.
_impl = Path(__file__).with_name("generate_decoder_impl.py.gz")
_source = gzip.decompress(_impl.read_bytes())
_scope = {"__name__": "__main__", "__file__": str(Path(__file__).resolve())}
exec(compile(_source, str(_impl), "exec"), _scope)
