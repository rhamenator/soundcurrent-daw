// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/session.hpp>
#include <array>
#include <span>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif
namespace soundcurrent::daw::detail {
inline void hashRequire(bool ok,const char *message) {
    if (!ok) throw ProjectError(ErrorCode::Io,message);
}
class Hash {
  public:
    Hash(const Hash &)=delete;
    Hash &operator=(const Hash &)=delete;
    Hash() {
#ifdef _WIN32
        hashRequire(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"Cannot open media SHA256 provider");
        if (BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)!=0) {
            BCryptCloseAlgorithmProvider(algorithm_,0); algorithm_=nullptr;
            throw ProjectError(ErrorCode::Io,"Cannot create media SHA256 context");
        }
#else
        hash_=EVP_MD_CTX_new(); hashRequire(hash_!=nullptr,"Cannot create media SHA256 context");
        if (EVP_DigestInit_ex(hash_,EVP_sha256(),nullptr)!=1) {
            EVP_MD_CTX_free(hash_);hash_=nullptr;throw ProjectError(ErrorCode::Io,"Cannot initialize media SHA256");
        }
#endif
    }
    ~Hash() {
#ifdef _WIN32
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_,0);
#else
        EVP_MD_CTX_free(hash_);
#endif
    }
    void add(std::span<const char> bytes) {
#ifdef _WIN32
        hashRequire(BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())),static_cast<ULONG>(bytes.size()),0)==0,"Media SHA256 update failed");
#else
        hashRequire(EVP_DigestUpdate(hash_,bytes.data(),bytes.size())==1,"Media SHA256 update failed");
#endif
    }
    std::array<char,64> finish() {
        std::array<unsigned char,32> bytes{};
#ifdef _WIN32
        hashRequire(BCryptFinishHash(hash_,bytes.data(),static_cast<ULONG>(bytes.size()),0)==0,"Media SHA256 finish failed");
#else
        unsigned count=0;hashRequire(EVP_DigestFinal_ex(hash_,bytes.data(),&count)==1 && count==bytes.size(),"Media SHA256 finish failed");
#endif
        std::array<char,64> result{};constexpr auto hex="0123456789abcdef";
        for (std::size_t i=0;i<bytes.size();++i) { result[2*i]=hex[bytes[i]>>4]; result[2*i+1]=hex[bytes[i]&15]; }
        return result;
    }
  private:
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm_=nullptr; BCRYPT_HASH_HANDLE hash_=nullptr;
#else
    EVP_MD_CTX *hash_=nullptr;
#endif
};
} // namespace soundcurrent::daw::detail
