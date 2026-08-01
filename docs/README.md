# Pdf++ documentation

This directory contains current project documentation, testing guidance, and
architecture decisions. Version history is maintained in
[CHANGELOG.md](../CHANGELOG.md).

## Core documentation

- [Feature matrix](FeatureMatrix.md) - supported features and current limitations.
- [Performance guide](Performance.md) - cache, memory-mapped input, streaming output, and benchmarks.
- [Test coverage](TestCoverage.md) - mapping between features and tests.
- [Cross-engine validation](CrossEngineValidation.md) - compatibility checks against other engines.
- [Public release checklist](PublicReleaseChecklist.md) - pre-release checks.
- [Keyword highlight example](Keyword_Highlight_Example.md) - search and highlighting API example.

## Architecture

- [Project structure](architecture/ProjectStructure.md) - library directories and modules.
- [Win32 reader architecture](../apps/PdfPP.Win32/ARCHITECTURE.md) - Windows viewer architecture.

## Tools and test data

- [Validation tools](../tools/validation/README.md) - cross-engine validation.
- [Fuzzing](../tools/fuzz/README.md) - build and run the reader fuzz target.
- [Malformed PDF corpus](../tests/corpus/README.md) - malformed input test data.
