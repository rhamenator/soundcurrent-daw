# Third-party notices

## Shipped source

**JSON for Modern C++ 3.12.0**, copyright Niels Lohmann, MIT license. The original copyright and permission text is retained in [LICENSE.MIT](third_party/nlohmann/LICENSE.MIT) and the vendored header. Exact source URLs and SHA-256 hashes are in [the manifest](third_party/manifest.json). Used only on control/worker threads; no JSON code enters a real-time callback.

## Linked system dependencies of the current core

- Linux: OpenSSL Crypto, version **3.5.5** in this development build. Apache-2.0; [upstream license](https://github.com/openssl/openssl/blob/openssl-3.5.5/LICENSE.txt). It supplies worker-side media SHA-256. Source and binaries are not vendored. Distribution packaging must retain the actual linked version's notices/license and audit transitives.
- Windows: operating-system BCrypt and Win32 file APIs. No additional crypto DLL distributed. The current MinGW cross-build also uses its C++ runtime; runtime deployment/license notices must be audited before packaging.
- C++ standard/compiler runtimes: system components for development; exact binary runtime dependencies require platform package inventory before release.

Qt, PipeWire, JACK, plugin SDKs and other planned libraries are not linked by the current root build. The isolated `experiments/` build has separate dependencies described in the [inventory](docs/03-dependencies.md). There is no installable DAW package yet.
