// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Secrets in the Windows Credential Manager, as generic credentials "EmuDEN:<key>" kept on
// this machine only (never roamed).

#include <windows.h>
#include <wincred.h>

#include "den/platform/pal.h"

namespace den::pal {

namespace {

constexpr std::size_t MAX_VALUE = 2048;

std::wstring Target(const std::string& key) {
    const std::string name = "EmuDEN:" + key;
    const int size = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), out.data(), size);
    return out;
}

class CredentialSecrets final : public Secrets {
public:
    std::optional<std::string> Get(const std::string& key) override {
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(Target(key).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            return std::nullopt;
        }
        std::string value(reinterpret_cast<const char*>(credential->CredentialBlob),
                          credential->CredentialBlobSize);
        CredFree(credential);
        return value;
    }
    Status Set(const std::string& key, const std::string& value) override {
        if (value.size() > MAX_VALUE) {
            return Status::Fail("the secret is larger than 2 KiB");
        }
        std::wstring target = Target(key);
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.CredentialBlobSize = static_cast<DWORD>(value.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        wchar_t user[] = L"EmuDEN";
        credential.UserName = user;
        if (!CredWriteW(&credential, 0)) {
            return Status::Fail("the credential store refused the secret (" +
                                std::to_string(GetLastError()) + ")");
        }
        return {};
    }
    void Remove(const std::string& key) override {
        CredDeleteW(Target(key).c_str(), CRED_TYPE_GENERIC, 0);
    }
};

} // namespace

Secrets& GetSecrets() {
    static CredentialSecrets secrets;
    return secrets;
}

} // namespace den::pal
