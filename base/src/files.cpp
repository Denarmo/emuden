// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "den/base/files.h"

namespace den {

namespace fs = std::filesystem;

fs::path PathFromUtf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string PathToUtf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string ToLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    });
    return out;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && ToLower(a) == ToLower(b);
}

std::string Trim(std::string_view text) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::optional<std::vector<std::uint8_t>> ReadBinaryFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        return std::nullopt;
    }
    return data;
}

std::optional<std::string> ReadTextFile(const fs::path& path) {
    auto data = ReadBinaryFile(path);
    if (!data) {
        return std::nullopt;
    }
    return std::string(data->begin(), data->end());
}

bool WriteFileAtomic(const fs::path& path, std::string_view data) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) {
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

std::uint64_t Fnv1a(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::uint32_t Crc32(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::optional<std::uint64_t> HashFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::uint64_t hash = FNV_OFFSET;
    std::vector<char> buffer(1 << 16);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hash = Fnv1a(hash, buffer.data(), static_cast<std::size_t>(file.gcount()));
    }
    return hash;
}

std::string GetEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

bool IsRegularFile(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool IsDirectory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::vector<fs::path> ListDirectory(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    const fs::directory_iterator end;
    while (!ec && it != end) {
        out.push_back(it->path());
        it.increment(ec);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace den
