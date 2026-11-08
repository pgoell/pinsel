#include "tiles/zip.hpp"

#define ZLIB_CONST
#include <zlib.h>

#include <QIODevice>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <utility>

namespace pinsel {

namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr qsizetype kLocalHeaderSize = 30;
constexpr qsizetype kCentralHeaderSize = 46;
constexpr qsizetype kEndSize = 22;
constexpr qsizetype kMaxComment = 0xffff;
constexpr std::uint16_t kVersion = 20;
constexpr std::uint16_t kStored = 0;
constexpr std::uint16_t kDeflated = 8;
constexpr std::uint16_t kEncrypted = 0x0001;
constexpr std::uint16_t kUtf8Names = 0x0800;
// 1 January 1980 in MS-DOS date format, the earliest date a zip can hold. A
// fixed date makes the same image give the same file.
constexpr std::uint16_t kDate = (1U << 5U) | 1U;
// Deflate never shrinks data by more than this, so a larger claimed size is a
// lie. https://zlib.net/zlib_tech.html
constexpr std::uint64_t kMaxDeflateRatio = 1032;
constexpr std::uint64_t kMax32 = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint64_t kMax16 = std::numeric_limits<std::uint16_t>::max();

const Bytef* bytes(QByteArrayView data) {
  return reinterpret_cast<const Bytef*>(data.data());
}

std::uint32_t crcOf(QByteArrayView data) {
  return static_cast<std::uint32_t>(crc32_z(0, bytes(data), static_cast<z_size_t>(data.size())));
}

void put16(QByteArray& out, std::uint16_t value) {
  out.append(static_cast<char>(value & 0xffU));
  out.append(static_cast<char>(value >> 8U));
}

void put32(QByteArray& out, std::uint32_t value) {
  put16(out, static_cast<std::uint16_t>(value & 0xffffU));
  put16(out, static_cast<std::uint16_t>(value >> 16U));
}

std::uint16_t get16(QByteArrayView data, qsizetype at) {
  return qFromLittleEndian<std::uint16_t>(data.sliced(at, 2).data());
}

std::uint32_t get32(QByteArrayView data, qsizetype at) {
  return qFromLittleEndian<std::uint32_t>(data.sliced(at, 4).data());
}

// Raw deflate, with no zlib header, as zip stores it.
constexpr int kRawWindowBits = -MAX_WBITS;

std::optional<QByteArray> deflateRaw(QByteArrayView data) {
  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, kRawWindowBits, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    return std::nullopt;
  }
  const uLong bound = deflateBound(&stream, static_cast<uLong>(data.size()));
  QByteArray out(static_cast<qsizetype>(bound), Qt::Uninitialized);
  stream.next_in = bytes(data);
  stream.avail_in = static_cast<uInt>(data.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  const int result = deflate(&stream, Z_FINISH);
  const auto packed = static_cast<qsizetype>(stream.total_out);
  deflateEnd(&stream);
  if (result != Z_STREAM_END) {
    return std::nullopt;
  }
  out.truncate(packed);
  return out;
}

std::optional<QByteArray> inflateRaw(QByteArrayView packed, std::uint32_t size) {
  QByteArray out(static_cast<qsizetype>(size), Qt::Uninitialized);
  z_stream stream{};
  if (inflateInit2(&stream, kRawWindowBits) != Z_OK) {
    return std::nullopt;
  }
  stream.next_in = bytes(packed);
  stream.avail_in = static_cast<uInt>(packed.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = size;
  const int result = inflate(&stream, Z_FINISH);
  const uInt left = stream.avail_out;
  inflateEnd(&stream);
  if (result != Z_STREAM_END || left != 0) {
    return std::nullopt;
  }
  return out;
}

}  // namespace

bool ZipWriter::write(const QByteArray& bytes) {
  written_ += static_cast<std::uint64_t>(bytes.size());
  return device_->write(bytes) == bytes.size();
}

bool ZipWriter::add(const QString& name, const QByteArray& data, bool deflate) {
  if (std::cmp_greater(data.size(), kMax32) || entries_.size() >= kMax16) {
    return false;
  }
  const std::optional<QByteArray> packed = deflate ? deflateRaw(data) : data;
  if (!packed) {
    return false;
  }
  const Entry entry{
      .name = name.toUtf8(),
      .crc = crcOf(data),
      .packed_size = static_cast<std::uint32_t>(packed->size()),
      .size = static_cast<std::uint32_t>(data.size()),
      .method = deflate ? kDeflated : kStored,
      .offset = static_cast<std::uint32_t>(written_),
  };
  const auto end = written_ + std::uint64_t{kLocalHeaderSize} +
                   static_cast<std::uint64_t>(entry.name.size()) + entry.packed_size;
  if (written_ > kMax32 || end > kMax32 || std::cmp_greater(entry.name.size(), kMax16)) {
    return false;
  }
  QByteArray header;
  put32(header, kLocalSignature);
  put16(header, kVersion);
  put16(header, kUtf8Names);
  put16(header, entry.method);
  put16(header, 0);
  put16(header, kDate);
  put32(header, entry.crc);
  put32(header, entry.packed_size);
  put32(header, entry.size);
  put16(header, static_cast<std::uint16_t>(entry.name.size()));
  put16(header, 0);
  header.append(entry.name);
  entries_.push_back(entry);
  return write(header) && write(*packed);
}

bool ZipWriter::finish() {
  const std::uint64_t start = written_;
  QByteArray directory;
  for (const Entry& entry : entries_) {
    put32(directory, kCentralSignature);
    put16(directory, kVersion);
    put16(directory, kVersion);
    put16(directory, kUtf8Names);
    put16(directory, entry.method);
    put16(directory, 0);
    put16(directory, kDate);
    put32(directory, entry.crc);
    put32(directory, entry.packed_size);
    put32(directory, entry.size);
    put16(directory, static_cast<std::uint16_t>(entry.name.size()));
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, 0);
    put16(directory, 0);
    put32(directory, 0);
    put32(directory, entry.offset);
    directory.append(entry.name);
  }
  const auto directory_size = static_cast<std::uint64_t>(directory.size());
  if (start + directory_size > kMax32) {
    return false;
  }
  const auto count = static_cast<std::uint16_t>(entries_.size());
  put32(directory, kEndSignature);
  put16(directory, 0);
  put16(directory, 0);
  put16(directory, count);
  put16(directory, count);
  put32(directory, static_cast<std::uint32_t>(directory_size));
  put32(directory, static_cast<std::uint32_t>(start));
  put16(directory, 0);
  return write(directory);
}

