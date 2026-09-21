#pragma once
#include <HalStorage.h>

#include <iostream>
#include <limits>

namespace serialization {
template <typename T>
inline void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
inline void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline bool tryWritePod(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
inline void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
inline void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline bool tryReadPod(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline bool tryWriteString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  return tryWritePod(file, len) && (len == 0 || file.write(reinterpret_cast<const uint8_t*>(s.data()), len) == len);
}

inline void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

inline void readString(FsFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  // Guard against corrupt/truncated cache files: never resize to a length
  // larger than the bytes that actually remain in the file. With exceptions
  // disabled (-fno-exceptions) a bogus huge length would call
  // std::__throw_length_error() and abort; capping keeps it crash-free and a
  // truncated read is handled by the caller's cache-version/integrity checks.
  const size_t filePos = file.position();
  const size_t fileSize = file.size();
  const size_t remaining = filePos <= fileSize ? fileSize - filePos : 0;
  if (len > remaining) {
    len = static_cast<uint32_t>(remaining);
  }
  s.resize(len);
  file.read(&s[0], len);
}

inline bool tryReadString(FsFile& file, std::string& s) {
  uint32_t len = 0;
  if (!tryReadPod(file, len)) {
    return false;
  }
  if (static_cast<size_t>(len) > s.max_size() || len > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  s.resize(len);
  const int readLen = static_cast<int>(len);
  return len == 0 || file.read(&s[0], readLen) == readLen;
}
}  // namespace serialization
