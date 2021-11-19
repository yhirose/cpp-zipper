#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

#include "zipper.h"

size_t write_file(const char *filename, const char *data, size_t len,
                  bool append) {
  if (filename == nullptr || *filename == '\0' || data == nullptr) return 0;

  auto f = fopen(filename, (append ? "a" : "w"));
  if (f == nullptr) return 0;

  size_t wrote = 0;

  {
    size_t r;
    do {
      r = fwrite(data, sizeof(char), len, f);
      wrote += r;
      len -= r;
      data += r;
    } while (r > 0 && len != 0);
  }

  fclose(f);
  return wrote;
}

bool recurse_mkdir(const char *dirname) {
#ifdef _WIN32
  const char SEP = '\\';
#else
  const char SEP = '/';
#endif

  const char *p;
  auto ret = true;

  std::string temp;
  /* Skip Windows drive letter. */
#ifdef _WIN32
    if ((p = strchr(dirname, ':') != nullptr) {
    p++;
    } else {
#endif
    p = dirname;
#ifdef _WIN32
    }
#endif

    while ((p = strchr(p, SEP)) != nullptr) {
    /* Skip empty elements. Could be a Windows UNC path or
       just multiple separators which is okay. */
    if (p != dirname && *(p - 1) == SEP) {
      p++;
      continue;
    }
    /* Put the path up to this point into a temporary to
       pass to the make directory function. */
    temp.assign(dirname, p - dirname);
    p++;
#ifdef _WIN32
    if (CreateDirectory(temp, nullptr) == FALSE) {
      if (GetLastError() != ERROR_ALREADY_EXISTS) {
        ret = false;
        break;
      }
    }
#else
    if (mkdir(temp.data(), 0774) != 0) {
      if (errno != EEXIST) {
        ret = false;
        break;
      }
    }
#endif
    }
    return ret;
}

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
  Zipper zipper;

  if (!zipper.open(zipname)) {
    printf("Could not open %s for zipping\n", zipname);
    return false;
  }

  printf("adding dir: %s\n", d2_name);
  if (!zipper.add_dir(d2_name)) {
    printf("failed to write dir %s\n", d2_name);
    return false;
  }

  printf("adding dir: %s\n", d3_name);
  if (!zipper.add_dir(d3_name)) {
    printf("failed to write dir %s\n", d3_name);
    return false;
  }

  printf("adding file: %s\n", f1_name);
  if (!zipper.add_buf(f1_name, (const unsigned char *)f1_data,
                      strlen(f1_data))) {
    printf("failed to write %s\n", f1_name);
    return false;
  }

  printf("adding file: %s\n", f2_name);
  if (!zipper.add_buf(f2_name, (const unsigned char *)f2_data,
                      strlen(f2_data))) {
    printf("failed to write %s\n", f2_name);
    return false;
  }

  printf("adding file: %s\n", f3_name);
  if (!zipper.add_buf(f3_name, (const unsigned char *)f3_data,
                      strlen(f3_data))) {
    printf("failed to write %s\n", f3_name);
    return false;
  }

  printf("adding dir: %s\n", d1_name);
  if (!zipper.add_dir(d1_name)) {
    printf("failed to write dir %s\n", d1_name);
    return false;
  }

  printf("adding file: %s\n", f4_name);
  if (!zipper.add_buf(f4_name, (const unsigned char *)f4_data,
                      strlen(f4_data))) {
    printf("failed to write %s\n", f4_name);
    return false;
  }

  return true;
}

static bool unzip_test_zip(void) {
  UnZipper unzipper;

  if (!unzipper.open(zipname)) {
    printf("Could not open %s for unzipping\n", zipname);
    return false;
  }

  zipper_result_t zipper_ret;

  do {
    zipper_ret = ZIPPER_RESULT_SUCCESS;
    auto zfilename = unzipper.filename();
    if (zfilename.empty()) return true;

    if (unzipper.isdir()) {
      printf("reading dir: %s\n", zfilename.data());
      recurse_mkdir(zfilename.data());
      unzipper.goto_next_file();
      continue;
    }

    auto len = unzipper.filesize();
    printf("reading file (%llu bytes): %s\n", len, zfilename.data());

    std::string buf;
    zipper_ret = unzipper.read_buf(buf);
    if (zipper_ret == ZIPPER_RESULT_ERROR) {
      break;
    }

    recurse_mkdir(zfilename.data());
    write_file(zfilename.data(), buf.data(), buf.size(), false);
  } while (zipper_ret == ZIPPER_RESULT_SUCCESS);

  if (zipper_ret == ZIPPER_RESULT_ERROR) {
    printf("failed to read file\n");
    return false;
  }

  return true;
}

int main(int argc, char **argv) {
  if (!create_test_zip()) return 1;
  if (!unzip_test_zip()) return 1;
  return 0;
}
