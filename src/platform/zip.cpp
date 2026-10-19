/* Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
 * SPDX-License-Identifier: BSD-3-Clause */

/**
 * @file
 * @brief Stream bounded ZIP records and publish complete archives atomically.
 */
#include "zip.hpp"

#include "files.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>

namespace clutch {
namespace {
// PKWARE classic ZIP fields; ZIP64 sentinel values are deliberately excluded.
constexpr auto local_signature = UINT32_C(0x04034b50);
constexpr auto central_signature = UINT32_C(0x02014b50);
constexpr auto descriptor_signature = UINT32_C(0x08074b50);
constexpr auto end_signature = UINT32_C(0x06054b50);
constexpr auto zip_version = UINT16_C(20);
constexpr auto unix_version = UINT16_C(0x0314);
constexpr auto utf8_descriptor_flags = UINT16_C(0x0808);
constexpr auto dos_epoch_date = UINT16_C(0x0021);      // January 1, 1980.
constexpr auto crc_polynomial = UINT32_C(0xedb88320);  // Reflected IEEE CRC-32.
constexpr auto crc_initial = UINT32_C(0xffffffff);
constexpr auto maximum_size = UINT32_C(0xfffffffe);
constexpr auto maximum_members = UINT16_C(0xfffe);
constexpr auto unix_file = UINT32_C(0100644);
constexpr auto unix_executable = UINT32_C(0100755);
constexpr auto unix_directory = UINT32_C(0040755);
constexpr auto attribute_shift = 16U;
constexpr auto byte_bits = 8U;
constexpr auto byte_mask = UINT32_C(0xff);
constexpr auto buffer_bytes = size_t{64} * 1024U;
constexpr auto maximum_stage_attempts = 100U;

/// Keep a failed write private and remove only the directory we created.
class Stage {
  public:
  explicit Stage(const fs::path &parent) {
    const auto token =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < maximum_stage_attempts; ++attempt) {
      path = parent / (".clutch-zip-" + std::to_string(token) + "-" +
                       std::to_string(attempt));
      if (fs::create_directory(path))
        return;
    }

    throw std::runtime_error("cannot create ZIP staging directory");
  }

  ~Stage() {
    try {
      fs::remove_all(path);
    } catch (...) {
      // Preserve a write/publication error. The private stage may be removed
      // manually; a partial archive is never published as the requested output.
      std::fputs("warning: ZIP staging cleanup failed\n", stderr);
    }
  }

  Stage(const Stage &) = delete;
  Stage &operator=(const Stage &) = delete;
  fs::path path;
};

/// Encode unsigned fields explicitly rather than serializing native structs.
void little(std::ostream &stream,
            const std::uint32_t value,
            const unsigned bytes) {
  for (unsigned index = 0; index < bytes; ++index)
    stream.put(static_cast<char>((value >> (index * byte_bits)) & byte_mask));
}

/// Table-driven CRC keeps large uncompressed source snapshots inexpensive.
std::uint32_t
checksum(std::uint32_t crc, const char *const data, const size_t size) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> values{};
    for (size_t index = 0; index < values.size(); ++index) {
      auto value = static_cast<std::uint32_t>(index);
      for (unsigned bit = 0; bit < byte_bits; ++bit)
        value = (value >> 1U) ^ ((value & 1U) ? crc_polynomial : 0U);
      values[index] = value;
    }
    return values;
  }();
  for (size_t index = 0; index < size; ++index)
    crc = table[(crc ^ static_cast<unsigned char>(data[index])) & byte_mask] ^
          (crc >> byte_bits);
  return crc;
}

/// Bound every offset before narrowing to classic ZIP's 32-bit representation.
std::uint32_t position(std::ostream &stream) {
  const auto offset = stream.tellp();
  if (!stream || offset < 0 ||
      static_cast<std::uint64_t>(offset) > maximum_size)
    throw std::runtime_error(
        "ZIP write failed or exceeds the classic ZIP four-GiB limit");
  return static_cast<std::uint32_t>(offset);
}

