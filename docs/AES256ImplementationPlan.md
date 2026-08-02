# AES-256 Revision 6 implementation plan

Pdf++ currently implements the PDF Standard Security Handler revisions 3 and 4
(RC4-128 and AES-128). AES-256 revision 6 must be added as a separate security
profile; it cannot reuse the revision 4 password and file-key derivation.

## Kernel requirements

1. Add SHA-256 and AES-256-CBC primitives with known-answer tests.
2. Normalize passwords as UTF-8, truncate to 127 bytes, and append the required
   validation salt and key salt values.
3. Generate and validate 48-byte `/U` and `/O` entries using the revision 6
   hash rounds, including the 64-byte `/UE` and `/OE` encrypted file keys.
4. Generate and validate the 16-byte `/Perms` block, including the metadata and
   `adb` validation markers.
5. Emit `/V 5 /R 6 /Length 256` with `/CFM /AESV3` and the required `/StdCF`
   crypt filter. Object encryption uses AES-256 with a random 16-byte IV.
6. Preserve the existing permission checks and incremental writer behavior; an
   authenticated owner must bypass user permission restrictions exactly as it
   does for revisions 3 and 4.

## Compatibility and safety invariants

- Revision 3/4 files must remain byte-compatible with the current implementation.
- Revision 6 files must be rejected explicitly when authentication data is
  incomplete or malformed; no silent fallback to AES-128 is allowed.
- Passwords and key material must not be logged or retained in shared caches.
- Every new primitive must have deterministic known-answer tests and round-trip
  tests through both the reader and password manager.

## Implementation order

The standalone SHA-256/AES-256 primitive layer is now implemented in
`Internal/Security/PdfCrypto.hpp/.cpp`. It is covered by the SHA-256 `abc`
vector, the NIST AES-256 block vector, and an AES-256 decrypt round trip. The
next milestone is password normalization and revision-6 key derivation; the
Standard Security Handler dictionary will remain unchanged until that layer is
validated.
