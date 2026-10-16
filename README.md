# Huffman compression tool

A C++17 command-line tool that displays byte frequencies and Huffman codes,
compresses files into packed binary data, and restores the original bytes.
Text (including UTF-8) and binary files are both supported.

## Build and use

Requires a C++17 compiler (`clang++` by default).

```sh
make

# Show the frequency and Huffman code table only:
./main test.txt

# Compress, showing the table and input/output sizes:
./main -c test.txt -o test.huff

# Rebuild the tree from the header and decompress:
./main -d test.huff -o restored.txt

# Verify that the restored file is identical:
cmp test.txt restored.txt

# Compare file sizes:
wc -c test.txt test.huff
```

`./main` defaults to displaying the table for `test.txt`. Compression and
decompression require both an input filename and `-o` for the output filename.
Long options are `--compress`, `--decompress`, `--output`, and `--help`.
Use `--` before input filenames beginning with a dash. Quote filenames containing
spaces.

Input and output must be different files, including hard links and symbolic
links to the same file. Output is written to a temporary file beside the target
and published only after processing succeeds. A successful operation replaces
an existing output file. Errors return a nonzero exit status.

Small files and files with nearly uniform byte frequencies can grow because
of the header and byte padding. Repeated text usually compresses well.

## File format (HUF1)

All integers are unsigned and stored in big-endian order.

| Field | Size | Meaning |
| --- | --- | --- |
| Magic/version | 4 bytes | ASCII `HUF1` |
| Symbol count | 2 bytes | Number of distinct bytes, from 0 to 256 |
| Frequency entries | 9 bytes each | One byte symbol followed by its 8-byte frequency |
| Payload | Remaining bytes | Huffman bits, packed most-significant bit first |

The header ends after `6 + 9 * symbol_count` bytes. Frequencies are written in
ascending byte order. The decoder reconstructs the same tree by repeatedly
merging the two lightest subtrees; equal weights are ordered by the smallest
byte value in each subtree. The first subtree is the left (`0`) branch and the
second is the right (`1`) branch.

The sum of the frequencies gives the original byte count. The frequencies and
rebuilt code lengths also give the exact payload bit count. The last byte is
padded with zero bits, which are excluded from decoding. Empty inputs have a
six-byte header and no payload. A single distinct byte receives code `0`.

The decoder checks the signature, frequency entries, integer overflow, payload
length, padding, completed codes, and decoded frequencies. This format has no
checksum: corruption that preserves valid codes and frequencies may go undetected.

## Tests

Python 3.9 or newer is required to run the integration tests:

```sh
make test
```

Tests cover byte-for-byte restoration of text, UTF-8, all 256 byte values,
random bytes, empty and single-symbol files, deterministic output, exact header
and bit packing, size reduction, invalid files, CLI errors, and output protection.
The local `test.txt` sample is also checked when present.
