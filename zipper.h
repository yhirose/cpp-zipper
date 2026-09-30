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
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#define ZIPPER_BUF_SIZE 8192

namespace zipper {

using read_cb_t = std::function<void(const char *data, size_t len)>;

namespace detail {

// An archive held in memory, read or written through minizip's I/O hooks.
// A reader views bytes it does not own; a writer grows `out`, overwriting in
// place when minizip seeks back to finish a header.
struct MemoryStream {
  const char *data = nullptr;
  size_t size = 0;
  std::string *out = nullptr;
  size_t pos = 0;
};

inline voidpf ZCALLBACK mem_open(voidpf opaque, const void *, int) {
  return opaque;
}

inline uLong ZCALLBACK mem_read(voidpf, voidpf stream, void *buf, uLong size) {
  auto s = static_cast<MemoryStream *>(stream);
  auto avail = s->pos < s->size ? s->size - s->pos : 0;
  auto n = static_cast<size_t>(size) < avail ? static_cast<size_t>(size) : avail;
  std::memcpy(buf, s->data + s->pos, n);
  s->pos += n;
  return static_cast<uLong>(n);
}

inline uLong ZCALLBACK mem_write(voidpf, voidpf stream, const void *buf,
                                 uLong size) {
  auto s = static_cast<MemoryStream *>(stream);
  if (!s->out) { return 0; }
  auto end = s->pos + size;
  if (end > s->out->size()) { s->out->resize(end); }
  std::memcpy(&(*s->out)[s->pos], buf, size);
  s->pos = end;
  s->data = s->out->data();
  s->size = s->out->size();
  return size;
}

inline ZPOS64_T ZCALLBACK mem_tell(voidpf, voidpf stream) {
  return static_cast<MemoryStream *>(stream)->pos;
}

inline long ZCALLBACK mem_seek(voidpf, voidpf stream, ZPOS64_T offset,
                               int origin) {
  auto s = static_cast<MemoryStream *>(stream);
  ZPOS64_T base = 0;
  switch (origin) {
  case ZLIB_FILEFUNC_SEEK_SET: base = 0; break;
  case ZLIB_FILEFUNC_SEEK_CUR: base = s->pos; break;
  case ZLIB_FILEFUNC_SEEK_END: base = s->size; break;
  default: return -1;
  }
  if (base + offset > s->size) { return -1; }
  s->pos = static_cast<size_t>(base + offset);
  return 0;
}

inline int ZCALLBACK mem_close(voidpf, voidpf) { return 0; }
inline int ZCALLBACK mem_error(voidpf, voidpf) { return 0; }

inline zlib_filefunc64_def memory_functions(MemoryStream *s) {
  zlib_filefunc64_def f;
  f.zopen64_file = mem_open;
  f.zread_file = mem_read;
  f.zwrite_file = mem_write;
  f.ztell64_file = mem_tell;
  f.zseek64_file = mem_seek;
  f.zclose_file = mem_close;
  f.zerror_file = mem_error;
  f.opaque = s;
  return f;
}

inline const char *unzip_message(int code) {
  switch (code) {
  case UNZ_ERRNO: return "I/O error";
  case UNZ_PARAMERROR: return "invalid parameter";
  case UNZ_BADZIPFILE: return "not a ZIP archive, or a damaged one";
  case UNZ_INTERNALERROR: return "internal error";
  case UNZ_CRCERROR: return "the data does not match its CRC";
  default: return "unknown error";
  }
}

inline const char *zip_message(int code) {
  switch (code) {
  case ZIP_ERRNO: return "I/O error";
  case ZIP_PARAMERROR: return "invalid parameter";
  case ZIP_BADZIPFILE: return "not a ZIP archive, or a damaged one";
  case ZIP_INTERNALERROR: return "internal error";
  default: return "unknown error";
  }
}

} // namespace detail

// Writes an archive to a file, or to memory (`open_memory()`, then
// `buffer()` after `close()`). Every call that can fail returns false and
// leaves the reason in `error()`.
class Zip {
public:
  Zip() = default;
  Zip(const std::string &zipname) { open(zipname); }
  ~Zip() { close(); }

