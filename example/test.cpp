// Checks what zipper.h promises: archives written and read on disk and in
// memory, entries found by name, and a reason for every failure.
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

#include "zipper.h"

static unsigned long le(const std::string &b, size_t at, int n) {
  unsigned long v = 0;
  for (int i = n - 1; i >= 0; i--) {
    v = (v << 8) | static_cast<unsigned char>(b[at + i]);
  }
  return v;
}

// Where each central directory header starts.
static std::vector<size_t> central_headers(const std::string &b) {
  std::vector<size_t> at;
  for (auto i = b.find("PK\x01\x02"); i != std::string::npos;
       i = b.find("PK\x01\x02", i + 4)) {
    at.push_back(i);
  }
  return at;
}

// The compressed bytes of the archive's first entry start here.
static size_t first_data(const std::string &b) {
  auto lh = b.find("PK\x03\x04");
  return lh + 30 + le(b, lh + 26, 2) + le(b, lh + 28, 2);
}

// Why reading `name` out of `archive` fails.
static std::string read_error(const std::string &archive,
                              const std::string &name) {
  zipper::UnZip unzip;
  assert(unzip.open_memory(archive.data(), archive.size()));
  assert(unzip.locate(name));
  std::string buf;
  assert(!unzip.read(buf));
  return unzip.error();
}

static std::string make_archive() {
  zipper::Zip zip;
  assert(zip.open_memory());
  assert(zip.add_dir("docs"));
  assert(zip.add_file("docs/a.txt", "alpha"));
  assert(zip.add_file("empty.txt", ""));
  assert(zip.add_file(std::string(300, 'n') + ".txt", "long name"));
  std::string big(200000, '\0');
  for (size_t i = 0; i < big.size(); i++) {
    big[i] = static_cast<char>(i * 7);
  }
  assert(zip.add_file("big.bin", big));
  assert(zip.close());
  assert(!zip.buffer().empty());
  return zip.buffer();
}

