#include "platform.hpp"
#include <array>
#include <bcrypt.h>
#include <objbase.h>
#include <stdexcept>

namespace gengine::editor
{
    std::wstring Wide(std::string_view text)
    {
        if (text.empty()) {
            return {};
        }

        // 必要なUTF-16の長さを先に求め、不正なUTF-8は置換せず拒否する。
        const int count =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (count == 0) {
            throw std::invalid_argument("invalid UTF-8");
        }

        auto result = std::wstring(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                            count);
        return result;
    }

    std::string Utf8(std::wstring_view text)
    {
        if (text.empty()) {
            return {};
        }

        // UTF-8へ変換したときのバイト数を確保し、変換できない文字列は拒否する。
        const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                              nullptr, 0, nullptr, nullptr);
        if (count == 0) {
            throw std::invalid_argument("invalid UTF-16");
        }

        auto result = std::string(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                            count, nullptr, nullptr);
        return result;
    }

    std::string PathText(const std::filesystem::path& path)
    {
        return Utf8(path.generic_wstring());
    }

    std::string ReadFile(const std::filesystem::path& path, std::size_t limit)
    {
        // スナップショットの読込み中に別プロセスから書き換えられないようにする。
        auto file = Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr));
        auto size = LARGE_INTEGER{};
        if (file.Valid() == false || GetFileSizeEx(file.Get(), &size) == FALSE || size.QuadPart < 0 ||
            static_cast<std::uint64_t>(size.QuadPart) > limit) {
            throw std::runtime_error("cannot read file or file exceeds the byte limit: " + PathText(path));
        }

        // サイズを検査してから領域を確保し、途中までの読込みを成功扱いにしない。
        auto result = std::string(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (::ReadFile(file.Get(), result.data(), static_cast<DWORD>(result.size()), &read, nullptr) == FALSE ||
            read != result.size()) {
            throw std::runtime_error("file read failed: " + PathText(path));
        }
        return result;
    }

    void WriteNewFile(const std::filesystem::path& path, std::string_view data)
    {
        // CREATE_NEWで既存ファイルを守り、書込みとフラッシュが終わってから成功とする。
        auto file =
            Handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        DWORD written = 0;
        if (file.Valid() == false ||
            WriteFile(file.Get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) == FALSE ||
            written != data.size() || FlushFileBuffers(file.Get()) == FALSE) {
            throw std::runtime_error("cannot create file without overwriting: " + PathText(path));
        }
    }

    void WriteAtomicFile(const std::filesystem::path& path, std::string_view data)
    {
        const auto temporary = path.parent_path() / (L".gengine-" + Wide(NewId()) + L".tmp");
        auto created = false;
        try {
            auto file = Handle(
                CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            created = file.Valid();
            DWORD written = 0;
            if (created == false || data.size() > MAXDWORD ||
                WriteFile(file.Get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr) == FALSE ||
                written != data.size() || FlushFileBuffers(file.Get()) == FALSE) {
                throw std::runtime_error("cannot write scene temporary file");
            }
            if (file.Close() == false) {
                throw std::runtime_error("cannot close scene temporary file");
            }
            if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
                FALSE) {
                throw std::runtime_error("cannot replace scene file; previous file retained");
            }
        } catch (...) {
            if (created) {
                DeleteFileW(temporary.c_str());
            }
            throw;
        }
    }

    std::string Sha256(std::string_view data)
    {
        auto bytes = std::array<unsigned char, 32>{};
        if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), bytes.data(), static_cast<ULONG>(bytes.size())) < 0) {
            throw std::runtime_error("SHA-256 failed");
        }

        // バイナリのハッシュを、JSONや結果照合で扱える小文字の16進表記へ変換する。
        auto result = std::string{};
        constexpr auto kHex = "0123456789abcdef";
        for (const auto value : bytes) {
            result += kHex[value >> 4];
            result += kHex[value & 15];
        }
        return result;
    }

    std::string NewId()
    {
        auto id = GUID{};
        if (FAILED(CoCreateGuid(&id))) {
            throw std::runtime_error("cannot allocate session ID");
        }

        auto buffer = std::array<wchar_t, 40>{};
        StringFromGUID2(id, buffer.data(), static_cast<int>(buffer.size()));
        const auto text = Utf8(buffer.data());
        return text.substr(1, text.size() - 2);
    }

    std::wstring QuoteArgument(std::wstring_view value)
    {
        // Windowsの引数規則に合わせ、引用符直前のバックスラッシュをまとめて処理する。
        auto result = std::wstring(L"\"");
        std::size_t slashes = 0;
        for (const wchar_t character : value) {
            if (character == L'\\') {
                ++slashes;
                continue;
            }
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            slashes = 0;
            result += character;
        }

        // 末尾のバックスラッシュが閉じる引用符をエスケープしないよう二重化する。
        result.append(slashes * 2, L'\\');
        result += L'"';
        return result;
    }
} // namespace gengine::editor
