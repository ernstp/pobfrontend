project('POB Frontend', 'cpp', default_options : ['cpp_std=c++20'])

qt6_dep = dependency('qt6', modules : ['Gui', 'OpenGL', 'Core'])
lua_dep = dependency('luajit')
zlib_dep = dependency('zlib')
gl_dep = dependency('GL')

# Import the extension module that knows how
# to invoke Qt tools.
qt_headers = [
  'src/subscript.hpp',
  'src/pobwindow.hpp',
  ]
sources = [
  'src/main.cpp',
  'src/pobwindow.cpp',
  'src/lua_cb_gfx.cpp',
  'src/lua_utils.cpp',
  'src/texture_loader.cpp',
  'src/utils.cpp',
  ]
qt6 = import('qt6')
prep = qt6.preprocess(moc_headers : qt_headers)

extra_args = []
extra_ld_args = []
if meson.get_compiler('cpp').get_id() == 'gcc'
  extra_args = ['-Wno-reorder']
  sanitizer = []
  #sanitizer = ['-fsanitize=leak']
  #sanitizer = ['-fsanitize=address']
  # gprof
  #sanitizer = ['-pg']
  extra_args += sanitizer
  extra_ld_args = sanitizer
endif
extra_args += ['-Wall', '-Os', '-fno-omit-frame-pointer']

executable('pobfrontend',
  sources : sources + prep,
  dependencies : [gl_dep, qt6_dep, zlib_dep, lua_dep],
  cpp_args: extra_args,
  link_args: extra_ld_args
)
