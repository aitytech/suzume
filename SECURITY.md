# Security policy

## Reporting a vulnerability

Report privately, not through the public issue tracker:

- **Preferred:** GitHub's private vulnerability reporting, from the repository's
  [Security tab](https://github.com/libraz/suzume/security/advisories/new).
- **Alternative:** email `libraz@libraz.net`.

Include what you sent in, what happened, the affected version, and a minimal
reproduction if you have one. Expect an acknowledgement within a few days.

## Supported versions

Pre-1.0. Fixes land on the latest release only; older versions are not patched.

## What is in scope

Suzume tokenizes text it did not produce, and loads a dictionary it is handed,
so both inputs are the interesting surface. In scope:

- Text that causes a crash, a hang, unbounded memory growth, or a read or write
  outside an allocation — in the C++ core or through any of the bindings.
- A crafted dictionary or model file that does the same, or that makes the
  tokenizer read a file it was not pointed at.
- Anything that escapes the WebAssembly sandbox, or that lets tokenized input
  reach the network or the filesystem from the browser build.

## What is not in scope

- Wrong segmentation. A boundary in the wrong place is an accuracy bug; report
  it as a normal issue.
- Documented limits behaving as documented — input length caps and the like.
  A limit that can be bypassed is in scope.
- Vulnerabilities in the text being tokenized, or in whatever consumes the
  tokens afterwards.
- Findings that need an attacker to already control the process or to replace
  the dictionary file on disk.
