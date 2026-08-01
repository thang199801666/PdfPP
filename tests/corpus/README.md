# Malformed PDF corpus

These files are intentionally invalid seed inputs for reader regression tests and libFuzzer. They must be rejected without a crash or unbounded allocation; they are not expected to render.

- `truncated.pdf`: header without xref/trailer
- `bad-xref-offset.pdf`: valid-looking header with an invalid `startxref`
- `unterminated-object.pdf`: object with an unterminated dictionary
