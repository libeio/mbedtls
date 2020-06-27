/*
 *  Privacy Enhanced Mail (PEM) decoding
 *
 *  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_PEM_PARSE_C) || defined(MBEDTLS_PEM_WRITE_C)

#include "mbedtls/pem.h"
#include "mbedtls/base64.h"
#include "mbedtls/des.h"
#include "mbedtls/aes.h"
#include "mbedtls/md5.h"
#include "mbedtls/cipher.h"
#include "mbedtls/platform_util.h"

#include <string.h>

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdlib.h>
#define mbedtls_calloc    calloc
#define mbedtls_free       free
#endif

#if defined(MBEDTLS_PEM_PARSE_C)
void mbedtls_pem_init( mbedtls_pem_context *ctx )
{
    memset( ctx, 0, sizeof( mbedtls_pem_context ) );
}

#if defined(MBEDTLS_MD5_C) && defined(MBEDTLS_CIPHER_MODE_CBC) &&         \
    ( defined(MBEDTLS_DES_C) || defined(MBEDTLS_AES_C) )
/*
 * Read a 16-byte hex string and convert it to binary
 */
static int pem_get_iv( const unsigned char *s, unsigned char *iv,   //获取加密文件（密钥文件、证书请求文件、证书等）的 初始化向量 iv
                       size_t iv_len )
{
    size_t i, j, k;

    memset( iv, 0, iv_len );

    for( i = 0; i < iv_len * 2; i++, s++ )
    {
        if( *s >= '0' && *s <= '9' ) j = *s - '0'; else
        if( *s >= 'A' && *s <= 'F' ) j = *s - '7'; else
        if( *s >= 'a' && *s <= 'f' ) j = *s - 'W'; else
            return( MBEDTLS_ERR_PEM_INVALID_ENC_IV );

        k = ( ( i & 1 ) != 0 ) ? j : j << 4;        //偶位高字节，奇位低字节  D1 -> 1101 0001

        iv[i >> 1] = (unsigned char)( iv[i >> 1] | k );
    }

    return( 0 );
}

