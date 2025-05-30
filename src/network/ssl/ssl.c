/* SSL support - wrappers for SSL routines */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/ssl.h>
#include <openssl/rand.h>
#define USE_OPENSSL
#elif defined(CONFIG_NSS_COMPAT_OSSL)
#include <nss_compat_ossl/nss_compat_ossl.h>
#define USE_OPENSSL
#elif defined(CONFIG_GNUTLS)
#include <gcrypt.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#else
#error "Huh?! You have SSL enabled, but not OPENSSL nor GNUTLS!! And then you want exactly *what* from me?"
#endif

#include <limits.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h> /* OS/2 needs this after sys/types.h */
#endif

#include "elinks.h"

#include "intl/libintl.h"
#include "main/main.h"
#include "main/module.h"
#include "network/connection.h"
#include "network/socket.h"
#include "network/ssl/ssl.h"
#include "osdep/osdep.h"
#include "util/conv.h"
#include "util/error.h"
#include "util/string.h"
#include "util/random.h"


/* FIXME: As you can see, SSL is currently implemented in very, erm,
 * decentralized manner. */

#ifdef USE_OPENSSL

#ifndef PATH_MAX
#define	PATH_MAX	256 /* according to my /usr/include/bits/posix1_lim.h */
#endif

#if OPENSSL_VERSION_MAJOR >= 3
#define WSK void **
#else
#define WSK void *
#endif

static SSL_CTX *context = NULL;
int socket_SSL_ex_data_idx = -1;

/** Prevent SSL_dup() if the SSL is associated with struct socket.
 * We cannot copy struct socket and it doesn't have a reference count
 * either.  */
static int
socket_SSL_ex_data_dup(CRYPTO_EX_DATA *to, const CRYPTO_EX_DATA *from,
		       WSK from_d, int idx, long argl, void *argp)
{
	ELOG
	/* The documentation of from_d in RSA_get_ex_new_index(3)
	 * is a bit unclear.  The caller does something like:
	 *
	 * void *data = CRYPTO_get_ex_data(from, idx);
	 * socket_SSL_dup(to, from, &data, idx, argl, argp);
	 * CRYPTO_set_ex_data(to, idx, data);
	 *
	 * i.e., from_d always points to a pointer, even though
	 * it is just a void * in the prototype.  */
	struct socket *socket = (struct socket *)*(void **)from_d;

	assert(idx == socket_SSL_ex_data_idx);
	if_assert_failed return 0;

	if (socket)
		return 0;	/* prevent SSL_dup() */
	else
		return 1;	/* allow SSL_dup() */
}


#ifdef CONFIG_OS_DOS

#define LINKS_CRT_FILE "links.crt"

static int
ssl_set_private_paths(SSL_CTX *ctx)
{
	ELOG
	char *path, *c;
	char *bundle = getenv("CURL_CA_BUNDLE");
	int r;

	if (bundle) {
		r = SSL_CTX_load_verify_locations(ctx, bundle, NULL);

		if (r) {
			return 0;
		}
	}
	path = stracpy(program.path);

	if (!path) {
		return -1;
	}

	for (c = path + strlen((const char *)path); c > path; c--) {
		if (dir_sep(c[-1])) {
			break;
		}
	}
	c[0] = 0;

	add_to_strn(&path, LINKS_CRT_FILE);
	r = SSL_CTX_load_verify_locations(ctx, (char *)path, NULL);
	mem_free(path);

	if (r != 1) {
		return -1;
	}
	return 0;
}
#endif

