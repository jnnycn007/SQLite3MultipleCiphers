/*
** Name:        cipher_chacha20.c
** Purpose:     Implementation of cipher ChaCha20 - Poly1305
** Author:      Ulrich Telle
** Created:     2020-02-02
** Copyright:   (c) 2006-2024 Ulrich Telle
** License:     MIT
*/

#include "cipher_common.h"

/* --- ChaCha20-Poly1305 cipher (plus sqleet variant) --- */
#if HAVE_CIPHER_CHACHA20

#define CIPHER_NAME_CHACHA20 "chacha20"

/*
** Configuration parameters for "chacha20"
**
** - legacy mode : compatibility with original sqleet
**                 (page 1 encrypted, kdf_iter = 12345)
**                 possible values:  1 = yes, 0 = no
** - kdf_iter : number of iterations for key derivation
*/

#ifdef SQLITE3MC_USE_SQLEET_LEGACY
#define CHACHA20_LEGACY_DEFAULT   1
#else
#define CHACHA20_LEGACY_DEFAULT   0
#endif

#define CHACHA20_KDF_ITER_DEFAULT 64007
#define SQLEET_KDF_ITER           12345
#define CHACHA20_LEGACY_PAGE_SIZE 4096

SQLITE_PRIVATE CipherParams mcChaCha20Params[] =
{
  { "legacy",                CHACHA20_LEGACY_DEFAULT,   CHACHA20_LEGACY_DEFAULT,   0, 1 },
  { "legacy_page_size",      CHACHA20_LEGACY_PAGE_SIZE, CHACHA20_LEGACY_PAGE_SIZE, 0, SQLITE_MAX_PAGE_SIZE },
  { "kdf_iter",              CHACHA20_KDF_ITER_DEFAULT, CHACHA20_KDF_ITER_DEFAULT, 1, 0x7fffffff },
  { "plaintext_header_size", 0,                         0,                         0, 100 /* restrict to db header size */ },
  CIPHER_PARAMS_SENTINEL
};

#define KEYLENGTH_CHACHA20       32
#define SALTLENGTH_CHACHA20      16
#define PAGE_NONCE_LEN_CHACHA20  16
#define PAGE_TAG_LEN_CHACHA20    16
#define PAGE_RESERVED_CHACHA20   (PAGE_NONCE_LEN_CHACHA20 + PAGE_TAG_LEN_CHACHA20)

typedef struct _chacha20Cipher
{
  int     m_legacy;
  int     m_legacyPageSize;
  int     m_kdfIter;
  int     m_plaintextHeaderSize;
  int     m_keyLength;
  uint8_t m_key[KEYLENGTH_CHACHA20];
  uint8_t m_salt[SALTLENGTH_CHACHA20];
} ChaCha20Cipher;

static void*
AllocateChaCha20Cipher(sqlite3* db)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) sqlite3_malloc(sizeof(ChaCha20Cipher));
  if (chacha20Cipher != NULL)
  {
    memset(chacha20Cipher, 0, sizeof(ChaCha20Cipher));
    chacha20Cipher->m_keyLength = KEYLENGTH_CHACHA20;
    memset(chacha20Cipher->m_key, 0, KEYLENGTH_CHACHA20);
    memset(chacha20Cipher->m_salt, 0, SALTLENGTH_CHACHA20);
  }
  if (chacha20Cipher != NULL)
  {
    CipherParams* cipherParams = sqlite3mcGetCipherParams(db, CIPHER_NAME_CHACHA20);
    chacha20Cipher->m_legacy = sqlite3mcGetCipherParameter(cipherParams, "legacy");
    chacha20Cipher->m_legacyPageSize = sqlite3mcGetCipherParameter(cipherParams, "legacy_page_size");
    chacha20Cipher->m_kdfIter = sqlite3mcGetCipherParameter(cipherParams, "kdf_iter");
    if (chacha20Cipher->m_legacy != 0)
    {
      chacha20Cipher->m_kdfIter = SQLEET_KDF_ITER;
    }
    chacha20Cipher->m_plaintextHeaderSize = sqlite3mcGetCipherParameter(cipherParams, "plaintext_header_size");
  }
  return chacha20Cipher;
}

