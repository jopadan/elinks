srcs += files('ftp.c')

if get_option('test')
    exe = executable('ftp-parser', 'ftp-parser.c', meson.project_source_root() / 'src/protocol/date.c', meson.project_source_root() / 'src/protocol/ftpparse.c', testdeps, dependencies:[iconvdeps],
    c_args:['-DHAVE_CONFIG_H'], cpp_args:['-DHAVE_CONFIG_H'], include_directories:['.','../..','../../..'],
    build_by_default:false)

    test_lib = environment({'TEST_LIB': meson.project_source_root() / 'test/libtest.sh'})
    t = find_program('test-ftp-parser')
    test('ftp-parser', t, depends:[exe], env:test_lib, workdir:meson.current_build_dir(), is_parallel:false)
endif