static void
init_openssl(struct module *module)
{
	ELOG
	char f_randfile[PATH_MAX];

	/* In a nutshell, on OS's without a /dev/urandom, the OpenSSL library
	 * cannot initialize the PRNG and so every attempt to use SSL fails.
	 * It's actually an OpenSSL FAQ, and according to them, it's up to the
	 * application coders to seed the RNG. -- William Yodlowsky */
	RAND_file_name(f_randfile, sizeof(f_randfile));
#ifdef HAVE_RAND_EGD
	if (RAND_egd(f_randfile) < 0) {
		/* Not an EGD, so read and write to it */
#endif
		if (RAND_load_file(f_randfile, -1))
			RAND_write_file(f_randfile);
#ifdef HAVE_RAND_EGD
	}
#endif

#if defined(HAVE_RAND_ADD) && defined(CONFIG_OS_DOS)
	{
		unsigned char *os_pool;
		int os_pool_size;
		os_seed_random(&os_pool, &os_pool_size);
		if (os_pool_size) RAND_add(os_pool, os_pool_size, os_pool_size);
			mem_free(os_pool);
	}
#endif
	SSLeay_add_ssl_algorithms();
	context = SSL_CTX_new(SSLv23_client_method());
	SSL_CTX_set_options(context, SSL_OP_ALL);

#ifdef CONFIG_OS_DOS
	ssl_set_private_paths(context);
#endif
	SSL_CTX_set_default_verify_paths(context);
	socket_SSL_ex_data_idx = SSL_get_ex_new_index(0, NULL,
						      NULL,
						      socket_SSL_ex_data_dup,
						      NULL);
}

static const char *
get_name_openssl(struct module *xxx)
{
	ELOG
	static char opensslversion[64];
	strncpy(opensslversion, SSLeay_version(OPENSSL_VERSION), 63);

	return opensslversion;
}

static void
done_openssl(struct module *module)
{
	ELOG
	if (context) SSL_CTX_free(context);
	/* There is no function that undoes SSL_get_ex_new_index.  */
}

static union option_info openssl_options[] = {
	INIT_OPT_BOOL("connection.ssl", N_("Verify certificates"),
		"cert_verify", OPT_ZERO, 1,
		N_("Verify the peer's SSL certificate. Note that this "
		"needs extensive configuration of OpenSSL by the user.")),
#ifdef CONFIG_GEMINI
	INIT_OPT_BOOL("connection.ssl", N_("Verify certificates for gemini protocol"),
		"gemini_cert_verify", OPT_ZERO, 1,
		N_("Verify the peer's SSL certificate for gemini protocol. Note that this "
		"needs extensive configuration of OpenSSL by the user.")),
#endif
	INIT_OPT_BOOL("connection.ssl", N_("Use HTTPS by default"),
		"https_by_default", OPT_ZERO, 0,
		N_("Use HTTPS when a URL scheme is not provided.")),

	INIT_OPT_TREE("connection.ssl", N_("Client Certificates"),
		"client_cert", OPT_SORT,
		N_("X509 client certificate options.")),

	INIT_OPT_BOOL("connection.ssl.client_cert", N_("Enable"),
		"enable", OPT_ZERO, 0,
		N_("Enable or not the sending of X509 client certificates "
		"to servers which request them.")),

#ifdef CONFIG_NSS_COMPAT_OSSL
	INIT_OPT_STRING("connection.ssl.client_cert", N_("Certificate nickname"),
		"nickname", OPT_ZERO, "",
		N_("The nickname of the client certificate stored in NSS "
		"database. If this value is unset, the nickname from "
		"the X509_CLIENT_CERT variable is used instead. If you "
		"have a PKCS#12 file containing client certificate, you "
		"can import it into your NSS database with:\n"
		"\n"
		"$ pk12util -i mycert.p12 -d /path/to/database\n"
		"\n"
		"The NSS database location can be changed by SSL_DIR "
		"environment variable. The database can be also shared "
		"with Mozilla browsers.")),
#else
	INIT_OPT_STRING("connection.ssl.client_cert", N_("Certificate File"),
		"file", OPT_ZERO, "",
		N_("The location of a file containing the client certificate "
		"and unencrypted private key in PEM format. If unset, the "
		"file pointed to by the X509_CLIENT_CERT variable is used "
		"instead.")),
#endif

	NULL_OPTION_INFO,
};

static struct module openssl_module = struct_module(
	/* name: */		"OpenSSL",
	/* options: */		openssl_options,
	/* events: */		NULL,
	/* submodules: */	NULL,
	/* data: */		NULL,
	/* init: */		init_openssl,
	/* done: */		done_openssl,
	/* getname: */	get_name_openssl
);

#elif defined(CONFIG_GNUTLS)

static gnutls_anon_client_credentials_t anon_cred = NULL;
static gnutls_certificate_credentials_t xcred = NULL;

