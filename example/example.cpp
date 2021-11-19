#include <filesystem>
#include <fstream>

#include "zipper.h"

namespace fs = std::filesystem;

static const char *zipname = "test.zip";
static const char *f1_name = "Moneypenny.txt";
static const char *f2_name = "Bond.txt";
static const char *f3_name = "Up/M.txt";
static const char *f4_name = "Down/Q.tt";
static const char *f1_data = "secretary\n";
static const char *f2_data = "secret agent\n";
static const char *f3_data = "top guy\n";
static const char *f4_data = "bottom guy\n";
static const char *d1_name = "Up/";
static const char *d2_name = "Around/";
static const char *d3_name = "Bound/A/B";

static bool create_test_zip(void) {
  zipper::Zip zip;

  if (!zip.open(zipname)) {
    printf("Could not open %s for zipping\n", zipname);
    return false;
  }

  printf("adding dir: %s\n", d2_name);
  if (!zip.add_dir(d2_name)) {
    printf("failed to write dir %s\n", d2_name);
    return false;
  }

  printf("adding dir: %s\n", d3_name);
  if (!zip.add_dir(d3_name)) {
    printf("failed to write dir %s\n", d3_name);
    return false;
  }

  printf("adding file: %s\n", f1_name);
  if (!zip.add_buf(f1_name, (const unsigned char *)f1_data,
                      strlen(f1_data))) {
    printf("failed to write %s\n", f1_name);
    return false;
  }

  printf("adding file: %s\n", f2_name);
  if (!zip.add_buf(f2_name, (const unsigned char *)f2_data,
                      strlen(f2_data))) {
    printf("failed to write %s\n", f2_name);
    return false;
  }

  printf("adding file: %s\n", f3_name);
  if (!zip.add_buf(f3_name, (const unsigned char *)f3_data,
                      strlen(f3_data))) {
    printf("failed to write %s\n", f3_name);
    return false;
  }

  printf("adding dir: %s\n", d1_name);
  if (!zip.add_dir(d1_name)) {
    printf("failed to write dir %s\n", d1_name);
    return false;
  }

  printf("adding file: %s\n", f4_name);
  if (!zip.add_buf(f4_name, (const unsigned char *)f4_data,
                      strlen(f4_data))) {
    printf("failed to write %s\n", f4_name);
    return false;
  }

  return true;
}

static bool unzip_test_zip(void) {
  zipper::UnZip unzip;

  if (!unzip.open(zipname)) {
    printf("Could not open %s for unzipping\n", zipname);
    return false;
  }

  if (!unzip.filename().empty()) {
    do {
      auto zfilename = fs::path("out") / unzip.filename();

      if (unzip.isdir()) {
        printf("reading dir: %s\n", unzip.filename().c_str());
        fs::create_directories(zfilename);
      } else {
        auto len = unzip.filesize();
        printf("reading file (%llu bytes): %s\n", len,
               unzip.filename().c_str());

        std::string buf;
        if (unzip.read_buf(buf) == false) {
          printf("failed to read file\n");
          break;
        }

        fs::create_directories(zfilename.parent_path());
        {
          std::ofstream f(zfilename, std::ios::out);
          f.write(buf.data(), buf.size());
        }
      }
    } while (unzip.skip_file());
  }

  return true;
}

int main(int argc, char **argv) {
  if (!create_test_zip()) return 1;
  if (!unzip_test_zip()) return 1;
  return 0;
}