/// Refuse invalid UTF-8 instead of emitting a falsely labelled filename.
bool utf8(const std::string &text) {
  for (size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index++]);
    if (first < 0x80U)
      continue;

    const unsigned count = first >= 0xc2U && first <= 0xdfU   ? 1U
                           : first >= 0xe0U && first <= 0xefU ? 2U
                           : first >= 0xf0U && first <= 0xf4U ? 3U
                                                              : 0U;
    if (!count || count > text.size() - index)
      return false;
    const auto second = static_cast<unsigned char>(text[index]);
    if ((first == 0xe0U && second < 0xa0U) ||
        (first == 0xedU && second >= 0xa0U) ||
        (first == 0xf0U && second < 0x90U) ||
        (first == 0xf4U && second >= 0x90U))
      return false;
    for (unsigned continuation = 0; continuation < count; ++continuation)
      if ((static_cast<unsigned char>(text[index++]) & 0xc0U) != 0x80U)
        return false;
  }
  return true;
}

/// Reject names that extract differently across POSIX and Windows hosts.
std::string portable_key(const std::string &name) {
  if (name.empty() || name.size() > maximum_members || name.front() == '/')
    throw std::runtime_error("invalid ZIP member name");
  if (!utf8(name))
    throw std::runtime_error("ZIP member name must be valid UTF-8");
  std::string key;
  size_t begin = 0;
  while (begin < name.size()) {
    const auto end = name.find('/', begin);
    const auto part = name.substr(begin, end == name.npos ? end : end - begin);
    if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
        part.back() == ' ')
      throw std::runtime_error("nonportable ZIP member name: " + name);
    const auto start = key.size();
    for (const unsigned char character : part) {
      if (character < ' ' || character == 127U ||
          std::string("\\:*?\"<>|").find(static_cast<char>(character)) !=
              std::string::npos)
        throw std::runtime_error("nonportable ZIP member name: " + name);
      key += static_cast<char>(character >= 'A' && character <= 'Z'
                                   ? character + ('a' - 'A')
                                   : character);
    }
    const auto base = key.substr(start,
                                 key.find('.', start) == key.npos
                                     ? key.npos
                                     : key.find('.', start) - start);
    if (base == "con" || base == "prn" || base == "aux" || base == "nul" ||
        (base.size() == 4 &&
         (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) &&
         base.back() >= '1' && base.back() <= '9'))
      throw std::runtime_error("reserved Windows ZIP member name: " + name);
    if (end == name.npos)
      break;
    key += '/';
    begin = end + 1;
  }
  if (key.back() == '/')
    key.pop_back();
  return key;
}

struct Record {
  const ZipEntry *entry;
  std::uint32_t offset, crc = 0, size = 0, attributes = 0;
};
}  // namespace