#if 0
const static int kx_priority[16] = {
	GNUTLS_KX_RSA, GNUTLS_KX_DHE_DSS, GNUTLS_KX_DHE_RSA, GNUTLS_KX_SRP,
	/* Do not use anonymous authentication, unless you know what that means */
	GNUTLS_KX_ANON_DH, GNUTLS_KX_RSA_EXPORT, 0
};
const static int cipher_priority[16] = {
	GNUTLS_CIPHER_RIJNDAEL_128_CBC, GNUTLS_CIPHER_ARCFOUR_128,
	GNUTLS_CIPHER_3DES_CBC, GNUTLS_CIPHER_AES_256_CBC, GNUTLS_CIPHER_ARCFOUR_40, 0
};
const static int cert_type_priority[16] = { GNUTLS_CRT_X509, GNUTLS_CRT_OPENPGP, 0 };
#endif

static void
init_gnutls(struct module *module)
{
	ELOG
	int ret = gnutls_global_init();
	char *ca_file = get_opt_str("connection.ssl.trusted_ca_file",
					     NULL);

	if (ret < 0)
		INTERNAL("GNUTLS init failed: %s", gnutls_strerror(ret));

	ret = gnutls_anon_allocate_client_credentials(&anon_cred);
	if (ret < 0)
		INTERNAL("GNUTLS anon credentials alloc failed: %s",
			 gnutls_strerror(ret));

	ret = gnutls_certificate_allocate_credentials(&xcred);
	if (ret < 0)
		INTERNAL("GNUTLS X509 credentials alloc failed: %s",
			 gnutls_strerror(ret));
	/* Here, we should load certificate files etc. */
	if (*ca_file) {
		/* FIXME: check returned values. --witekfl */
		gnutls_certificate_set_x509_trust_file(xcred, ca_file,
			GNUTLS_X509_FMT_PEM);
	} else {
#ifdef HAVE_GNUTLS_CERTIFICATE_SET_X509_SYSTEM_TRUST
		gnutls_certificate_set_x509_system_trust(xcred);
#endif
	}
	gnutls_certificate_set_verify_flags(xcred,
		GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT);

	if (get_opt_bool("connection.ssl.client_cert.enable", NULL)) {
		char *client_cert;

		client_cert = get_opt_str("connection.ssl.client_cert.file", NULL);
		if (!*client_cert) {
			client_cert = getenv("X509_CLIENT_CERT");
			if (client_cert && !*client_cert)
				client_cert = NULL;
		}

		if (client_cert) {
			gnutls_certificate_set_x509_key_file(xcred,
				client_cert, client_cert, GNUTLS_X509_FMT_PEM);
		}
	}
}

static const char *
get_name_gnutls(struct module *xxx)
{
	ELOG
	static char gnutlsversion[64];
	snprintf(gnutlsversion, 63, "GnuTLS %s", gnutls_check_version(NULL));

	return gnutlsversion;
}

static void
done_gnutls(struct module *module)
{
	ELOG
	if (xcred) gnutls_certificate_free_credentials(xcred);
	if (anon_cred) gnutls_anon_free_client_credentials(anon_cred);
	gnutls_global_deinit();
}

static union option_info gnutls_options[] = {
	INIT_OPT_BOOL("connection.ssl", N_("Verify certificates"),
		"cert_verify", OPT_ZERO, 0,
		N_("Verify the peer's SSL certificate.  If you enable "
		"this, set also \"Trusted CA file\".")),
#ifdef CONFIG_GEMINI
	INIT_OPT_BOOL("connection.ssl", N_("Verify certificates for gemini protocol"),
		"gemini_cert_verify", OPT_ZERO, 1,
		N_("Verify the peer's SSL certificate for gemini protocol.  If you enable "
		"this, set also \"Trusted CA file\".")),
#endif
	INIT_OPT_BOOL("connection.ssl", N_("Use HTTPS by default"),
		"https_by_default", OPT_ZERO, 0,
		N_("Use HTTPS when a URL scheme is not provided.")),

