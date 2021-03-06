/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013  LiteSpeed Technologies, Inc.                        *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#include "sslcontext.h"
#include "sslconnection.h"
#include "sslerror.h"

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

//#include "http/vhostmap.h"



static const SSL_METHOD* getMethod( int iMethod )
{
    switch( iMethod )
    {
    //case SSLContext::SSL_v2:
    //    return SSLv2_method();
    case SSLContext::SSL_v3:
        return SSLv3_method();
    case SSLContext::SSL_TLSv1:
        return TLSv1_method();
    case SSLContext::SSL_ALL:
    default:
        return SSLv23_method();
    }
}

/*
static SSL_METHOD* getServerMethod( int iMethod )
{
    switch( iMethod )
    {
    case SSL_Context::SSL_v2:
        meth = SSLv2_server_method();
        break;
    case SSL_Context::SSL_v3:
        meth = SSLv3_server_method();
        break;
    case SSL_Context::SSL_TLSv1:
        meth = TLSv1_server_method();
        break;
    case SSL_Context::SSL_all:
    default:
        meth = SSLv23_server_method();
    }
}

static SSL_METHOD* getClientMethod( int iMethod )
{
    switch( iMethod )
    {
    case SSL_Context::SSL_v2:
        meth = SSLv2_client_method();
        break;
    case SSL_Context::SSL_v3:
        meth = SSLv3_client_method();
        break;
    case SSL_Context::SSL_TLSv1:
        meth = TLSv1_client_method();
        break;
    case SSL_Context::SSL_all:
    default:
        meth = SSLv23_client_method();
    }
}
int SSLContext::initServer( int iMethod)
{
    SSL_METHOD * meth;
    if ( s_pCtx == NULL )
    {
        SSL_load_error_strings();
        SSLeay_add_ssl_algorithms();
        meth = getServerMethod( iMethod );
        s_pCtx = SSL_CTX_new (meth);
    }
}
SSL_MODE_ENABLE_PARTIAL_WRITE
*/

long SSLContext::setOptions( long options )
{
    return SSL_CTX_set_options( m_pCtx, options );
}

long SSLContext::setSessionCacheMode( long mode )
{
    return SSL_CTX_set_session_cache_mode( m_pCtx, mode );
}

int SSLContext::seedRand(int len)
{
    static int fd = open( "/dev/urandom", O_RDONLY|O_NONBLOCK );
    char achBuf[2048];
    int ret;
    if ( fd >= 0 )
    {
        int toRead;
        do
        {
            toRead = sizeof( achBuf );
            if ( len < toRead )
                toRead = len;
            ret = read( fd, achBuf, toRead );
            if ( ret > 0 )
            {
                RAND_seed( achBuf, ret );
                len -= ret;
            }
        }while((len > 0 ) &&( ret == toRead ));
        fcntl( fd, F_SETFD, FD_CLOEXEC );
        //close( fd );
    }
    else
    {
 #ifdef DEVRANDOM_EGD
        /* Use an EGD socket to read entropy from an EGD or PRNGD entropy
         * collecting daemon. */
        static const char * egdsockets[] = { "/var/run/egd-pool", "/dev/egd-pool", "/etc/egd-pool" };
        for (egdsocket = egdsockets; *egdsocket && n < len; egdsocket++)
        {

            ret = RAND_egd_bytes(*egdsocket, len);
            if (ret > 0)
                len -= ret;
        }
#endif
    }
    if ( len == 0 )
        return 0;
    if ( len > (int)sizeof( achBuf ) )
        len = (int)sizeof( achBuf );
    gettimeofday( (timeval *)achBuf, NULL);
    ret = sizeof( timeval );
    *(pid_t *)(achBuf + ret ) = getpid();   
    ret += sizeof( pid_t );
    *(uid_t *)( achBuf + ret ) = getuid();
    ret += sizeof( uid_t );
    if ( len > ret )
        memmove( achBuf + ret, achBuf + sizeof( achBuf ) - len + ret, len - ret );
    RAND_seed( achBuf, len );
    return 0;
}

static void SSLConnection_ssl_info_cb( const SSL *pSSL, int where, int ret)
{
    //if ((where & SSL_CB_HANDSHAKE_START) != 0) 
    //{
    //    //close connection, may not needed for 0.9.8m and later
    //}
    if ((where & SSL_CB_HANDSHAKE_DONE) != 0) 
    {
         pSSL->s3->flags |= SSL3_FLAGS_NO_RENEGOTIATE_CIPHERS;
    }
}


