#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;
using Frequencies = array<uint64_t, 256>;
using Codes = array<string, 256>;

struct Node {
  uint64_t weight;
  unsigned int symbol;
  shared_ptr<Node> left, right;
  bool isLeaf() const { return !left; }
};

struct CompareNode {
  bool operator()(const shared_ptr<Node> &a, const shared_ptr<Node> &b) const {
    if (a->weight != b->weight)
      return a->weight > b->weight;
    // Break ties by the smallest byte in each subtree, so rebuilding is exact.
    return a->symbol > b->symbol;
  }
};

uint64_t checkedAdd(uint64_t a, uint64_t b) {
  if (b > numeric_limits<uint64_t>::max() - a)
    throw runtime_error("File is too large or its header contains overflowing frequencies.");
  return a + b;
}

shared_ptr<Node> buildTree(const Frequencies &freq) {
  priority_queue<shared_ptr<Node>, vector<shared_ptr<Node>>, CompareNode> heap;
  for (unsigned int i = 0; i < freq.size(); ++i)
    if (freq[i])
      heap.push(make_shared<Node>(Node{freq[i], i, nullptr, nullptr}));
  while (heap.size() > 1) {
    auto left = heap.top();
    heap.pop();
    auto right = heap.top();
    heap.pop();
    heap.push(make_shared<Node>(Node{checkedAdd(left->weight, right->weight),
                                    min(left->symbol, right->symbol), left, right}));
  }
  return heap.empty() ? nullptr : heap.top();
}

void generateCodes(const shared_ptr<Node> &node, const string &prefix, Codes &codes) {
  if (!node)
    return;
  if (node->isLeaf()) {
    codes[node->symbol] = prefix.empty() ? "0" : prefix;
    return;
  }
  generateCodes(node->left, prefix + "0", codes);
  generateCodes(node->right, prefix + "1", codes);
}

string formatChar(unsigned int c) {
  if (c == '\n') return "\\n";
  if (c == '\t') return "\\t";
  if (c == '\r') return "\\r";
  if (c == ' ') return "' '";
  if (isprint(static_cast<unsigned char>(c))) return string(1, static_cast<char>(c));
  ostringstream out;
  out << "0x" << hex << uppercase << setw(2) << setfill('0') << c;
  return out.str();
}

void printTable(const Frequencies &freq, const Codes &codes) {
  cout << "\nCharacter | Frequency            | Huffman Code\n"
       << "----------+----------------------+-------------------------\n";
  for (unsigned int i = 0; i < freq.size(); ++i)
    if (freq[i])
      cout << left << setw(9) << formatChar(i) << " | " << setw(20)
           << freq[i] << " | " << codes[i] << '\n';
}

// Fixed-width, big-endian integers make the format independent of the host.
void writeInteger(ostream &out, uint64_t value, unsigned int bytes) {
  for (unsigned int i = bytes; i > 0; --i)
    out.put(static_cast<char>((value >> ((i - 1) * 8)) & 0xff));
}

uint64_t readInteger(istream &in, unsigned int bytes) {
  uint64_t value = 0;
  for (unsigned int i = 0; i < bytes; ++i) {
    int byte = in.get();
    if (byte == char_traits<char>::eof())
      throw runtime_error("Truncated compressed file header.");
    value = (value << 8) | static_cast<unsigned char>(byte);
  }
  return value;
}

void writeHeader(ostream &out, const Frequencies &freq) {
  out.write("HUF1", 4);
  unsigned int count = 0;
  for (auto frequency : freq)
    if (frequency) ++count;
  writeInteger(out, count, 2);
  for (unsigned int i = 0; i < freq.size(); ++i) {
    if (freq[i]) {
      writeInteger(out, i, 1);
      writeInteger(out, freq[i], 8);
    }
  }
}