static void
FreeChaCha20Cipher(void* cipher)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) cipher;
  memset(chacha20Cipher, 0, sizeof(ChaCha20Cipher));
  sqlite3_free(chacha20Cipher);
}

static void
CloneChaCha20Cipher(void* cipherTo, void* cipherFrom)
{
  ChaCha20Cipher* chacha20CipherTo = (ChaCha20Cipher*) cipherTo;
  ChaCha20Cipher* chacha20CipherFrom = (ChaCha20Cipher*) cipherFrom;
  chacha20CipherTo->m_legacy = chacha20CipherFrom->m_legacy;
  chacha20CipherTo->m_legacyPageSize = chacha20CipherFrom->m_legacyPageSize;
  chacha20CipherTo->m_kdfIter = chacha20CipherFrom->m_kdfIter;
  chacha20CipherTo->m_plaintextHeaderSize = chacha20CipherFrom->m_plaintextHeaderSize;
  chacha20CipherTo->m_keyLength = chacha20CipherFrom->m_keyLength;
  memcpy(chacha20CipherTo->m_key, chacha20CipherFrom->m_key, KEYLENGTH_CHACHA20);
  memcpy(chacha20CipherTo->m_salt, chacha20CipherFrom->m_salt, SALTLENGTH_CHACHA20);
}

static int
GetLegacyChaCha20Cipher(void* cipher)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*)cipher;
  return chacha20Cipher->m_legacy;
}

static int
GetPageSizeChaCha20Cipher(void* cipher)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) cipher;
  int pageSize = 0;
  if (chacha20Cipher->m_legacy != 0)
  {
    pageSize = chacha20Cipher->m_legacyPageSize;
    if ((pageSize < 512) || (pageSize > SQLITE_MAX_PAGE_SIZE) || (((pageSize - 1) & pageSize) != 0))
    {
      pageSize = 0;
    }
  }
  return pageSize;
}

static int
GetReservedChaCha20Cipher(void* cipher)
{
  return PAGE_RESERVED_CHACHA20;
}

static unsigned char*
GetSaltChaCha20Cipher(void* cipher)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) cipher;
  return chacha20Cipher->m_salt;
}

static void
GenerateKeyChaCha20Cipher(void* cipher, char* userPassword, int passwordLength, int rekey, unsigned char* cipherSalt)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) cipher;

  int keyOnly = 1;
  if (rekey || cipherSalt == NULL)
  {
    chacha20_rng(chacha20Cipher->m_salt, SALTLENGTH_CHACHA20);
    keyOnly = 0;
  }
  else
  {
    memcpy(chacha20Cipher->m_salt, cipherSalt, SALTLENGTH_CHACHA20);
  }

  /* Bypass key derivation, if raw key (and optionally salt) are given */
  int bypass = sqlite3mcExtractRawKey(userPassword, passwordLength,
                                      keyOnly, KEYLENGTH_CHACHA20, SALTLENGTH_CHACHA20,
                                      chacha20Cipher->m_key, chacha20Cipher->m_salt);
  if (!bypass)
  {
    fastpbkdf2_hmac_sha256((unsigned char*)userPassword, passwordLength,
                           chacha20Cipher->m_salt, SALTLENGTH_CHACHA20,
                           chacha20Cipher->m_kdfIter,
                           chacha20Cipher->m_key, KEYLENGTH_CHACHA20);
  }
  SQLITE3MC_DEBUG_LOG("generate: codec=%p pFile=%p\n", chacha20Cipher, fd);
  SQLITE3MC_DEBUG_HEX("generate  key:", chacha20Cipher->m_key, KEYLENGTH_CHACHA20);
  SQLITE3MC_DEBUG_HEX("generate salt:", chacha20Cipher->m_salt, SALTLENGTH_CHACHA20);
}