int SSLContext::init( int iMethod )
{
    if ( m_pCtx == NULL )
    {
        SSL_METHOD * meth;
        if ( initSSL() )
            return -1;
        m_iMethod = iMethod;
        meth = (SSL_METHOD*)getMethod( iMethod );
        m_pCtx = SSL_CTX_new (meth);
        if ( m_pCtx )
        {
#ifdef SSL_OP_NO_COMPRESSION
            /* OpenSSL >= 1.0 only */
            SSL_CTX_set_options(m_pCtx, SSL_OP_NO_COMPRESSION);
#endif
            setOptions( SSL_OP_SINGLE_DH_USE|SSL_OP_ALL );
            //setOptions( SSL_OP_NO_SSLv2 );
            setOptions( SSL_OP_NO_SSLv2 );

            setOptions( SSL_OP_CIPHER_SERVER_PREFERENCE);

            SSL_CTX_set_mode( m_pCtx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER );
            if ( m_iRenegProtect )
            {
                setOptions( SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION );
                SSL_CTX_set_info_callback( m_pCtx, SSLConnection_ssl_info_cb );
            }
            return 0;
        }
        else
        {
            //FIXME: log ssl error
            return -1;
        }
    }
    return 0;
}

SSLContext::SSLContext( int iMethod )
    : m_pCtx( NULL )
    , m_iMethod( iMethod )
    , m_iRenegProtect( 1 )
{
}
SSLContext::~SSLContext()
{
    release();
}


void SSLContext::release()
{
    if ( m_pCtx != NULL )
    {
        SSL_CTX* pCtx = m_pCtx;
        m_pCtx = NULL;
        SSL_CTX_free( pCtx );
    }
}


SSL* SSLContext::newSSL()
{
    init( m_iMethod );
    seedRand( 128 );
    return SSL_new(m_pCtx);
}

static int translateType( int type )
{
    switch( type )
    {
    case SSLContext::FILETYPE_PEM:
        return SSL_FILETYPE_PEM;
    case SSLContext::FILETYPE_ASN1:
        return SSL_FILETYPE_ASN1;
    default:
        return -1;
    }
}

static bool isFileChanged( const char * pFile, const struct stat &stOld )
{
    struct stat st;
    if ( ::stat( pFile, &st ) == -1 )
        return false;
    return ((st.st_size != stOld.st_size)||
            (st.st_ino != stOld.st_ino )||
            (st.st_mtime != stOld.st_mtime ));
}

bool SSLContext::isKeyFileChanged( const char * pKeyFile ) const
{
    return isFileChanged( pKeyFile, m_stKey );
}
bool SSLContext::isCertFileChanged( const char * pCertFile ) const
{
    return isFileChanged( pCertFile, m_stCert );
}

bool SSLContext::setKeyCertificateFile( const char * pFile, int iType, int chained )
{
    return setKeyCertificateFile( pFile, iType, pFile, iType, chained );
}

bool SSLContext::setKeyCertificateFile( const char * pKeyFile, int iKeyType,
                                        const char * pCertFile, int iCertType,
                                        int chained )
{
    if ( !setCertificateFile( pCertFile, iCertType, chained ) )
        return false;
    if ( !setPrivateKeyFile( pKeyFile, iKeyType ) )
        return false;
    return  SSL_CTX_check_private_key( m_pCtx ) == 1;
}

bool SSLContext::setCertificateFile( const char * pFile, int type, int chained )
{
    if ( !pFile )
        return false;
    ::stat( pFile, &m_stCert );
    if ( init( m_iMethod ) )
        return false;
    if ( chained )
        return SSL_CTX_use_certificate_chain_file( m_pCtx, pFile ) == 1;
    else
        return SSL_CTX_use_certificate_file( m_pCtx, pFile,
            translateType( type ) ) == 1;
}