	/* The default value of the following option points to a file
	 * generated by the ca-certificates Debian package.  Don't use
	 * CONFDIR here: if someone installs ELinks in $HOME and wants
	 * to have a user-specific trust list, he or she can just
	 * change the file name via the option manager.  Distributors
	 * of binary packages should of course change the default to
	 * suit their systems.
	 * TODO: If the file name is relative, look in xdg_config_home?  */
	INIT_OPT_STRING("connection.ssl", N_("Trusted CA file"),
		"trusted_ca_file", OPT_ZERO,
#ifdef HAVE_GNUTLS_CERTIFICATE_SET_X509_SYSTEM_TRUST
		"",
#else
		"/etc/ssl/certs/ca-certificates.crt",
#endif
		N_("The location of a file containing certificates of "
		"trusted certification authorities in PEM format. "
		"ELinks then trusts certificates issued by these CAs.\n"
		"\n"
		"If you change this option or the file, you must "
		"restart ELinks for the changes to take effect. "
		"This option affects GnuTLS but not OpenSSL.")),

	INIT_OPT_TREE("connection.ssl", N_("Client Certificates"),
		"client_cert", OPT_SORT,
		N_("X509 client certificate options.")),

	INIT_OPT_BOOL("connection.ssl.client_cert", N_("Enable"),
		"enable", OPT_ZERO, 0,
		N_("Enable or not the sending of X509 client certificates "
		"to servers which request them.")),

	INIT_OPT_STRING("connection.ssl.client_cert", N_("Certificate File"),
		"file", OPT_ZERO, "",
		N_("The location of a file containing the client certificate "
		"and unencrypted private key in PEM format. If unset, the "
		"file pointed to by the X509_CLIENT_CERT variable is used "
		"instead.")),

	NULL_OPTION_INFO,
};

static struct module gnutls_module = struct_module(
	/* name: */		"GnuTLS",
	/* options: */		gnutls_options,
	/* events: */		NULL,
	/* submodules: */	NULL,
	/* data: */		NULL,
	/* init: */		init_gnutls,
	/* done: */		done_gnutls,
	/* getname: */	get_name_gnutls
);

#endif /* USE_OPENSSL or CONFIG_GNUTLS */

static union option_info ssl_options[] = {
	INIT_OPT_TREE("connection", N_("SSL"),
		"ssl", OPT_SORT,
		N_("SSL options.")),

	NULL_OPTION_INFO,
};

static struct module *ssl_modules[] = {
#ifdef USE_OPENSSL
	&openssl_module,
#elif defined(CONFIG_GNUTLS)
	&gnutls_module,
#endif
	NULL,
};

struct module ssl_module = struct_module(
	/* name: */		N_("SSL"),
	/* options: */		ssl_options,
	/* events: */		NULL,
	/* submodules: */	ssl_modules,
	/* data: */		NULL,
	/* init: */		NULL,
	/* done: */		NULL,
	/* getname: */	NULL
);

