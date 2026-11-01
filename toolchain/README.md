# Toolchain used for this build
- GCC 13.3.0 (`releases/gcc-13.3.0` from github.com/gcc-mirror/gcc), C only, `--target=bfin-elf`, in-tree GMP 6.3.0 /
  MPFR 4.2.2 / MPC 1.3.1 (official tarballs, sha256 a3c2b802…, b67ba038…, ab642492…), configure options as the Opus
  kit's `tools/toolchain/build-bfin-toolchain.sh`. Same GCC version the Opus kit uses.
- binutils 2.29.51 from deanm1278/blackfin-plus-gnu @ e4408a15 (ADI lineage; ftp.gnu.org was unreachable here, so not the
  kit's 2.38). flex 2.6.4 built from its GitHub release to generate the lexers.
- GNU Blackfin simulator (same tree) for the asm/C checks, with `bfin-sim-half-register-loads.patch`: the stock sim
  loses the first of two half-register loads into one register in a parallel instruction (`R0.L = W[I0++] || R0.H =
  W[I1++]`, used by SISTER's stereo mix kernel). Test-tool fix only; nothing shipped depends on it.
With network access to ftp.gnu.org, the Opus kit's own script is the simpler route.