bool SSLContext::setCertificateChainFile( const char * pFile )
{
    BIO *bio;
    X509 *x509;
    STACK_OF(X509) * pExtraCerts;
    unsigned long err;
    int n;

    if ((bio = BIO_new(BIO_s_file_internal())) == NULL)
        return -1;
    if (BIO_read_filename(bio, pFile) <= 0) {
        BIO_free(bio);
        return -1;
    }
    pExtraCerts=m_pCtx->extra_certs;
    if ( pExtraCerts != NULL) {
        sk_X509_pop_free((STACK_OF(X509) *)pExtraCerts, X509_free);
        m_pCtx->extra_certs = NULL;
    }
    n = 0;
    while ((x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL)) != NULL) {
        if (!SSL_CTX_add_extra_chain_cert(m_pCtx, x509)) {
            X509_free(x509);
            BIO_free(bio);
            return -1;
        }
        n++;
    }
    if ((err = ERR_peek_error()) > 0) {
        if (!(   ERR_GET_LIB(err) == ERR_LIB_PEM
              && ERR_GET_REASON(err) == PEM_R_NO_START_LINE)) {
            BIO_free(bio);
            return -1;
        }
        while (ERR_get_error() > 0) ;
    }
    BIO_free(bio);
    return n > 0;
}



bool SSLContext::setCALocation( const char * pCAFile, const char * pCAPath, int cv )
{
    if ( init( m_iMethod ) )
        return false;
    int ret = SSL_CTX_load_verify_locations(m_pCtx, pCAFile, pCAPath);
    if ( (ret != 0) && cv )
    {
        ret = SSL_CTX_set_default_verify_paths( m_pCtx );
        STACK_OF(X509_NAME) *pCAList = NULL;
        if ( pCAFile ) 
        {
            pCAList = SSL_load_client_CA_file( pCAFile );
        }
        if ( pCAPath )
        {
            if ( !pCAList ) 
            {
                pCAList = sk_X509_NAME_new_null();
            }
            if ( !SSL_add_dir_cert_subjects_to_stack( pCAList, pCAPath ) )
            {
                sk_X509_NAME_free( pCAList );
                pCAList = NULL;
            }
        }
        if ( pCAList )
            SSL_CTX_set_client_CA_list( m_pCtx, pCAList );
    }

    return ret != 0;
}

bool SSLContext::setPrivateKeyFile( const char * pFile, int type )
{
    if ( !pFile )
        return false;
    ::stat( pFile, &m_stKey );
    if ( init( m_iMethod ) )
        return false;
    return SSL_CTX_use_PrivateKey_file( m_pCtx, pFile,
            translateType( type ) ) == 1;
}

bool SSLContext::checkPrivateKey()
{
    if ( m_pCtx )
        return SSL_CTX_check_private_key( m_pCtx ) == 1;
    else
        return false;
}

bool SSLContext::setCipherList( const char * pList )
{
    if ( m_pCtx )
    {
        char cipher[4096];

        if ( strncasecmp( pList, "ALL:", 4 ) == 0 )
        {
            //snprintf( cipher, 4095, "RC4:%s", pList );
            //strcpy( cipher, "ALL:HIGH:!aNULL:!SSLV2:!eNULL" );
            strcpy( cipher, "RC4:HIGH:!aNULL:!MD5:!SSLv2:!eNULL:!EDH:!LOW:!EXPORT56:!EXPORT40" );
            //strcpy( cipher, "RC4:-EXP:-SSLv2:-ADH" );
            pList = cipher;
        }

        return SSL_CTX_set_cipher_list( m_pCtx, pList ) == 1;
    }
    else
        return false;
}



/*
SL_CTX_set_verify(ctx, nVerify,  ssl_callback_SSLVerify);
    SSL_CTX_sess_set_new_cb(ctx,      ssl_callback_NewSessionCacheEntry);
    SSL_CTX_sess_set_get_cb(ctx,      ssl_callback_GetSessionCacheEntry);
    SSL_CTX_sess_set_remove_cb(ctx,   ssl_callback_DelSessionCacheEntry);
    SSL_CTX_set_tmp_rsa_callback(ctx, ssl_callback_TmpRSA);
    SSL_CTX_set_tmp_dh_callback(ctx,  ssl_callback_TmpDH);
    SSL_CTX_set_info_callback(ctx,    ssl_callback_LogTracingState);
*/

int SSLContext::initSSL()
{
    SSL_load_error_strings();
    SSL_library_init();
#ifndef SSL_OP_NO_COMPRESSION
    /* workaround for OpenSSL 0.9.8 */
    sk_SSL_COMP_zero(SSL_COMP_get_compression_methods());
#endif
    return seedRand( 512 );
}

