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
