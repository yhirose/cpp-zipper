// Checks what zipper.h promises: archives written and read on disk and in
// memory, entries found by name, and a reason for every failure.
#include <cassert>
#include <iostream>
#include <string>

#include "zipper.h"

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
    zipper::UnZip broken;
    assert(broken.open_memory(damaged.data(), damaged.size()));
    std::string buf;
    assert(broken.locate("s.txt"));
    assert(!broken.read(buf));
    assert(broken.error() == "the data does not match its CRC");
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
  }

  std::cout << "ok" << std::endl;
  return 0;
}