static int
EncryptPageChaCha20Cipher(void* cipher, int page, unsigned char* data, int len, int reserved)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) cipher;
  int rc = SQLITE_OK;
  int legacy = chacha20Cipher->m_legacy;
  int nReserved = (reserved == 0 && legacy == 0) ? 0 : GetReservedChaCha20Cipher(cipher);
  int n = len - nReserved;
  int usePlaintextHeader = 0;

  /* Generate one-time keys */
  uint8_t otk[64];
  uint32_t counter;
  int offset = 0;

  /* Check whether a plaintext header should be used */
  if (page == 1)
  {
    int plaintextHeaderSize = chacha20Cipher->m_plaintextHeaderSize;
    if (plaintextHeaderSize > 0)
    {
      usePlaintextHeader = 1;
      offset = (chacha20Cipher->m_legacy != 0) ? plaintextHeaderSize :
               (plaintextHeaderSize > CIPHER_PAGE1_OFFSET) ? plaintextHeaderSize : CIPHER_PAGE1_OFFSET;
    }
    else
    {
      offset = (chacha20Cipher->m_legacy != 0) ? 0 : CIPHER_PAGE1_OFFSET;
    }
  }

  /* Check whether number of required reserved bytes and actually reserved bytes match */
  if ((legacy == 0 && nReserved > reserved) || ((legacy != 0 && nReserved != reserved)))
  {
    return SQLITE_CORRUPT;
  }

  if (nReserved > 0)
  {
    /* Encrypt and authenticate */
    memset(otk, 0, 64);
    chacha20_rng(data + n, PAGE_NONCE_LEN_CHACHA20);
    counter = LOAD32_LE(data + n + PAGE_NONCE_LEN_CHACHA20 - 4) ^ page;
    chacha20_xor(otk, 64, chacha20Cipher->m_key, data + n, counter);

    chacha20_xor(data + offset, n - offset, otk + 32, data + n, counter + 1);
    if (page == 1 && usePlaintextHeader == 0)
    {
      memcpy(data, chacha20Cipher->m_salt, SALTLENGTH_CHACHA20);
    }
    poly1305(data, n + PAGE_NONCE_LEN_CHACHA20, otk, data + n + PAGE_NONCE_LEN_CHACHA20);
  }
  else
  {
    /* Encrypt only */
    uint8_t nonce[PAGE_NONCE_LEN_CHACHA20];
    memset(otk, 0, 64);
    sqlite3mcGenerateInitialVector(page, nonce);
    counter = LOAD32_LE(&nonce[PAGE_NONCE_LEN_CHACHA20 - 4]) ^ page;
    chacha20_xor(otk, 64, chacha20Cipher->m_key, nonce, counter);

    /* Encrypt */
    chacha20_xor(data + offset, n - offset, otk + 32, nonce, counter + 1);
    if (page == 1 && usePlaintextHeader == 0)
    {
      memcpy(data, chacha20Cipher->m_salt, SALTLENGTH_CHACHA20);
    }
  }

  return rc;
}

static int
chacha20_ismemset(const void* v, unsigned char value, int len)
{
  const unsigned char* a = v;
  int i = 0, result = 0;

  for (i = 0; i < len; i++) {
    result |= a[i] ^ value;
  }

  return (result != 0);
}

