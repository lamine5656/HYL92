#!/usr/bin/env bash
# Called from bootstrapper CMake with HYL92_BG_INPUT/HYL92_BG_OUTPUT set.
#
# LZMA-alone (-F lzma) so the runtime decoder sees the classic 13-byte CLI
# header (5 props bytes + 8-byte LE uncompressed size), exactly like
# pack_bootstrapper.sh does for bootstrapper.elf.
set -euo pipefail

input="${HYL92_BG_INPUT:?HYL92_BG_INPUT not set}"
output="${HYL92_BG_OUTPUT:?HYL92_BG_OUTPUT not set}"
compressor="${HYL92_BG_XZ:?HYL92_BG_XZ not set}"
[[ -f "${input}" ]] || { echo "missing ${input}" >&2; exit 1; }
mkdir -p "$(dirname "${output}")"

if [[ "$(basename "${compressor}")" == "lzma" ]]; then
  "${compressor}" -9 -c "${input}" > "${output}"
else
  "${compressor}" -F lzma -9 -c "${input}" > "${output}"
fi

[[ -s "${output}" ]] || { echo "compression produced no output" >&2; exit 1; }
echo "hyl92 bg: $(wc -c < "${input}") -> $(wc -c < "${output}") bytes"
