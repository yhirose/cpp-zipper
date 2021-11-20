//
//  zipper.h
//
//  This code is based on 'Making MiniZip Easier to Use' by John Schember.
//  https://nachtimwald.com/2019/09/08/making-minizip-easier-to-use/
//
//  Copyright (c) 2021 Yuji Hirose. All rights reserved.
//  MIT License
//

#pragma once

#include <minizip/unzip.h>
#include <minizip/zip.h>

#include <cassert>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#define ZIPPER_BUF_SIZE 8192
#define ZIPPER_MAX_NAMELEN 256

namespace zipper {

using read_cb_t = std::function<void(const unsigned char *buf, size_t size)>;

class Zip {
 public:
  Zip() = default;
  Zip(const std::string &zipname) { open(zipname); }
  ~Zip() { close(); }

  bool open(const std::string &zipname) {
    zfile_ = zipOpen64(zipname.data(), 0);
    return is_open();
  }

  bool is_open() const { return zfile_ != nullptr; }

  void close() {
    if (zfile_ != nullptr) {
      zipClose(zfile_, nullptr);
      zfile_ = nullptr;
    }
  }

  bool add_file(const std::string &path) {
    assert(zfile_);

    std::ifstream f(path);
    if (!f) {
      return false;
    }

    f.seekg(0, f.end);
    auto flen = f.tellg();
    f.seekg(0, f.beg);

    auto ret = zipOpenNewFileInZip64(
        zfile_, path.data(), nullptr, nullptr, 0, nullptr, 0, nullptr,
        Z_DEFLATED, Z_DEFAULT_COMPRESSION, (flen > 0xffffffff) ? 1 : 0);

    if (ret != ZIP_OK) {
      return false;
    }

    std::vector<char> buf(flen);
    f.read(&buf[0], buf.size());
    ret = zipWriteInFileInZip(zfile_, buf.data(), buf.size());

    zipCloseFileInZip(zfile_);
    return ret == ZIP_OK;
  }

  bool add_buf(const std::string &zfilename, const unsigned char *buf,
               size_t buflen) {
    assert(zfile_ && buf && buflen > 0);

    auto ret = zipOpenNewFileInZip64(
        zfile_, zfilename.data(), nullptr, nullptr, 0, nullptr, 0, nullptr,
        Z_DEFLATED, Z_DEFAULT_COMPRESSION, (buflen > 0xffffffff) ? 1 : 0);

    if (ret != ZIP_OK) {
      return false;
    }

    ret = zipWriteInFileInZip(zfile_, buf, buflen);

    zipCloseFileInZip(zfile_);
    return ret == ZIP_OK ? true : false;
  }

  bool add_dir(std::string dirname) {
    assert(zfile_ && !dirname.empty());

    if (dirname.back() != '/') {
      dirname += '/';
    }

    auto ret = zipOpenNewFileInZip64(zfile_, dirname.data(), nullptr, nullptr,
                                     0, nullptr, 0, nullptr, 0, 0, 0);

    if (ret != ZIP_OK) {
      return false;
    }

    zipCloseFileInZip(zfile_);
    return ret == ZIP_OK ? true : false;
  }

  operator zipFile() { return zfile_; }

 private:
  zipFile zfile_ = nullptr;
};

class UnZip {
 public:
  UnZip() = default;
  UnZip(const std::string &zipname) { open(zipname); }
  ~UnZip() { close(); }

  bool open(const std::string &zipname) {
    uzfile_ = unzOpen64(zipname.data());
    return is_open();
  }

  bool is_open() const { return uzfile_ != nullptr; }

  void close() {
    if (uzfile_ != nullptr) {
      unzClose(uzfile_);
      uzfile_ = nullptr;
    }
  }

  bool read(read_cb_t cb) const {
    assert(uzfile_);

    auto ret = unzOpenCurrentFile(uzfile_);
    if (ret != UNZ_OK) {
      return false;
    }

    unsigned char tbuf[ZIPPER_BUF_SIZE];
    int red;

    while ((red = unzReadCurrentFile(uzfile_, tbuf, sizeof(tbuf))) > 0) {
      cb(tbuf, red);
    }

    unzCloseCurrentFile(uzfile_);
    return red >= 0;
  }

  bool read_buf(std::string &buf) const {
    return read([&](const unsigned char *data, size_t len) {
      buf.append(reinterpret_cast<const char *>(data), len);
    });
  }

  std::string file_path() const {
    assert(uzfile_);

    char name[ZIPPER_MAX_NAMELEN];
    unz_file_info64 finfo;

    auto ret = unzGetCurrentFileInfo64(uzfile_, &finfo, name, sizeof(name),
                                       nullptr, 0, nullptr, 0);
    if (ret != UNZ_OK) {
      return nullptr;
    }

    return name;
  }

  bool isdir() const {
    assert(uzfile_);

    char name[ZIPPER_MAX_NAMELEN];
    unz_file_info64 finfo;

    auto ret = unzGetCurrentFileInfo64(uzfile_, &finfo, name, sizeof(name),
                                       nullptr, 0, nullptr, 0);
    if (ret != UNZ_OK) {
      return false;
    }

    auto len = strlen(name);
    return finfo.uncompressed_size == 0 && len > 0 && name[len - 1] == '/';
  }

  bool skip_file() const {
    assert(uzfile_);
    return unzGoToNextFile(uzfile_) == UNZ_OK;
  }

  uint64_t file_size() const {
    assert(uzfile_);

    unz_file_info64 finfo;
    auto ret = unzGetCurrentFileInfo64(uzfile_, &finfo, nullptr, 0, nullptr, 0,
                                       nullptr, 0);
    if (ret != UNZ_OK) {
      return 0;
    }

    return finfo.uncompressed_size;
  }

  operator unzFile() { return uzfile_; }

 private:
  unzFile uzfile_ = nullptr;
};

template <typename T>
inline bool enumerate(const std::string &zipname, T callback) {
  UnZip unzip;
  if (!unzip.open(zipname)) {
    return false;
  }
  if (!unzip.file_path().empty()) {
    do {
      callback(unzip);
    } while (unzip.skip_file());
  }
  return true;
}

};  // namespace zipper