int main() {
  auto bytes = make_archive();

  {
    // Every entry, in the order written.
    zipper::UnZip unzip;
    assert(unzip.open_memory(bytes.data(), bytes.size()));
    std::vector<std::string> names;
    unzip.enumerate([&](auto &u) { names.push_back(u.file_path()); });
    assert(names.size() == 5);
    assert(names[0] == "docs/");
    assert(names[3] == std::string(300, 'n') + ".txt");
  }

  {
    // Entries by name, their bytes and sizes.
    zipper::UnZip unzip;
    assert(unzip.open_memory(bytes.data(), bytes.size()));
    std::string buf;
    assert(unzip.locate("docs/a.txt") && unzip.is_file());
    assert(unzip.read(buf) && buf == "alpha");
    buf.clear();
    assert(unzip.locate("empty.txt") && unzip.file_size() == 0);
    assert(unzip.read(buf) && buf.empty());
    buf.clear();
    assert(unzip.locate("big.bin") && unzip.file_size() == 200000);
    assert(unzip.read(buf) && buf.size() == 200000 && buf[1] == 7);
    assert(unzip.locate("docs/") && unzip.is_dir());
    assert(!unzip.locate("nothing.txt"));
    assert(unzip.error() == "no entry nothing.txt");
  }

  {
    // The same archive through a file.
    {
      zipper::Zip zip("test_memory.zip");
      assert(zip.is_open());
      zipper::UnZip from;
      assert(from.open_memory(bytes.data(), bytes.size()));
      from.enumerate([&](auto &u) {
        std::string buf;
        if (u.is_dir()) {
          assert(zip.add_dir(u.file_path()));
        } else {
          assert(u.read(buf));
          assert(zip.add_file(u.file_path(), buf));
        }
      });
    }
    zipper::UnZip unzip("test_memory.zip");
    std::string buf;
    assert(unzip.locate("big.bin") && unzip.read(buf) && buf.size() == 200000);
  }

  {
    // Made on Unix with a mode, on a fixed valid date, so the same entries
    // make the same bytes.
    assert(make_archive() == bytes);
    auto headers = central_headers(bytes);
    assert(headers.size() == 5);
    for (auto at : headers) {
      assert(le(bytes, at + 4, 2) >> 8 == 3);
      assert(le(bytes, at + 12, 4) == 0x00410000); // 1980-02-01 00:00
    }
    assert(le(bytes, headers[0] + 38, 4) == ((040755ul << 16) | 0x10));
    assert(le(bytes, headers[1] + 38, 4) == 0100644ul << 16);
  }

  {
    // Failures say why.
    zipper::UnZip unzip;
    std::string junk = "this is not a zip archive at all";
    assert(!unzip.open_memory(junk.data(), junk.size()));
    assert(unzip.error() == "not a ZIP archive, or a damaged one");
    assert(!unzip.open("no-such-file.zip"));
    assert(unzip.error() == "cannot open no-such-file.zip as a ZIP archive");

    // An entry whose bytes do not match the CRC its headers record.
    zipper::Zip zip;
    assert(zip.open_memory());
    assert(zip.add_file("s.txt", "some text"));
    assert(zip.close());
    std::string damaged = zip.buffer();
    for (auto at : {damaged.find("PK\x03\x04") + 14, damaged.find("PK\x01\x02") + 16}) {
      damaged[at] = static_cast<char>(damaged[at] ^ 0x01);
    }
    assert(read_error(damaged, "s.txt") == "the data does not match its CRC");

    // Compressed data that deflate cannot make sense of.
    std::string text(1000, 'x');
    assert(zip.open_memory());
    assert(zip.add_file("x.txt", text));
    assert(zip.close());
    damaged = zip.buffer();
    damaged[first_data(damaged)] = '\xff'; // final block of reserved type 3
    assert(read_error(damaged, "x.txt") == "the compressed data is damaged");

    // Compressed data that stops before its stream does: both headers
    // record only the first two bytes of it.
    damaged = zip.buffer();
    for (auto at : {damaged.find("PK\x03\x04") + 18, damaged.find("PK\x01\x02") + 20}) {
      damaged.replace(at, 4, std::string("\x02\0\0\0", 4));
    }
    assert(read_error(damaged, "x.txt") == "the compressed data ends early");
  }

  {
    // An archive opened in memory reads whatever bytes it is pointed at
    // now; the ones it was opened on can go.
    std::string lent = bytes;
    zipper::UnZip unzip;
    assert(unzip.open_memory(lent.data(), lent.size()));
    assert(unzip.locate("big.bin"));
    std::string other = bytes;
    assert(unzip.rebind_memory(other.data(), other.size()));
    std::fill(lent.begin(), lent.end(), '\0');
    std::string buf;
    assert(unzip.read(buf) && buf.size() == 200000 && buf[1] == 7);
    buf.clear();
    assert(unzip.locate("docs/a.txt") && unzip.read(buf) && buf == "alpha");

    // Lent nothing, reads fail rather than crash, and work again once the
    // bytes come back.
    assert(unzip.rebind_memory(nullptr, 0));
    buf.clear();
    assert(!unzip.read(buf) && !unzip.error().empty());
    assert(!unzip.locate("empty.txt"));
    assert(unzip.file_path().empty() && unzip.file_size() == 0);
    assert(!unzip.first());
    assert(unzip.rebind_memory(other.data(), other.size()));
    assert(unzip.locate("docs/a.txt") && unzip.read(buf) && buf == "alpha");

    // Not while an entry is being read.
    assert(unzip.locate("big.bin"));
    size_t chunks = 0;
    buf.clear();
    assert(unzip.read([&](const char *data, size_t len) {
      assert(!unzip.rebind_memory(nullptr, 0));
      buf.append(data, len);
      chunks++;
    }));
    assert(chunks > 1 && buf.size() == 200000);
    assert(unzOpenCurrentFile(unzip) == UNZ_OK);
    assert(!unzip.rebind_memory(nullptr, 0));
    assert(unzCloseCurrentFile(unzip) == UNZ_OK);
    assert(unzip.rebind_memory(other.data(), other.size()));

    // Nor for an archive not opened in memory.
    unzip.close();
    assert(!unzip.rebind_memory(other.data(), other.size()));
    zipper::UnZip from_file("test_memory.zip");
    assert(from_file.is_open());
    assert(!from_file.rebind_memory(other.data(), other.size()));
  }

  {
    // An empty archive has no entries.
    zipper::Zip zip;
    assert(zip.open_memory() && zip.close());
    zipper::UnZip unzip;
    assert(unzip.open_memory(zip.buffer().data(), zip.buffer().size()));
    int count = 0;
    unzip.enumerate([&](auto &) { count++; });
    assert(count == 0);
    assert(!unzip.first());
    assert(unzip.error().empty());
  }

  std::cout << "ok" << std::endl;
  return 0;
}
