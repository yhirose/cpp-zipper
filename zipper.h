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

typedef enum {
  ZIPPER_RESULT_ERROR = 0,
  ZIPPER_RESULT_SUCCESS,
  ZIPPER_RESULT_SUCCESS_EOF
} zipper_result_t;

inline bool zipper_add_file(zipFile zfile, const char *filename) {
  if (zfile == nullptr || filename == nullptr) return false;

  auto f = fopen(filename, "r");
  if (f == nullptr) return false;

  fseek(f, 0, SEEK_END);
  auto flen = ftell(f);
  rewind(f);

  auto ret = zipOpenNewFileInZip64(
      zfile, filename, nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
      Z_DEFAULT_COMPRESSION, (flen > 0xffffffff) ? 1 : 0);
  if (ret != ZIP_OK) {
    fclose(f);
    return false;
  }

  unsigned char buf[ZIPPER_BUF_SIZE];
  size_t red;

  while ((red = fread(buf, sizeof(*buf), sizeof(buf), f)) > 0) {
    ret = zipWriteInFileInZip(zfile, buf, red);
    if (ret != ZIP_OK) {
      fclose(f);
      zipCloseFileInZip(zfile);
      return false;
    }
  }

  zipCloseFileInZip(zfile);
  return true;
}

inline bool zipper_add_buf(zipFile zfile, const char *zfilename,
                           const unsigned char *buf, size_t buflen) {
  int ret;

  if (zfile == nullptr || buf == nullptr || buflen == 0) return false;

  ret = zipOpenNewFileInZip64(zfile, zfilename, nullptr, nullptr, 0, nullptr, 0,
                              nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION,
                              (buflen > 0xffffffff) ? 1 : 0);
  if (ret != ZIP_OK) return false;

  ret = zipWriteInFileInZip(zfile, buf, buflen);
  zipCloseFileInZip(zfile);
  return ret == ZIP_OK ? true : false;
}

inline bool zipper_add_dir(zipFile zfile, const char *dirname) {
  if (zfile == nullptr || dirname == nullptr || *dirname == '\0') return false;

  size_t len = strlen(dirname);
  auto temp = static_cast<char *>(calloc(1, len + 2));
  memcpy(temp, dirname, len + 2);
  if (temp[len - 1] != '/') {
    temp[len] = '/';
    temp[len + 1] = '\0';
  } else {
    temp[len] = '\0';
  }

  int ret = zipOpenNewFileInZip64(zfile, temp, nullptr, nullptr, 0, nullptr, 0,
                                  nullptr, 0, 0, 0);
  if (ret != ZIP_OK) return false;
  free(temp);
  zipCloseFileInZip(zfile);
  return ret == ZIP_OK ? true : false;
}

using zipper_read_cb_t =
    std::function<void(const unsigned char *buf, size_t size)>;

inline zipper_result_t zipper_read(unzFile zfile, zipper_read_cb_t cb) {
  if (zfile == nullptr) return ZIPPER_RESULT_ERROR;

  auto ret = unzOpenCurrentFile(zfile);
  if (ret != UNZ_OK) return ZIPPER_RESULT_ERROR;

  unsigned char tbuf[ZIPPER_BUF_SIZE];
  int red;

  while ((red = unzReadCurrentFile(zfile, tbuf, sizeof(tbuf))) > 0) {
    cb(tbuf, red);
  }

  if (red < 0) {
    unzCloseCurrentFile(zfile);
    return ZIPPER_RESULT_ERROR;
  }

  unzCloseCurrentFile(zfile);
  if (unzGoToNextFile(zfile) != UNZ_OK) return ZIPPER_RESULT_SUCCESS_EOF;
  return ZIPPER_RESULT_SUCCESS;
}

inline zipper_result_t zipper_read_buf(unzFile zfile, std::string &buf) {
  auto ret = zipper_read(zfile, [&](const unsigned char *data, size_t len) {
    buf.append(reinterpret_cast<const char *>(data), len);
  });

  // Really necessary?
  if (ret == ZIPPER_RESULT_ERROR) {
    buf.clear();
  }

  return ret;
}

inline std::string zipper_filename(unzFile zfile) {
  if (zfile == nullptr) return nullptr;

  char name[ZIPPER_MAX_NAMELEN];
  unz_file_info64 finfo;

  auto ret = unzGetCurrentFileInfo64(zfile, &finfo, name, sizeof(name), nullptr,
                                     0, nullptr, 0);
  if (ret != UNZ_OK) return nullptr;

  return name;
}

inline bool zipper_isdir(unzFile zfile) {
  if (zfile == nullptr) return false;

  char name[ZIPPER_MAX_NAMELEN];
  unz_file_info64 finfo;

  auto ret = unzGetCurrentFileInfo64(zfile, &finfo, name, sizeof(name), nullptr,
                                     0, nullptr, 0);
  if (ret != UNZ_OK) return false;

  auto len = strlen(name);
  if (finfo.uncompressed_size == 0 && len > 0 && name[len - 1] == '/')
    return true;
  return false;
}

inline bool zipper_skip_file(unzFile zfile) {
  if (unzGoToNextFile(zfile) != UNZ_OK) return false;
  return true;
}

inline uint64_t zipper_filesize(unzFile zfile) {
  if (zfile == nullptr) return 0;

  unz_file_info64 finfo;
  auto ret = unzGetCurrentFileInfo64(zfile, &finfo, nullptr, 0, nullptr, 0,
                                     nullptr, 0);
  if (ret != UNZ_OK) return 0;

  return finfo.uncompressed_size;
}

class Zipper {
 public:
  Zipper() = default;
  Zipper(const char *zipname) { open(zipname); }
  ~Zipper() { close(); }

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
    return zipper_add_file(zfile_, filename);
  }

  bool add_buf(const char *zfilename, const unsigned char *buf, size_t buflen) {
    return zipper_add_buf(zfile_, zfilename, buf, buflen);
  }

  bool add_dir(const char *dirname) { return zipper_add_dir(zfile_, dirname); }

 private:
  zipFile zfile_ = nullptr;
};

class UnZipper {
 public:
  UnZipper() = default;
  UnZipper(const char *zipname) { open(zipname); }
  ~UnZipper() { close(); }

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

  zipper_result_t read(zipper_read_cb_t cb) const {
    return zipper_read(uzfile_, cb);
  }

  zipper_result_t read_buf(std::string &buf) const {
    return zipper_read_buf(uzfile_, buf);
  }

  std::string filename() const { return zipper_filename(uzfile_); }

  bool isdir() const { return zipper_isdir(uzfile_); }

  bool skip_file() const { return zipper_skip_file(uzfile_); }

  uint64_t filesize() const { return zipper_filesize(uzfile_); }

  void goto_next_file() { unzGoToNextFile(uzfile_); }

  operator unzFile() const { return uzfile_; }

 private:
  unzFile uzfile_ = nullptr;
};
