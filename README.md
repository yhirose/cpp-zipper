cpp-zipper
===========

A single file C++ header-only minizip wrapper library

This code is based on 'Making MiniZip Easier to Use' by John Schember.
https://nachtimwald.com/2019/09/08/making-minizip-easier-to-use/

Example
-------

```cpp
#include <filesystem>
#include <iostream>

#include "zipper.h"

namespace fs = std::filesystem;

int main() {
  {
    zipper::Zip zip("test_copy.zip");

    zipper::enumerate("test.zip", [&zip](auto &unzip) {
      if (unzip.is_dir()) {
        zip.add_dir(unzip.file_path());
      } else {
        std::string buf;
        if (unzip.read(buf)) {
          zip.add_file(unzip.file_path(), buf);
        }
      }
    });
  }

  {
    zipper::UnZip zip0("test.zip");
    zipper::UnZip zip1("test_copy.zip");

    do {
      assert(zip0.is_dir() == zip1.is_dir());
      assert(zip0.is_file() == zip1.is_file());
      assert(zip0.file_path() == zip1.file_path());
      assert(zip0.file_size() == zip1.file_size());

      if (zip0.is_file()) {
        std::string buf0, buf1;
        assert(zip0.read(buf0) == zip1.read(buf1));
        assert(buf0 == buf1);
      }
    } while (zip0.next() && zip1.next());
  }

  return 0;
}
```

In memory
---------

An archive can be read from bytes already in memory, and written to memory
instead of a file. `open_memory(data, size)` does not copy the bytes, so they
must outlive the `UnZip`.

```cpp
std::string bytes;
{
  zipper::Zip zip;
  zip.open_memory();
  zip.add_file("hello.txt", "Hello, world!");
  zip.close();                 // the archive is complete once close() returns
  bytes = zip.buffer();
}

zipper::UnZip unzip;
unzip.open_memory(bytes.data(), bytes.size());
if (unzip.locate("hello.txt")) {  // find an entry by name
  std::string text;
  unzip.read(text);
}
```

An archive opened with `open_memory` can be pointed at other bytes later with
`rebind_memory(data, size)`, as long as they are the same archive: a caller
can lend its bytes only for each read and take them back in between with
`rebind_memory(nullptr, 0)`, during which reads fail instead of touching
memory. It returns `false` for an archive not opened in memory, or while an
entry is being read.

```cpp
zipper::UnZip unzip;
unzip.open_memory(bytes.data(), bytes.size());
unzip.rebind_memory(nullptr, 0);  // done with the bytes for now
// ...
unzip.rebind_memory(bytes.data(), bytes.size());
unzip.locate("hello.txt");
```

Entries
-------

`Zip` writes every entry as made on Unix with mode `0644` (directories
`0755`) and dated 1980-02-01 00:00, whatever the host, so the same entries
always make the same bytes, and `unzip` keeps UTF-8 names as they are.

Errors
------

Every call that can fail returns `false` and leaves the reason in `error()`:
a file that cannot be opened, bytes that are not a ZIP archive, an entry that
is not there, one that is encrypted or compressed with a method other than
stored or deflate, or data that does not match its CRC.

```cpp
zipper::UnZip unzip;
if (!unzip.open("archive.zip")) {
  std::cerr << unzip.error() << std::endl;
}
```

Tests
-----

```sh
cd example && make
```

License
-------

MIT license (© 2021 Yuji Hirose)
