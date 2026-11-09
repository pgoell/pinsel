#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QFile>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

class QIODevice;

namespace pinsel {

// The parts of the zip format that OpenRaster needs: stored and deflated
// entries, no zip64, no encryption, one disk.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT

// Writes the entries in the order added, then the central directory.
class ZipWriter {
 public:
  explicit ZipWriter(QIODevice& device) : device_(&device) {}

  // Deflate pays only for data that is not compressed already. Returns false
  // when the device fails or the archive would need zip64.
  bool add(const QString& name, const QByteArray& data, bool deflate);
  bool finish();

 private:
  struct Entry {
    QByteArray name;
    std::uint32_t crc;
    std::uint32_t packed_size;
    std::uint32_t size;
    std::uint16_t method;
    std::uint32_t offset;
  };

  bool write(const QByteArray& bytes);

  QIODevice* device_;
  std::uint64_t written_ = 0;
  std::vector<Entry> entries_;
};

// Maps the file rather than reading it, so a large archive costs no memory
// until an entry is read.
class ZipReader {
 public:
  explicit ZipReader(const QString& path);

  // Why the file is not a zip archive this reader can read, or empty.
  [[nodiscard]] const QString& error() const {
    return error_;
  }
  // The name of the entry the archive stores first.
  [[nodiscard]] QString firstName() const;
  // Null when there is no such entry, it is broken, or it would inflate past
  // max_size bytes. The size an entry claims comes from the file, so the cap
  // keeps a crafted archive from taking all memory.
  [[nodiscard]] std::optional<QByteArray> read(const QString& name, std::uint32_t max_size) const;

 private:
  struct Entry {
    QString name;
    std::uint16_t flags;
    std::uint16_t method;
    std::uint32_t crc;
    std::uint32_t packed_size;
    std::uint32_t size;
    std::uint32_t offset;
  };

  void readDirectory();

  QFile file_;
  QByteArrayView data_;
  std::vector<Entry> entries_;
  QString error_;
};

}  // namespace pinsel