  Zip(const Zip &) = delete;
  Zip &operator=(const Zip &) = delete;

  bool open(const std::string &zipname) {
    close();
    zfile_ = zipOpen64(zipname.data(), APPEND_STATUS_CREATE);
    return opened("cannot create " + zipname);
  }

  bool open_memory() {
    close();
    buffer_.clear();
    stream_ = detail::MemoryStream{nullptr, 0, &buffer_, 0};
    auto funcs = detail::memory_functions(&stream_);
    zfile_ = zipOpen2_64("", APPEND_STATUS_CREATE, nullptr, &funcs);
    return opened("cannot create an archive in memory");
  }

  bool is_open() const { return zfile_ != nullptr; }

  bool close() {
    if (zfile_ == nullptr) { return true; }
    auto ret = zipClose(zfile_, nullptr);
    zfile_ = nullptr;
    return check(ret);
  }

  bool add_dir(std::string dirname) {
    assert(zfile_ && !dirname.empty());

    if (dirname.back() != '/') { dirname += '/'; }

    auto ret = zipOpenNewFileInZip64(zfile_, dirname.data(), nullptr, nullptr,
                                     0, nullptr, 0, nullptr, 0, 0, 0);
    if (!check(ret)) { return false; }
    return check(zipCloseFileInZip(zfile_));
  }

  bool add_file(const std::string &path, const char *data, size_t len) {
    assert(zfile_ && (data || len == 0));

    auto ret = zipOpenNewFileInZip64(
        zfile_, path.data(), nullptr, nullptr, 0, nullptr, 0, nullptr,
        Z_DEFLATED, Z_DEFAULT_COMPRESSION, (len > 0xffffffff) ? 1 : 0);
    if (!check(ret)) { return false; }

    // zipWriteInFileInZip takes an unsigned int, so a large file goes in
    // pieces.
    while (len > 0) {
      auto n = len > 0x40000000 ? 0x40000000 : len;
      ret = zipWriteInFileInZip(zfile_, data, static_cast<unsigned int>(n));
      if (!check(ret)) {
        zipCloseFileInZip(zfile_);
        return false;
      }
      data += n;
      len -= n;
    }
    return check(zipCloseFileInZip(zfile_));
  }

  bool add_file(const std::string &path, const std::string &data) {
    return add_file(path, data.data(), data.size());
  }

  // The archive `open_memory()` wrote, complete once `close()` returns.
  const std::string &buffer() const { return buffer_; }

  const std::string &error() const { return error_; }

  operator zipFile() { return zfile_; }

private:
  bool opened(const std::string &message) {
    error_ = zfile_ ? std::string() : message;
    return zfile_ != nullptr;
  }

  bool check(int ret) {
    if (ret == ZIP_OK) { return true; }
    error_ = detail::zip_message(ret);
    return false;
  }

  zipFile zfile_ = nullptr;
  detail::MemoryStream stream_;
  std::string buffer_;
  std::string error_;
};

// Reads an archive from a file, or from bytes in memory (`open_memory`,
// which does not copy them: they must outlive the UnZip). A cursor walks the
// entries (`next()`, or `locate()` by name); every call that can fail returns
// false and leaves the reason in `error()`.
class UnZip {
public:
  UnZip() = default;
  UnZip(const std::string &zipname) { open(zipname); }
  ~UnZip() { close(); }

  UnZip(const UnZip &) = delete;
  UnZip &operator=(const UnZip &) = delete;

  bool open(const std::string &zipname) {
    close();
    uzfile_ = unzOpen64(zipname.data());
    return opened("cannot open " + zipname + " as a ZIP archive");
  }

  bool open_memory(const char *data, size_t size) {
    close();
    stream_ = detail::MemoryStream{data, size, nullptr, 0};
    auto funcs = detail::memory_functions(&stream_);
    uzfile_ = unzOpen2_64("", &funcs);
    return opened("not a ZIP archive, or a damaged one");
  }

  bool is_open() const { return uzfile_ != nullptr; }

