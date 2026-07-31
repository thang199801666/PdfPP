# Image patch dependency compatibility fix

This cumulative compatibility patch synchronizes the image extraction/writer changes with the required CTM, Form XObject, typed stream, and positioned text extraction APIs.

The previous image-only patch contained a newer `PdfDocument.cpp` but did not include all prerequisite headers and implementations. This caused compile errors involving:

- `PdfContentProcessor::Process(content, initialState)`
- `PdfTextStateSnapshot::currentTransformationMatrix`
- `PdfTextExtractionRequest::initialTransformationMatrix`
- `PdfTextExtractionRequest::xObjectHandler`
- matrix helpers and image extraction helper signatures

The files in this patch come from the same tested source tree and must be copied together.

After copying:

1. Close Visual Studio.
2. Delete `artifacts/obj`.
3. Reopen `Pdf++.sln`.
4. Select x64 Debug or Release.
5. Clean Solution, then Rebuild Solution.

Verification performed:

- C++20 core build passed.
- Sample build passed.
- Unit tests passed.
- Reader integration tests passed.
- Writer/image smoke tests passed.
- 3/3 CTest tests passed.