Frequencies readHeader(istream &in) {
  char magic[4];
  if (!in.read(magic, 4) || string(magic, 4) != "HUF1")
    throw runtime_error("Not a supported HUF1 compressed file.");
  auto count = readInteger(in, 2);
  if (count > 256)
    throw runtime_error("Invalid symbol count in header.");
  Frequencies freq{};
  uint64_t total = 0;
  for (uint64_t i = 0; i < count; ++i) {
    auto symbol = readInteger(in, 1);
    auto frequency = readInteger(in, 8);
    if (!frequency || freq[symbol])
      throw runtime_error("Zero frequency or duplicate symbol in header.");
    total = checkedAdd(total, frequency);
    freq[symbol] = frequency;
  }
  return freq;
}

uint64_t encodedBits(const Frequencies &freq, const Codes &codes) {
  uint64_t bits = 0;
  for (unsigned int i = 0; i < freq.size(); ++i) {
    auto length = codes[i].size();
    if (length && freq[i] > numeric_limits<uint64_t>::max() / length)
      throw runtime_error("Encoded bit count is too large.");
    bits = checkedAdd(bits, freq[i] * length);
  }
  return bits;
}

// Publish only complete output; failed reads or decoding leave existing files intact.
class OutputFile {
  fs::path target, temporary;
  ofstream stream;
public:
  explicit OutputFile(const fs::path &path) : target(path) {
    random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
      temporary = target;
      temporary += ".tmp." + to_string(random()) + "." + to_string(random());
      if (!fs::exists(temporary)) break;
      if (attempt == 99) throw runtime_error("Could not choose a temporary output file.");
    }
    stream.open(temporary, ios::binary | ios::trunc);
    if (!stream) throw runtime_error("Could not create output file: " + target.string());
  }
  ~OutputFile() {
    stream.close();
    error_code ignored;
    fs::remove(temporary, ignored);
  }
  ostream &get() { return stream; }
  void commit() {
    stream.flush();
    if (!stream) throw runtime_error("Failed to write output file.");
    stream.close();
    if (stream.fail()) throw runtime_error("Failed to close output file.");
    fs::rename(temporary, target);
  }
};

Frequencies countFrequencies(istream &in) {
  Frequencies freq{};
  char c;
  while (in.get(c)) {
    auto &frequency = freq[static_cast<unsigned char>(c)];
    frequency = checkedAdd(frequency, 1);
  }
  if (!in.eof() || in.bad()) throw runtime_error("Failed to read input file.");
  return freq;
}

void compress(istream &in, const fs::path &output, const Frequencies &freq,
              const Codes &codes) {
  encodedBits(freq, codes); // Validate size before producing output.
  in.clear();
  in.seekg(0);
  if (!in) throw runtime_error("Could not rewind input file.");
  OutputFile file(output);
  auto &out = file.get();
  writeHeader(out, freq);
  unsigned int byte = 0, used = 0;
  Frequencies observed{};
  char c;
  while (in.get(c)) {
    auto symbol = static_cast<unsigned char>(c);
    if (++observed[symbol] > freq[symbol])
      throw runtime_error("Input changed during compression.");
    for (char bit : codes[symbol]) {
      byte = (byte << 1) | (bit == '1');
      if (++used == 8) {
        out.put(static_cast<char>(byte));
        byte = used = 0;
      }
    }
  }
  if (!in.eof() || in.bad()) throw runtime_error("Failed to read input file.");
  if (observed != freq) throw runtime_error("Input changed during compression.");
  if (used) out.put(static_cast<char>(byte << (8 - used)));
  file.commit();
}