/*
static RSA *load_key(const char *file, char *pass, int isPrivate )
{
    BIO * bio_err = BIO_new_fp( stderr, BIO_NOCLOSE );
    BIO *bp = NULL;
    EVP_PKEY *pKey = NULL;
    RSA *pRSA = NULL;

    bp=BIO_new(BIO_s_file());
    if (bp == NULL)
    {
        return NULL;
    }
    if (BIO_read_filename(bp,file) <= 0)
    {
        BIO_free( bp );
        return NULL;
    }
    if ( !isPrivate )
        pKey = PEM_read_bio_PUBKEY( bp, NULL, NULL, pass);
    else
        pKey = PEM_read_bio_PrivateKey( bp, NULL, NULL, pass );
    if ( !pKey )
    {
        ERR_print_errors( bio_err );
    }
    else
    {
        pRSA = EVP_PKEY_get1_RSA( pKey );
        EVP_PKEY_free( pKey );
    }
    if (bp != NULL)
        BIO_free(bp);
    if ( bio_err )
        BIO_free( bio_err );
    return(pRSA);
}
*/

static RSA *load_key(const unsigned char *key, int keyLen, char *pass, int isPrivate )
{
    BIO * bio_err = BIO_new_fp( stderr, BIO_NOCLOSE );
    BIO * bp = NULL;
    EVP_PKEY *pKey = NULL;
    RSA *pRSA = NULL;

    bp = BIO_new_mem_buf( (void *)key, keyLen );
    if (bp == NULL)
    {
        return NULL;
    }
    if ( !isPrivate )
        pKey = PEM_read_bio_PUBKEY( bp, NULL, NULL, pass);
    else
        pKey = PEM_read_bio_PrivateKey( bp, NULL, NULL, pass );
    if ( !pKey )
    {
        ERR_print_errors( bio_err );
    }
    else
    {
        pRSA = EVP_PKEY_get1_RSA( pKey );
        EVP_PKEY_free( pKey );
    }
    if (bp != NULL)
        BIO_free(bp);
    if ( bio_err )
        BIO_free( bio_err );
    return(pRSA);
}


int  SSLContext::publickey_encrypt( const unsigned char * pPubKey, int keylen,
            const char * content, int len, char * encrypted, int bufLen )
{
    int ret;
    initSSL();
    RSA * pRSA = load_key( pPubKey, keylen, NULL, 0 );
    if ( pRSA )
    {
        if ( bufLen < RSA_size( pRSA) )
            return -1;
        ret = RSA_public_encrypt(len, (unsigned char *)content,
            (unsigned char *)encrypted, pRSA, RSA_PKCS1_OAEP_PADDING );
        RSA_free( pRSA );
        return ret;
    }
    else
        return -1;
    
}

int  SSLContext::publickey_decrypt( const unsigned char * pPubKey, int keylen,
                    const char * encrypted, int len, char * decrypted, int bufLen )
{
    int ret;
    initSSL();
    RSA * pRSA = load_key( pPubKey, keylen, NULL, 0 );
    if ( pRSA )
    {
        if ( bufLen < RSA_size( pRSA) )
            return -1;
        ret = RSA_public_decrypt(len, (unsigned char *)encrypted,
            (unsigned char *)decrypted, pRSA, RSA_PKCS1_PADDING );
        RSA_free( pRSA );
        return ret;
    }
    else
        return -1;
}

/*
int  SSLContext::privatekey_encrypt( const char * pPrivateKeyFile, const char * content,
                    int len, char * encrypted, int bufLen )
{
    int ret;
    initSSL();
    RSA * pRSA = load_key( pPrivateKeyFile, NULL, 1 );
    if ( pRSA )
    {
        if ( bufLen < RSA_size( pRSA) )
            return -1;
        ret = RSA_private_encrypt(len, (unsigned char *)content,
            (unsigned char *)encrypted, pRSA, RSA_PKCS1_PADDING );
        RSA_free( pRSA );
        return ret;
    }
    else
        return -1;
}
int  SSLContext::privatekey_decrypt( const char * pPrivateKeyFile, const char * encrypted,
                    int len, char * decrypted, int bufLen )
{
    int ret;
    initSSL();
    RSA * pRSA = load_key( pPrivateKeyFile, NULL, 1 );
    if ( pRSA )
    {
        if ( bufLen < RSA_size( pRSA) )
            return -1;
        ret = RSA_private_decrypt(len, (unsigned char *)encrypted,
            (unsigned char *)decrypted, pRSA, RSA_PKCS1_OAEP_PADDING );
        RSA_free( pRSA );
        return ret;
    }
    else
        return -1;
}
*/