void write_zip(const fs::path &requested, std::vector<ZipEntry> entries) {
  const auto output = fs::absolute(requested).lexically_normal();
  validate_generated_path(output);
  if (fs::exists(output))
    throw std::runtime_error("bundle output already exists: " +
                             output.u8string());
  if (entries.empty() || entries.size() > maximum_members)
    throw std::runtime_error("ZIP requires between one and 65,534 members");

  std::sort(entries.begin(),
            entries.end(),
            [](const ZipEntry &left, const ZipEntry &right) {
              return left.name < right.name;
            });
  std::map<std::string, bool> names;
  for (const auto &entry : entries)
    if (!names.emplace(portable_key(entry.name), entry.name.back() == '/')
             .second)
      throw std::runtime_error("duplicate or case-colliding ZIP member: " +
                               entry.name);

  for (const auto &[name, directory] : names) {
    (void)directory;
    for (auto slash = name.find('/'); slash != name.npos;
         slash = name.find('/', slash + 1))
      if (const auto found = names.find(name.substr(0, slash));
          found != names.end() && !found->second)
        throw std::runtime_error(
            "ZIP file conflicts with a parent directory: " + name);
  }

  fs::create_directories(output.parent_path());
  const Stage stage(output.parent_path());
  const auto temporary = stage.path / "archive.zip";
  std::ofstream stream(temporary, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot create ZIP archive");
  std::vector<Record> records;
  for (const auto &entry : entries) {
    Record record{&entry, position(stream)};
    const bool directory = entry.name.back() == '/';
    record.attributes = (directory ? unix_directory : unix_file)
                        << attribute_shift;
    little(stream, local_signature, 4U);
    little(stream, zip_version, 2U);
    little(stream, utf8_descriptor_flags, 2U);
    little(stream, 0, 2U);  // Stored method, no compression library required.
    little(stream, 0, 2U);
    little(stream, dos_epoch_date, 2U);
    for (unsigned field = 0; field < 3U; ++field)
      little(stream, 0, 4U);  // CRC and sizes follow in the data descriptor.
    little(stream, static_cast<std::uint32_t>(entry.name.size()), 2U);
    little(stream, 0, 2U);
    stream.write(entry.name.data(),
                 static_cast<std::streamsize>(entry.name.size()));
    auto crc = crc_initial;
    const auto append = [&](const char *const data, const size_t size) {
      if (size > maximum_size - record.size)
        throw std::runtime_error(
            "ZIP member exceeds the classic ZIP size limit");
      stream.write(data, static_cast<std::streamsize>(size));
      crc = checksum(crc, data, size);
      record.size += static_cast<std::uint32_t>(size);
      (void)position(stream);
    };
    if (!directory && !entry.source.empty()) {
      validate_generated_path(entry.source);
      if (!fs::is_regular_file(entry.source))
        throw std::runtime_error("ZIP input must be a regular file: " +
                                 entry.source.u8string());
      if (fs::file_size(entry.source) > maximum_size)
        throw std::runtime_error(
            "ZIP member exceeds the classic ZIP size limit");
      if ((fs::status(entry.source).permissions() & fs::perms::owner_exec) !=
          fs::perms::none)
        record.attributes = unix_executable << attribute_shift;
      std::ifstream input(entry.source, std::ios::binary);
      if (!input)
        throw std::runtime_error("cannot read ZIP input: " +
                                 entry.source.u8string());
      std::array<char, buffer_bytes> buffer{};
      while (input.read(buffer.data(),
                        static_cast<std::streamsize>(buffer.size())) ||
             input.gcount())
        append(buffer.data(), static_cast<size_t>(input.gcount()));
      if (!input.eof())
        throw std::runtime_error("failed reading ZIP input: " +
                                 entry.source.u8string());
    } else if (!directory)
      append(entry.content.data(), entry.content.size());
    record.crc = crc ^ crc_initial;
    little(stream, descriptor_signature, 4U);
    little(stream, record.crc, 4U);
    little(stream, record.size, 4U);
    little(stream, record.size, 4U);
    records.push_back(record);
  }

  const auto central_offset = position(stream);
  for (const auto &record : records) {
    little(stream, central_signature, 4U);
    little(stream, unix_version, 2U);
    little(stream, zip_version, 2U);
    little(stream, utf8_descriptor_flags, 2U);
    little(stream, 0, 2U);
    little(stream, 0, 2U);
    little(stream, dos_epoch_date, 2U);
    little(stream, record.crc, 4U);
    little(stream, record.size, 4U);
    little(stream, record.size, 4U);
    little(stream, static_cast<std::uint32_t>(record.entry->name.size()), 2U);
    for (unsigned field = 0; field < 4U; ++field)
      little(stream, 0, 2U);  // Extra, comment, disk and internal attributes.
    little(stream, record.attributes, 4U);
    little(stream, record.offset, 4U);
    stream.write(record.entry->name.data(),
                 static_cast<std::streamsize>(record.entry->name.size()));
  }
  const auto central_size = position(stream) - central_offset;
  little(stream, end_signature, 4U);
  little(stream, 0, 2U);
  little(stream, 0, 2U);
  little(stream, static_cast<std::uint32_t>(records.size()), 2U);
  little(stream, static_cast<std::uint32_t>(records.size()), 2U);
  little(stream, central_size, 4U);
  little(stream, central_offset, 4U);
  little(stream, 0, 2U);
  (void)position(stream);
  stream.close();
  if (!stream)
    throw std::runtime_error("cannot finish ZIP archive");

  // A hard link publishes a complete file without POSIX rename's overwrite
  // behavior. Stage and destination share a filesystem; a racing writer loses
  // safely rather than replacing the winner's archive.
  fs::create_hard_link(temporary, output);
}
}  // namespace clutch