void decompress(istream &in, const fs::path &output) {
  auto freq = readHeader(in);
  auto tree = buildTree(freq);
  Codes codes{};
  generateCodes(tree, "", codes);
  auto bits = encodedBits(freq, codes);
  auto dataStart = in.tellg();
  in.seekg(0, ios::end);
  auto dataEnd = in.tellg();
  auto bytes = bits / 8 + (bits % 8 != 0);
  if (dataStart == streampos(-1) || dataEnd == streampos(-1) ||
      dataEnd < dataStart || static_cast<uint64_t>(dataEnd - dataStart) != bytes)
    throw runtime_error("Compressed payload is truncated or has trailing data.");
  in.seekg(dataStart);
  if (!in) throw runtime_error("Could not read compressed payload.");
  OutputFile file(output);
  auto &out = file.get();
  auto remaining = freq;
  auto node = tree;
  uint64_t decoded = 0;
  for (uint64_t offset = 0; offset < bytes; ++offset) {
    int byte = in.get();
    if (byte == char_traits<char>::eof())
      throw runtime_error("Truncated compressed payload.");
    unsigned int used = (offset == bytes - 1 && bits % 8) ? bits % 8 : 8;
    for (unsigned int i = 0; i < used; ++i) {
      bool bit = (byte >> (7 - i)) & 1;
      if (tree->isLeaf()) {
        if (bit) throw runtime_error("Invalid single-symbol code.");
      } else {
        node = bit ? node->right : node->left;
      }
      if (node->isLeaf()) {
        if (!remaining[node->symbol])
          throw runtime_error("Decoded frequencies do not match the header.");
        --remaining[node->symbol];
        out.put(static_cast<char>(node->symbol));
        ++decoded;
        node = tree;
      }
    }
    if (used < 8 && (byte & ((1 << (8 - used)) - 1)))
      throw runtime_error("Invalid nonzero padding bits.");
  }
  if (in.bad() || node != tree || decoded != (tree ? tree->weight : 0) ||
      remaining != Frequencies{})
    throw runtime_error("Incomplete or invalid compressed payload.");
  file.commit();
  cout << "Decompressed " << decoded << " bytes to " << output.string() << '\n';
}

void usage(ostream &out) {
  out << "Usage:\n"
      << "  ./main [input]                      Show frequency and Huffman code table\n"
      << "  ./main -c input -o output.huff      Compress a file (and show table)\n"
      << "  ./main -d input.huff -o output     Decompress a file\n"
      << "Options: --compress, --decompress, --output, --help\n";
}

int main(int argc, char *argv[]) {
  try {
    string input, output;
    char mode = 't';
    bool endOptions = false;
    for (int i = 1; i < argc; ++i) {
      string arg = argv[i];
      if (!endOptions && (arg == "--help" || arg == "-h")) {
        usage(cout);
        return 0;
      }
      if (!endOptions && arg == "--") { endOptions = true; continue; }
      if (!endOptions && (arg == "-c" || arg == "--compress" ||
                          arg == "-d" || arg == "--decompress")) {
        if (mode != 't') throw runtime_error("Choose one compression or decompression mode.");
        mode = (arg == "-c" || arg == "--compress") ? 'c' : 'd';
      } else if (!endOptions && (arg == "-o" || arg == "--output")) {
        if (!output.empty() || i + 1 == argc) throw runtime_error("Specify one output filename after -o.");
        output = argv[++i];
        if (output.empty()) throw runtime_error("Output filename cannot be empty.");
      } else if (!endOptions && !arg.empty() && arg[0] == '-') {
        throw runtime_error("Unknown option: " + arg);
      } else {
        if (!input.empty()) throw runtime_error("Specify exactly one input file.");
        input = arg;
      }
    }
    if (mode != 't' && (input.empty() || output.empty()))
      throw runtime_error("Compression and decompression require an input file and -o output.");
    if (mode == 't' && !output.empty()) throw runtime_error("Use -c or -d with -o.");
    if (input.empty()) input = "test.txt";
    ifstream in(input, ios::binary);
    if (!in || !fs::is_regular_file(input)) throw runtime_error("Could not open input file: " + input);
    if (!output.empty() && fs::exists(output) && fs::equivalent(input, output))
      throw runtime_error("Input and output must be different files.");
    if (mode == 'd') {
      decompress(in, output);
    } else {
      auto freq = countFrequencies(in);
      auto tree = buildTree(freq);
      Codes codes{};
      generateCodes(tree, "", codes);
      printTable(freq, codes);
      if (mode == 'c') {
        compress(in, output, freq, codes);
        cout << "\nCompressed " << (tree ? tree->weight : 0) << " bytes to "
             << fs::file_size(output) << " bytes: " << output << '\n';
      }
    }
    return 0;
  } catch (const exception &error) {
    cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
