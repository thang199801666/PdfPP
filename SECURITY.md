# Security Policy

## Supported versions

Pdf++ is currently in public beta. Security fixes are applied to the latest development branch and latest beta release only.

| Version | Supported |
|---|---|
| Latest `0.x` beta | Yes |
| Older `0.x` releases | No |

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability involving malformed PDFs, memory corruption, denial of service, path handling, embedded files, or parser limit bypasses.

Use GitHub's **Security → Report a vulnerability** private reporting form when enabled. If private reporting is unavailable, contact the repository owner privately through their GitHub profile and include:

- A minimal reproducer or proof of concept.
- Affected version or commit.
- Platform and compiler.
- Expected and observed behavior.
- AddressSanitizer or debugger output when available.

Please allow reasonable time for investigation and coordinated disclosure.

## Security scope and current limitations

Pdf++ is not yet hardened for arbitrary untrusted public uploads. Applications should use sandboxing, strict `PdfReaderLimits`, timeouts, memory limits, and independent output validation. Encryption, digital signatures, and complete PDF security handlers are not implemented.