ZipReader::ZipReader(const QString& path) : file_(path) {
  if (!file_.open(QIODevice::ReadOnly)) {
    error_ = file_.errorString();
    return;
  }
  const uchar* mapped = file_.map(0, file_.size());
  if (mapped == nullptr) {
    error_ = file_.size() == 0 ? QStringLiteral("The file is empty") : file_.errorString();
    return;
  }
  data_ = QByteArrayView(mapped, file_.size());
  readDirectory();
  if (!error_.isEmpty()) {
    entries_.clear();
  }
}

void ZipReader::readDirectory() {
  const QString broken = QStringLiteral("Not a zip archive, or a broken one");
  qsizetype end = data_.size() - kEndSize;
  const qsizetype earliest = std::max<qsizetype>(0, end - kMaxComment);
  while (end >= earliest && get32(data_, end) != kEndSignature) {
    --end;
  }
  if (end < earliest) {
    error_ = broken;
    return;
  }
  const std::uint16_t count = get16(data_, end + 10);
  const std::uint32_t directory_size = get32(data_, end + 12);
  const std::uint32_t directory_start = get32(data_, end + 16);
  if (count == kMax16 || directory_size == kMax32 || directory_start == kMax32) {
    error_ = QStringLiteral("Zip64 archives are not supported");
    return;
  }
  if (std::uint64_t{directory_start} + directory_size > static_cast<std::uint64_t>(end)) {
    error_ = broken;
    return;
  }
  qsizetype at = directory_start;
  for (std::uint16_t index = 0; index < count; ++index) {
    if (at + kCentralHeaderSize > end || get32(data_, at) != kCentralSignature) {
      error_ = broken;
      return;
    }
    const std::uint16_t name_size = get16(data_, at + 28);
    const qsizetype next =
        at + kCentralHeaderSize + name_size + get16(data_, at + 30) + get16(data_, at + 32);
    if (next > end) {
      error_ = broken;
      return;
    }
    // Names without the UTF-8 flag are in code page 437, which matches UTF-8
    // for the ASCII names OpenRaster uses.
    entries_.push_back({
        .name = QString::fromUtf8(data_.sliced(at + kCentralHeaderSize, name_size)),
        .flags = get16(data_, at + 8),
        .method = get16(data_, at + 10),
        .crc = get32(data_, at + 16),
        .packed_size = get32(data_, at + 20),
        .size = get32(data_, at + 24),
        .offset = get32(data_, at + 42),
    });
    at = next;
  }
}

QString ZipReader::firstName() const {
  const auto first = std::ranges::min_element(entries_, {}, &Entry::offset);
  return first == entries_.end() ? QString() : first->name;
}

std::optional<QByteArray> ZipReader::read(const QString& name, std::uint32_t max_size) const {
  const auto found = std::ranges::find(entries_, name, &Entry::name);
  if (found == entries_.end() || (found->flags & kEncrypted) != 0 || found->size > max_size ||
      std::uint64_t{found->size} > (std::uint64_t{found->packed_size} * kMaxDeflateRatio) + 1024) {
    return std::nullopt;
  }
  const qsizetype header = found->offset;
  if (header + kLocalHeaderSize > data_.size() || get32(data_, header) != kLocalSignature) {
    return std::nullopt;
  }
  const qsizetype start =
      header + kLocalHeaderSize + get16(data_, header + 26) + get16(data_, header + 28);
  if (start + qsizetype{found->packed_size} > data_.size()) {
    return std::nullopt;
  }
  const QByteArrayView packed = data_.sliced(start, found->packed_size);
  std::optional<QByteArray> data;
  if (found->method == kStored && found->packed_size == found->size) {
    data = packed.toByteArray();
  } else if (found->method == kDeflated) {
    data = inflateRaw(packed, found->size);
  }
  if (!data || crcOf(*data) != found->crc) {
    return std::nullopt;
  }
  return data;
}

}  // namespace pinsel
