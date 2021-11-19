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

#include <functional>
#include <string>

#define ZIPPER_BUF_SIZE 8192
#define ZIPPER_MAX_NAMELEN 256

namespace zipper {

using read_cb_t = std::function<void(const unsigned char *buf, size_t size)>;

class Zip {
 public:
  Zip() = default;
  Zip(const char *zipname) { open(zipname); }
  ~Zip() { close(); }

  bool open(const char *zipname) {
    zfile_ = zipOpen64(zipname, 0);
    return is_open();
  }

  bool is_open() const { return zfile_ != nullptr; }

  void close() {
    if (zfile_ != nullptr) {
      zipClose(zfile_, nullptr);
      zfile_ = nullptr;
    }
  }

  bool add_file(const char *filename) {
    if (zfile_ == nullptr || filename == nullptr) return false;

    auto f = fopen(filename, "r");
    if (f == nullptr) return false;

    fseek(f, 0, SEEK_END);
    auto flen = ftell(f);
    rewind(f);

    auto ret = zipOpenNewFileInZip64(
        zfile_, filename, nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
        Z_DEFAULT_COMPRESSION, (flen > 0xffffffff) ? 1 : 0);
    if (ret != ZIP_OK) {
      fclose(f);
      return false;
    }

    unsigned char buf[ZIPPER_BUF_SIZE];
    size_t red;

    while ((red = fread(buf, sizeof(*buf), sizeof(buf), f)) > 0) {
      ret = zipWriteInFileInZip(zfile_, buf, red);
      if (ret != ZIP_OK) {
        fclose(f);
        zipCloseFileInZip(zfile_);
        return false;
      }
    }

    zipCloseFileInZip(zfile_);
    return true;
  }

  bool add_buf(const char *zfilename, const unsigned char *buf, size_t buflen) {
    if (zfile_ == nullptr || buf == nullptr || buflen == 0) return false;

    auto ret = zipOpenNewFileInZip64(
        zfile_, zfilename, nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
        Z_DEFAULT_COMPRESSION, (buflen > 0xffffffff) ? 1 : 0);
    if (ret != ZIP_OK) return false;

    ret = zipWriteInFileInZip(zfile_, buf, buflen);
    zipCloseFileInZip(zfile_);
    return ret == ZIP_OK ? true : false;
  }

  bool add_dir(const char *dirname) {
    if (zfile_ == nullptr || dirname == nullptr || *dirname == '\0')
      return false;

    auto len = strlen(dirname);
    auto temp = static_cast<char *>(calloc(1, len + 2));
    memcpy(temp, dirname, len + 2);
    if (temp[len - 1] != '/') {
      temp[len] = '/';
      temp[len + 1] = '\0';
    } else {
      temp[len] = '\0';
    }

    auto ret = zipOpenNewFileInZip64(zfile_, temp, nullptr, nullptr, 0, nullptr,
                                     0, nullptr, 0, 0, 0);
    if (ret != ZIP_OK) return false;
    free(temp);
    zipCloseFileInZip(zfile_);
    return ret == ZIP_OK ? true : false;
  }

 private:
  zipFile zfile_ = nullptr;
};

class UnZip {
 public:
  UnZip() = default;
  UnZip(const char *zipname) { open(zipname); }
  ~UnZip() { close(); }

  bool open(const char *zipname) {
    uzfile_ = unzOpen64(zipname);
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
    if (uzfile_ == nullptr) return false;

    auto ret = unzOpenCurrentFile(uzfile_);
    if (ret != UNZ_OK) return false;

    unsigned char tbuf[ZIPPER_BUF_SIZE];
    int red;

    while ((red = unzReadCurrentFile(uzfile_, tbuf, sizeof(tbuf))) > 0) {
      cb(tbuf, red);
    }

    if (red < 0) {
      unzCloseCurrentFile(uzfile_);
      return false;
    }

    unzCloseCurrentFile(uzfile_);
    return true;
  }

  bool read_buf(std::string &buf) const {
    return read([&](const unsigned char *data, size_t len) {
      buf.append(reinterpret_cast<const char *>(data), len);
    });
  }

  std::string filename() const {
    if (uzfile_ == nullptr) return nullptr;

    char name[ZIPPER_MAX_NAMELEN];
    unz_file_info64 finfo;

    auto ret = unzGetCurrentFileInfo64(uzfile_, &finfo, name, sizeof(name),
                                       nullptr, 0, nullptr, 0);
    if (ret != UNZ_OK) return nullptr;

    return name;
  }

  bool isdir() const {
    if (uzfile_ == nullptr) return false;

    char name[ZIPPER_MAX_NAMELEN];
    unz_file_info64 finfo;

    auto ret = unzGetCurrentFileInfo64(uzfile_, &finfo, name, sizeof(name),
                                       nullptr, 0, nullptr, 0);
    if (ret != UNZ_OK) return false;

    auto len = strlen(name);
    if (finfo.uncompressed_size == 0 && len > 0 && name[len - 1] == '/')
      return true;
    return false;
  }

  bool skip_file() const {
    if (unzGoToNextFile(uzfile_) != UNZ_OK) return false;
    return true;
  }

  uint64_t filesize() const {
    if (uzfile_ == nullptr) return 0;

    unz_file_info64 finfo;
    auto ret = unzGetCurrentFileInfo64(uzfile_, &finfo, nullptr, 0, nullptr, 0,
                                       nullptr, 0);
    if (ret != UNZ_OK) return 0;

    return finfo.uncompressed_size;
  }

  operator unzFile() const { return uzfile_; }

 private:
  unzFile uzfile_ = nullptr;
};

};  // namespace zipper