  void close() {
    if (uzfile_ != nullptr) {
      unzClose(uzfile_);
      uzfile_ = nullptr;
    }
  }

  // Reads the current entry, handing its bytes to `cb` as they come.
  bool read(read_cb_t cb) const {
    assert(uzfile_);

    unz_file_info64 finfo;
    if (!check(unzGetCurrentFileInfo64(uzfile_, &finfo, nullptr, 0, nullptr, 0,
                                       nullptr, 0))) {
      return false;
    }
    if (finfo.flag & 1) {
      error_ = "the entry is encrypted";
      return false;
    }
    if (finfo.compression_method != 0 && finfo.compression_method != Z_DEFLATED) {
      error_ = "the entry is compressed with method " +
               std::to_string(finfo.compression_method) +
               ", which only stored (0) and deflate (8) are read";
      return false;
    }
    if (!check(unzOpenCurrentFile(uzfile_))) { return false; }

    char buf[ZIPPER_BUF_SIZE];
    int red;
    while ((red = unzReadCurrentFile(uzfile_, buf, sizeof(buf))) > 0) {
      cb(buf, static_cast<size_t>(red));
    }
    auto closed = unzCloseCurrentFile(uzfile_);
    return check(red < 0 ? red : closed);
  }

  bool read(std::string &buf) const {
    return read([&](const char *data, size_t len) { buf.append(data, len); });
  }

  std::string file_path() const {
    assert(uzfile_);

    unz_file_info64 finfo;
    if (!check(unzGetCurrentFileInfo64(uzfile_, &finfo, nullptr, 0, nullptr, 0,
                                       nullptr, 0))) {
      return std::string();
    }
    std::string name(finfo.size_filename, '\0');
    if (!check(unzGetCurrentFileInfo64(uzfile_, &finfo, &name[0],
                                       name.size() + 1, nullptr, 0, nullptr,
                                       0))) {
      return std::string();
    }
    return name;
  }

  bool is_dir() const {
    auto name = file_path();
    return !name.empty() && name.back() == '/';
  }

  bool is_file() const { return !is_dir(); }

  bool first() const {
    assert(uzfile_);
    auto ret = unzGoToFirstFile(uzfile_);
    if (ret == UNZ_END_OF_LIST_OF_FILE) { return false; }
    return check(ret);
  }

  bool next() const {
    assert(uzfile_);
    auto ret = unzGoToNextFile(uzfile_);
    if (ret == UNZ_END_OF_LIST_OF_FILE) { return false; }
    return check(ret);
  }

  // Moves the cursor to the entry named `path` (as written, case and all).
  bool locate(const std::string &path) const {
    assert(uzfile_);
    auto ret = unzLocateFile(uzfile_, path.data(), 1);
    if (ret == UNZ_END_OF_LIST_OF_FILE) {
      error_ = "no entry " + path;
      return false;
    }
    return check(ret);
  }

  uint64_t file_size() const {
    assert(uzfile_);

    unz_file_info64 finfo;
    if (!check(unzGetCurrentFileInfo64(uzfile_, &finfo, nullptr, 0, nullptr, 0,
                                       nullptr, 0))) {
      return 0;
    }
    return finfo.uncompressed_size;
  }

  template <typename T> void enumerate(T callback) {
    if (first()) {
      do {
        callback(*this);
      } while (next());
    }
  }

  const std::string &error() const { return error_; }

  operator unzFile() { return uzfile_; }

private:
  bool opened(const std::string &message) {
    error_ = uzfile_ ? std::string() : message;
    return uzfile_ != nullptr;
  }

  bool check(int ret) const {
    if (ret == UNZ_OK) { return true; }
    error_ = detail::unzip_message(ret);
    return false;
  }

  unzFile uzfile_ = nullptr;
  detail::MemoryStream stream_;
  mutable std::string error_;
};

template <typename T>
inline bool enumerate(const std::string &zipname, T callback) {
  UnZip unzip;
  if (unzip.open(zipname)) {
    unzip.enumerate(callback);
    return true;
  }
  return false;
}

}; // namespace zipper