/*
    ASSERT (options->ca_file || options->ca_path);
    if (!SSL_CTX_load_verify_locations (ctx, options->ca_file, options->ca_path))
    msg (M_SSLERR, "Cannot load CA certificate file %s path %s (SSL_CTX_load_verify_locations)", options->ca_file, options->ca_path);

    // * Set a store for certs (CA & CRL) with a lookup on the "capath" hash directory * /
    if (options->ca_path) {
        X509_STORE *store = SSL_CTX_get_cert_store(ctx);

        if (store) 
        {
            X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir());
            if (!X509_LOOKUP_add_dir(lookup, options->ca_path, X509_FILETYPE_PEM))
                X509_LOOKUP_add_dir(lookup, NULL, X509_FILETYPE_DEFAULT);
            else
                msg(M_WARN, "WARNING: experimental option --capath %s", options->ca_path);
#if OPENSSL_VERSION_NUMBER >= 0x00907000L
            X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
#else
#warn This version of OpenSSL cannot handle CRL files in capath 
            msg(M_WARN, "WARNING: this version of OpenSSL cannot handle CRL files in capath");
#endif
        }
        else
            msg(M_SSLERR, "Cannot get certificate store (SSL_CTX_get_cert_store)");
    }
*/
extern SSLContext * VHostMapFindSSLContext( void * arg, const char * pName );
static int SSLConnection_ssl_servername_cb( SSL * pSSL, int *ad, void *arg )
{
#ifdef SSL_TLSEXT_ERR_OK
    const char * servername = SSL_get_servername( pSSL, TLSEXT_NAMETYPE_host_name );
    if ( !servername || !*servername )
        return SSL_TLSEXT_ERR_NOACK;
    SSLContext * pCtx = VHostMapFindSSLContext( arg, servername );
    if ( !pCtx )
        return SSL_TLSEXT_ERR_NOACK;
    SSL_set_SSL_CTX( pSSL, pCtx->get() );
    return SSL_TLSEXT_ERR_OK;
#else
    return -1;
#endif
}

int SSLContext::initSNI( void * param )
{
#ifdef SSL_TLSEXT_ERR_OK
    SSL_CTX_set_tlsext_servername_callback( m_pCtx, SSLConnection_ssl_servername_cb );
    SSL_CTX_set_tlsext_servername_arg( m_pCtx, param );
    return 0;
#else
    return -1;
#endif
}




/*!
    \fn SSLContext::setClientVerify( int mode, int depth)
 */
void SSLContext::setClientVerify( int mode, int depth)
{
    int req;
    switch( mode )
    {
    case 0:     //none
        req = SSL_VERIFY_NONE;
        break;
    case 1:     //optional
    case 3:     //no_ca
        req = SSL_VERIFY_PEER;
        break;
    case 2:     //required
    default:
        req = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE;
    }
    SSL_CTX_set_verify(m_pCtx, req, NULL );
    SSL_CTX_set_verify_depth(m_pCtx, depth);
    SSL_CTX_set_session_id_context( m_pCtx, (const unsigned char *)"litespeed", 9 );
}


/*!
    \fn SSLContext::addCRL( const char * pCRLFile, const char * pCRLPath)
 */
int SSLContext::addCRL( const char * pCRLFile, const char * pCRLPath)
{
    X509_STORE *store = SSL_CTX_get_cert_store(m_pCtx);
    X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir());
    if ( pCRLFile )
    {
        if ( !X509_load_crl_file( lookup, pCRLFile, X509_FILETYPE_PEM ) )
        {
            return -1;
        }
    }
    if ( pCRLPath )
    {
        if (!X509_LOOKUP_add_dir( lookup, pCRLPath, X509_FILETYPE_PEM) )
            return -1;
    }
    X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
    return 0;
}