static int pem_pbkdf1( unsigned char *key, size_t keylen,           //根据 iv 和 保护口令 获得 对称加密密钥
                       unsigned char *iv,
                       const unsigned char *pwd, size_t pwdlen )
{
    mbedtls_md5_context md5_ctx;
    unsigned char md5sum[16];
    size_t use_len;
    int ret;

    mbedtls_md5_init( &md5_ctx );

    /*
     * key[ 0..15] = MD5(pwd || IV)
     */
    if( ( ret = mbedtls_md5_starts_ret( &md5_ctx ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_update_ret( &md5_ctx, pwd, pwdlen ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_update_ret( &md5_ctx, iv,  8 ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_finish_ret( &md5_ctx, md5sum ) ) != 0 )
        goto exit;

    if( keylen <= 16 )
    {
        memcpy( key, md5sum, keylen );
        goto exit;
    }

    memcpy( key, md5sum, 16 );

    /*
     * key[16..23] = MD5(key[ 0..15] || pwd || IV])
     */
    if( ( ret = mbedtls_md5_starts_ret( &md5_ctx ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_update_ret( &md5_ctx, md5sum, 16 ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_update_ret( &md5_ctx, pwd, pwdlen ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_update_ret( &md5_ctx, iv, 8 ) ) != 0 )
        goto exit;
    if( ( ret = mbedtls_md5_finish_ret( &md5_ctx, md5sum ) ) != 0 )
        goto exit;

    use_len = 16;
    if( keylen < 32 )
        use_len = keylen - 16;

    memcpy( key + 16, md5sum, use_len );

exit:
    mbedtls_md5_free( &md5_ctx );
    mbedtls_platform_zeroize( md5sum, 16 );

    return( ret );
}

#if defined(MBEDTLS_DES_C)
/*
 * Decrypt with DES-CBC, using PBKDF1 for key derivation
 */
static int pem_des_decrypt( unsigned char des_iv[8],                    //对base64解码后的内容进行des解密，获取asn.1编码内容
                            unsigned char *buf, size_t buflen,
                            const unsigned char *pwd, size_t pwdlen )
{
    mbedtls_des_context des_ctx;
    unsigned char des_key[8];
    int ret;

    mbedtls_des_init( &des_ctx );

    if( ( ret = pem_pbkdf1( des_key, 8, des_iv, pwd, pwdlen ) ) != 0 )
        goto exit;

    if( ( ret = mbedtls_des_setkey_dec( &des_ctx, des_key ) ) != 0 )
        goto exit;
    ret = mbedtls_des_crypt_cbc( &des_ctx, MBEDTLS_DES_DECRYPT, buflen,
                     des_iv, buf, buf );

exit:
    mbedtls_des_free( &des_ctx );
    mbedtls_platform_zeroize( des_key, 8 );

    return( ret );
}

/*
 * Decrypt with 3DES-CBC, using PBKDF1 for key derivation
 */
static int pem_des3_decrypt( unsigned char des3_iv[8],              //对base64解码后的内容进行3des解密，获取asn.1编码内容
                             unsigned char *buf, size_t buflen,
                             const unsigned char *pwd, size_t pwdlen )
{
    mbedtls_des3_context des3_ctx;
    unsigned char des3_key[24];
    int ret;

    mbedtls_des3_init( &des3_ctx );

    if( ( ret = pem_pbkdf1( des3_key, 24, des3_iv, pwd, pwdlen ) ) != 0 )   //设置3des加解密密钥
        goto exit;

    if( ( ret = mbedtls_des3_set3key_dec( &des3_ctx, des3_key ) ) != 0 )    //密钥绑定
        goto exit;
    ret = mbedtls_des3_crypt_cbc( &des3_ctx, MBEDTLS_DES_DECRYPT, buflen,   //解密，输入输出使用相同缓冲
                     des3_iv, buf, buf );

exit:
    mbedtls_des3_free( &des3_ctx );
    mbedtls_platform_zeroize( des3_key, 24 );

    return( ret );
}
#endif /* MBEDTLS_DES_C */

#if defined(MBEDTLS_AES_C)
/*
 * Decrypt with AES-XXX-CBC, using PBKDF1 for key derivation
 */
static int pem_aes_decrypt( unsigned char aes_iv[16], unsigned int keylen,  //对base64解码后的内容进行aes解密，获取asn.1编码内容
                            unsigned char *buf, size_t buflen,
                            const unsigned char *pwd, size_t pwdlen )
{
    mbedtls_aes_context aes_ctx;
    unsigned char aes_key[32];
    int ret;

    mbedtls_aes_init( &aes_ctx );

    if( ( ret = pem_pbkdf1( aes_key, keylen, aes_iv, pwd, pwdlen ) ) != 0 )
        goto exit;

    if( ( ret = mbedtls_aes_setkey_dec( &aes_ctx, aes_key, keylen * 8 ) ) != 0 )
        goto exit;
    ret = mbedtls_aes_crypt_cbc( &aes_ctx, MBEDTLS_AES_DECRYPT, buflen,
                     aes_iv, buf, buf );

exit:
    mbedtls_aes_free( &aes_ctx );
    mbedtls_platform_zeroize( aes_key, keylen );

    return( ret );
}
#endif /* MBEDTLS_AES_C */

#endif /* MBEDTLS_MD5_C && MBEDTLS_CIPHER_MODE_CBC &&
          ( MBEDTLS_AES_C || MBEDTLS_DES_C ) */

int mbedtls_pem_read_buffer( mbedtls_pem_context *ctx, const char *header, const char *footer,
                     const unsigned char *data, const unsigned char *pwd,
                     size_t pwdlen, size_t *use_len )
{
    int ret, enc;
    size_t len;
    unsigned char *buf;
    const unsigned char *s1, *s2, *end;
#if defined(MBEDTLS_MD5_C) && defined(MBEDTLS_CIPHER_MODE_CBC) &&         \
    ( defined(MBEDTLS_DES_C) || defined(MBEDTLS_AES_C) )
    unsigned char pem_iv[16];
    mbedtls_cipher_type_t enc_alg = MBEDTLS_CIPHER_NONE;
#else
    ((void) pwd);
    ((void) pwdlen);
#endif /* MBEDTLS_MD5_C && MBEDTLS_CIPHER_MODE_CBC &&
          ( MBEDTLS_AES_C || MBEDTLS_DES_C ) */

    if( ctx == NULL )
        return( MBEDTLS_ERR_PEM_BAD_INPUT_DATA );

    s1 = (unsigned char *) strstr( (const char *) data, header );       //指向标头指针，如-----BEGIN RSA PRIVATE KEY-----, -----BEGIN CERTIFICATE-----

    if( s1 == NULL )
        return( MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT );

    s2 = (unsigned char *) strstr( (const char *) data, footer );       //指向标脚指针，如-----END RSA PRIVATE KEY-----, -----END CERTIFICATE-----

    if( s2 == NULL || s2 <= s1 )
        return( MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT );

    s1 += strlen( header );     //跳过标头
    if( *s1 == ' '  ) s1++;
    if( *s1 == '\r' ) s1++;
    if( *s1 == '\n' ) s1++;
    else return( MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT );

    end = s2;                   //s2 相当于一个固定哨兵
    end += strlen( footer );    //跳过标脚
    if( *end == ' '  ) end++;
    if( *end == '\r' ) end++;
    if( *end == '\n' ) end++;
    *use_len = end - data;      //记录本次处理的文本长度

    enc = 0;

    if( s2 - s1 >= 22 && memcmp( s1, "Proc-Type: 4,ENCRYPTED", 22 ) == 0 )
    {
#if defined(MBEDTLS_MD5_C) && defined(MBEDTLS_CIPHER_MODE_CBC) &&         \     //对于密钥文件，可以结合一个pem格式的 fd.key 来看
    ( defined(MBEDTLS_DES_C) || defined(MBEDTLS_AES_C) )
        enc++;

        s1 += 22;
        if( *s1 == '\r' ) s1++;
        if( *s1 == '\n' ) s1++;
        else return( MBEDTLS_ERR_PEM_INVALID_DATA );


#if defined(MBEDTLS_DES_C)
        if( s2 - s1 >= 23 && memcmp( s1, "DEK-Info: DES-EDE3-CBC,", 23 ) == 0 ) //3DES CBC 模式加密
        {
            enc_alg = MBEDTLS_CIPHER_DES_EDE3_CBC;

            s1 += 23;
            if( s2 - s1 < 16 || pem_get_iv( s1, pem_iv, 8 ) != 0 )  //获取初始化向量，保存在 pem_iv 中
                return( MBEDTLS_ERR_PEM_INVALID_ENC_IV );

            s1 += 16;   //跳过 iv 字符串
        }
        else if( s2 - s1 >= 18 && memcmp( s1, "DEK-Info: DES-CBC,", 18 ) == 0 ) //DES CBC 模式加密
        {
            enc_alg = MBEDTLS_CIPHER_DES_CBC;

            s1 += 18;
            if( s2 - s1 < 16 || pem_get_iv( s1, pem_iv, 8) != 0 )
                return( MBEDTLS_ERR_PEM_INVALID_ENC_IV );

            s1 += 16;   //跳过 iv 字符串
        }
#endif /* MBEDTLS_DES_C */

#if defined(MBEDTLS_AES_C)
        if( s2 - s1 >= 14 && memcmp( s1, "DEK-Info: AES-", 14 ) == 0 )
        {
            if( s2 - s1 < 22 )
                return( MBEDTLS_ERR_PEM_UNKNOWN_ENC_ALG );
            else if( memcmp( s1, "DEK-Info: AES-128-CBC,", 22 ) == 0 )  //128bits AES CBC
                enc_alg = MBEDTLS_CIPHER_AES_128_CBC;
            else if( memcmp( s1, "DEK-Info: AES-192-CBC,", 22 ) == 0 )  //192bits AES CBC
                enc_alg = MBEDTLS_CIPHER_AES_192_CBC;
            else if( memcmp( s1, "DEK-Info: AES-256-CBC,", 22 ) == 0 )  //256bits AES CBC
                enc_alg = MBEDTLS_CIPHER_AES_256_CBC;
            else
                return( MBEDTLS_ERR_PEM_UNKNOWN_ENC_ALG );

            s1 += 22;
            if( s2 - s1 < 32 || pem_get_iv( s1, pem_iv, 16 ) != 0 )
                return( MBEDTLS_ERR_PEM_INVALID_ENC_IV );

            s1 += 32;   //跳过 iv 字符串
        }
#endif /* MBEDTLS_AES_C */

        if( enc_alg == MBEDTLS_CIPHER_NONE )
            return( MBEDTLS_ERR_PEM_UNKNOWN_ENC_ALG );

        if( *s1 == '\r' ) s1++;     //跳过空行
        if( *s1 == '\n' ) s1++;
        else return( MBEDTLS_ERR_PEM_INVALID_DATA );
#else
        return( MBEDTLS_ERR_PEM_FEATURE_UNAVAILABLE );
#endif /* MBEDTLS_MD5_C && MBEDTLS_CIPHER_MODE_CBC &&
          ( MBEDTLS_AES_C || MBEDTLS_DES_C ) */
    }

    if( s1 >= s2 )
        return( MBEDTLS_ERR_PEM_INVALID_DATA );

    ret = mbedtls_base64_decode( NULL, 0, &len, s1, s2 - s1 );  //获取 base64 解码内容长度

    if( ret == MBEDTLS_ERR_BASE64_INVALID_CHARACTER )
        return( MBEDTLS_ERR_PEM_INVALID_DATA + ret );

    if( ( buf = mbedtls_calloc( 1, len ) ) == NULL )
        return( MBEDTLS_ERR_PEM_ALLOC_FAILED );

    if( ( ret = mbedtls_base64_decode( buf, len, &len, s1, s2 - s1 ) ) != 0 )   //正式解码
    {
        mbedtls_platform_zeroize( buf, len );
        mbedtls_free( buf );
        return( MBEDTLS_ERR_PEM_INVALID_DATA + ret );
    }

    if( enc != 0 )
    {
#if defined(MBEDTLS_MD5_C) && defined(MBEDTLS_CIPHER_MODE_CBC) &&         \
    ( defined(MBEDTLS_DES_C) || defined(MBEDTLS_AES_C) )
        if( pwd == NULL )
        {
            mbedtls_platform_zeroize( buf, len );
            mbedtls_free( buf );
            return( MBEDTLS_ERR_PEM_PASSWORD_REQUIRED );
        }

        ret = 0;

#if defined(MBEDTLS_DES_C)
        if( enc_alg == MBEDTLS_CIPHER_DES_EDE3_CBC )
            ret = pem_des3_decrypt( pem_iv, buf, len, pwd, pwdlen );    //以证书为例，在对证书解码后获取的是证书的密文。要想获取明文，需要根据 DEK-Info 提供的信息
        else if( enc_alg == MBEDTLS_CIPHER_DES_CBC )                    //进行解密，解密的密钥由 初始化向量和保护口令（如果有的话）共同生成，不需要人工提供
            ret = pem_des_decrypt( pem_iv, buf, len, pwd, pwdlen );     //如果创建证书时，不使用密钥加密策略，则这一步也可以省略
#endif /* MBEDTLS_DES_C */

#if defined(MBEDTLS_AES_C)
        if( enc_alg == MBEDTLS_CIPHER_AES_128_CBC )
            ret = pem_aes_decrypt( pem_iv, 16, buf, len, pwd, pwdlen );
        else if( enc_alg == MBEDTLS_CIPHER_AES_192_CBC )
            ret = pem_aes_decrypt( pem_iv, 24, buf, len, pwd, pwdlen );
        else if( enc_alg == MBEDTLS_CIPHER_AES_256_CBC )
            ret = pem_aes_decrypt( pem_iv, 32, buf, len, pwd, pwdlen );
#endif /* MBEDTLS_AES_C */

        if( ret != 0 )
        {
            mbedtls_free( buf );
            return( ret );
        }

        /*
         * The result will be ASN.1 starting with a SEQUENCE tag, with 1 to 3
         * length bytes (allow 4 to be sure) in all known use cases.
         *
         * Use that as a heuristic to try to detect password mismatches.
         */
        if( len <= 2 || buf[0] != 0x30 || buf[1] > 0x83 )
        {
            mbedtls_platform_zeroize( buf, len );
            mbedtls_free( buf );
            return( MBEDTLS_ERR_PEM_PASSWORD_MISMATCH );
        }
#else
        mbedtls_platform_zeroize( buf, len );
        mbedtls_free( buf );
        return( MBEDTLS_ERR_PEM_FEATURE_UNAVAILABLE );
#endif /* MBEDTLS_MD5_C && MBEDTLS_CIPHER_MODE_CBC &&
          ( MBEDTLS_AES_C || MBEDTLS_DES_C ) */
    }

    ctx->buf = buf;             //文件（如证书）的的明文，注意，此时文件仍是 ASN.1编码，还需要调用 ASN.1 解码器对其解析
    ctx->buflen = len;          //文件的长度

    return( 0 );
}

void mbedtls_pem_free( mbedtls_pem_context *ctx )
{
    if( ctx->buf != NULL )
        mbedtls_platform_zeroize( ctx->buf, ctx->buflen );
    mbedtls_free( ctx->buf );
    mbedtls_free( ctx->info );

    mbedtls_platform_zeroize( ctx, sizeof( mbedtls_pem_context ) );
}
#endif /* MBEDTLS_PEM_PARSE_C */

#if defined(MBEDTLS_PEM_WRITE_C)
int mbedtls_pem_write_buffer( const char *header, const char *footer,       //以证书为例，对一个ASN.1内容的证书文件，进行base64编码，添加相应 header 与 footer，生成 pem 格式证书
                      const unsigned char *der_data, size_t der_len,
                      unsigned char *buf, size_t buf_len, size_t *olen )
{
    int ret;
    unsigned char *encode_buf = NULL, *c, *p = buf;
    size_t len = 0, use_len, add_len = 0;

    mbedtls_base64_encode( NULL, 0, &use_len, der_data, der_len );      //尝试对文件进行base64编码，获取所需要的长度
    add_len = strlen( header ) + strlen( footer ) + ( use_len / 64 ) + 1;

    if( use_len + add_len > buf_len )   //对缓冲长度进行校验
    {
        *olen = use_len + add_len;
        return( MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL );
    }

    if( use_len != 0 &&
        ( ( encode_buf = mbedtls_calloc( 1, use_len ) ) == NULL ) )
        return( MBEDTLS_ERR_PEM_ALLOC_FAILED );

    if( ( ret = mbedtls_base64_encode( encode_buf, use_len, &use_len, der_data,     //正式对 ASN.1 文件进行编码
                               der_len ) ) != 0 )
    {
        mbedtls_free( encode_buf );
        return( ret );
    }

    memcpy( p, header, strlen( header ) );      //添加 header
    p += strlen( header );
    c = encode_buf;

    while( use_len )
    {
        len = ( use_len > 64 ) ? 64 : use_len;
        memcpy( p, c, len );
        use_len -= len;
        p += len;
        c += len;
        *p++ = '\n';
    }

    memcpy( p, footer, strlen( footer ) );      //添加 footer
    p += strlen( footer );

    *p++ = '\0';
    *olen = p - buf;

    mbedtls_free( encode_buf );
    return( 0 );
}
#endif /* MBEDTLS_PEM_WRITE_C */
#endif /* MBEDTLS_PEM_PARSE_C || MBEDTLS_PEM_WRITE_C */