static int
DecryptPageChaCha20Cipher(void* cipher, int page, unsigned char* data, int len, int reserved, int hmacCheck)
{
  ChaCha20Cipher* chacha20Cipher = (ChaCha20Cipher*) cipher;
  int rc = SQLITE_OK;
  int legacy = chacha20Cipher->m_legacy;
  int nReserved = (reserved == 0 && legacy == 0) ? 0 : GetReservedChaCha20Cipher(cipher);
  int n = len - nReserved;
  int usePlaintextHeader = 0;

  /* Generate one-time keys */
  uint8_t otk[64];
  uint32_t counter;
  uint8_t tag[16];
  int offset = 0;

  /* Check whether a plaintext header should be used */
  if (page == 1)
  {
    int plaintextHeaderSize = chacha20Cipher->m_plaintextHeaderSize;
    if (plaintextHeaderSize > 0)
    {
      usePlaintextHeader = 1;
      offset = (chacha20Cipher->m_legacy != 0) ? plaintextHeaderSize :
               (plaintextHeaderSize > CIPHER_PAGE1_OFFSET) ? plaintextHeaderSize : CIPHER_PAGE1_OFFSET;
    }
    else
    {
      offset = (chacha20Cipher->m_legacy != 0) ? 0 : CIPHER_PAGE1_OFFSET;
    }
  }

  /* Check whether number of required reserved bytes and actually reserved bytes match */
  if ((legacy == 0 && nReserved > reserved) || ((legacy != 0 && nReserved != reserved)))
  {
    return (page == 1) ? SQLITE_NOTADB : SQLITE_CORRUPT;
  }

  if (nReserved > 0)
  {
    int allzero = 0;
    /* Decrypt and verify MAC */
    memset(otk, 0, 64);
    counter = LOAD32_LE(data + n + PAGE_NONCE_LEN_CHACHA20 - 4) ^ page;
    chacha20_xor(otk, 64, chacha20Cipher->m_key, data + n, counter);

    /* Determine MAC and decrypt */
    allzero = chacha20_ismemset(data, 0, n);
    poly1305(data, n + PAGE_NONCE_LEN_CHACHA20, otk, tag);
    chacha20_xor(data + offset, n - offset, otk + 32, data + n, counter + 1);

    if (hmacCheck != 0)
    {
      /* Verify the MAC */
      if (poly1305_tagcmp(data + n + PAGE_NONCE_LEN_CHACHA20, tag))
      {
        SQLITE3MC_DEBUG_LOG("decrypt: codec=%p page=%d\n", chacha20Cipher, page);
        SQLITE3MC_DEBUG_HEX("decrypt key:", chacha20Cipher->m_key, 32);
        SQLITE3MC_DEBUG_HEX("decrypt otk:", otk, 64);
        SQLITE3MC_DEBUG_HEX("decrypt data+00:", data, 16);
        SQLITE3MC_DEBUG_HEX("decrypt data+24:", data + 24, 16);
        SQLITE3MC_DEBUG_HEX("decrypt data+n:", data + n, 16);
        SQLITE3MC_DEBUG_HEX("decrypt tag r:", data + n + PAGE_NONCE_LEN_CHACHA20, PAGE_TAG_LEN_CHACHA20);
        SQLITE3MC_DEBUG_HEX("decrypt tag c:", tag, PAGE_TAG_LEN_CHACHA20);
        /* Bad MAC */
        rc = (page == 1) ? SQLITE_NOTADB : SQLITE_CORRUPT;
      }
    }
    if (page == 1 && usePlaintextHeader == 0 && rc == SQLITE_OK)
    {
      memcpy(data, SQLITE_FILE_HEADER, 16);
    }
  }
  else
  {
    /* Decrypt only */
    uint8_t nonce[PAGE_NONCE_LEN_CHACHA20];
    memset(otk, 0, 64);
    sqlite3mcGenerateInitialVector(page, nonce);
    counter = LOAD32_LE(&nonce[PAGE_NONCE_LEN_CHACHA20 - 4]) ^ page;
    chacha20_xor(otk, 64, chacha20Cipher->m_key, nonce, counter);

    /* Decrypt */
    chacha20_xor(data + offset, n - offset, otk + 32, nonce, counter + 1);
    if (page == 1 && usePlaintextHeader == 0)
    {
      memcpy(data, SQLITE_FILE_HEADER, 16);
    }
  }

  return rc;
}

SQLITE_PRIVATE const CipherDescriptor mcChaCha20Descriptor =
{
  CIPHER_NAME_CHACHA20,
  AllocateChaCha20Cipher,
  FreeChaCha20Cipher,
  CloneChaCha20Cipher,
  GetLegacyChaCha20Cipher,
  GetPageSizeChaCha20Cipher,
  GetReservedChaCha20Cipher,
  GetSaltChaCha20Cipher,
  GenerateKeyChaCha20Cipher,
  EncryptPageChaCha20Cipher,
  DecryptPageChaCha20Cipher
};
#endif
