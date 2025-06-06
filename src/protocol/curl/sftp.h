#ifndef EL__PROTOCOL_CURL_SFTP_H
#define EL__PROTOCOL_CURL_SFTP_H

#include "main/module.h"
#include "protocol/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct module sftp_protocol_module;

#if defined(CONFIG_SFTP) && defined(CONFIG_LIBCURL)
extern protocol_handler_T sftp_protocol_handler;
void ftp_curl_handle_error(struct connection *conn, CURLcode res);
#else
#define sftp_protocol_handler NULL
#endif

#ifdef __cplusplus
}
#endif

#endif
