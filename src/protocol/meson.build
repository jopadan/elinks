if conf_data.get('CONFIG_BITTORRENT')
	subdir('bittorrent')
endif
if conf_data.get('CONFIG_LIBCURL')
	subdir('curl')
endif
if conf_data.get('CONFIG_FINGER')
	subdir('finger')
endif
if conf_data.get('CONFIG_FSP') or conf_data.get('CONFIG_FSP2')
	subdir('fsp')
endif
if conf_data.get('CONFIG_FTP')
	subdir('ftp')
endif
if conf_data.get('CONFIG_GEMINI')
	subdir('gemini')
endif
if conf_data.get('CONFIG_GOPHER')
	subdir('gopher')
endif
if conf_data.get('CONFIG_NNTP')
	subdir('nntp')
endif
if conf_data.get('CONFIG_SMB')
	subdir('smb')
endif
if conf_data.get('CONFIG_SPARTAN')
	subdir('spartan')
endif
if conf_data.get('CONFIG_URI_REWRITE')
	subdir('rewrite')
endif
subdir('auth')
subdir('file')
subdir('http')

if conf_data.get('CONFIG_DATA')
	srcs += files('data.c')
endif

if conf_data.get('CONFIG_FTP') or conf_data.get('CONFIG_SFTP')
	srcs += files('ftpparse.c')
endif

srcs += files('about.c', 'common.c', 'date.c', 'header.c', 'protocol.c', 'proxy.c', 'uri.c', 'user.c')

if get_option('test')
    subdir('test')
endif