int
init_ssl_connection(struct socket *socket,
		    const char *server_name)
{
	ELOG
#ifdef USE_OPENSSL
	socket->ssl = SSL_new(context);
	if (!socket->ssl) return S_SSL_ERROR;

	if (!SSL_set_ex_data((ssl_t *)socket->ssl, socket_SSL_ex_data_idx, socket)) {
		SSL_free((ssl_t *)socket->ssl);
		socket->ssl = NULL;
		return S_SSL_ERROR;
	}

	/* If the server name is known, pass it to OpenSSL.
	 *
	 * The return value of SSL_set_tlsext_host_name is not
	 * documented.  The source shows that it returns 1 if
	 * successful; on error, it calls SSLerr and returns 0.  */
	if (server_name
	    && !SSL_set_tlsext_host_name((ssl_t *)socket->ssl, server_name)) {
		SSL_free((ssl_t *)socket->ssl);
		socket->ssl = NULL;
		return S_SSL_ERROR;
	}

#elif defined(CONFIG_GNUTLS)
	ssl_t *state = (ssl_t *)mem_alloc(sizeof(ssl_t));

	if (!state) return S_SSL_ERROR;

	if (gnutls_init(state, GNUTLS_CLIENT) < 0) {
		/* DBG("sslinit %s", gnutls_strerror(ret)); */
		mem_free(state);
		return S_SSL_ERROR;
	}

	if (gnutls_cred_set(*state, GNUTLS_CRD_ANON, anon_cred) < 0) {
		/* DBG("sslanoncred %s", gnutls_strerror(ret)); */
		gnutls_deinit(*state);
		mem_free(state);
		return S_SSL_ERROR;
	}

	if (gnutls_cred_set(*state, GNUTLS_CRD_CERTIFICATE, xcred) < 0) {
		/* DBG("sslx509cred %s", gnutls_strerror(ret)); */
		gnutls_deinit(*state);
		mem_free(state);
		return S_SSL_ERROR;
	}

#ifdef HAVE_GNUTLS_PRIORITY_SET_DIRECT
	/* Disable OpenPGP certificates because they are not widely
	 * used and ELinks does not yet support verifying them.
	 * Besides, in GnuTLS < 2.4.0, they require the gnutls-extra
	 * library, whose GPLv3+ is not compatible with GPLv2 of
	 * ELinks.
	 *
	 * Disable TLS1.1 because https://bugzilla.novell.com/ does
	 * not reply to it and leaves the connection open so that
	 * ELinks does not detect an SSL error but rather times out.
	 * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=528661#25
	 *
	 * There is another gnutls_priority_set_direct call elsewhere
	 * in ELinks.  If you change the priorities here, please check
	 * whether that one needs to be changed as well.  */
	if (gnutls_priority_set_direct(*state,
				       "NORMAL:-VERS-TLS1.1:-VERS-SSL3.0",
				       NULL)) {
		gnutls_deinit(*state);
		mem_free(state);
		return S_SSL_ERROR;
	}
#else
	gnutls_set_default_priority(*state);
#endif
#if 0
	/* Deprecated functions */
	/* gnutls_handshake_set_private_extensions(*state, 1); */
	gnutls_cipher_set_priority(*state, cipher_priority);
	gnutls_kx_set_priority(*state, kx_priority);
	/* gnutls_certificate_type_set_priority(*state, cert_type_priority); */
#endif

	if (server_name
	    && gnutls_server_name_set(*state, GNUTLS_NAME_DNS, server_name,
				      strlen(server_name))) {
		gnutls_deinit(*state);
		mem_free(state);
		return S_SSL_ERROR;
	}

	socket->ssl = state;
#endif

	return S_OK;
}

void
done_ssl_connection(struct socket *socket)
{
	ELOG
	ssl_t *ssl = (ssl_t *)socket->ssl;

	if (!ssl) return;
#ifdef USE_OPENSSL
	SSL_free(ssl);
#elif defined(CONFIG_GNUTLS)
	gnutls_deinit(*ssl);
	mem_free(ssl);
#endif
	socket->ssl = NULL;
}

char *
get_ssl_connection_cipher(struct socket *socket)
{
	ELOG
	ssl_t *ssl = (ssl_t *)socket->ssl;
	struct string str;

	if (!init_string(&str)) return NULL;

#ifdef USE_OPENSSL
	add_format_to_string(&str, "%ld-bit %s %s",
		SSL_get_cipher_bits(ssl, NULL),
		SSL_get_cipher_version(ssl),
		SSL_get_cipher_name(ssl));
#elif defined(CONFIG_GNUTLS)
	/* XXX: How to get other relevant parameters? */
	add_format_to_string(&str, "%s - %s - %s - %s - %s",
		gnutls_protocol_get_name(gnutls_protocol_get_version(*ssl)),
		gnutls_kx_get_name(gnutls_kx_get(*ssl)),
		gnutls_cipher_get_name(gnutls_cipher_get(*ssl)),
		gnutls_mac_get_name(gnutls_mac_get(*ssl)),
		gnutls_certificate_type_get_name(gnutls_certificate_type_get(*ssl)));
#endif

	return str.source;
}

/* When CONFIG_SSL is defined, this implementation replaces the one in
 * src/util/random.c.  */
void
random_nonce(unsigned char buf[], size_t size)
{
	ELOG
#ifdef USE_OPENSSL
#ifdef HAVE_RAND_BYTES
	RAND_bytes(buf, size);
#else
	RAND_pseudo_bytes(buf, size);
#endif
#elif defined(CONFIG_GNUTLS)
	gcry_create_nonce(buf, size);
#else
# error unsupported SSL library
#endif
}
