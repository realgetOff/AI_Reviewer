project('AI_Reviewer', 'cpp', 
  version : '1.0', 
  meson_version : '>= 1.3.0', 
  default_options : ['cpp_std=c++17']
)

dep_curl = dependency('libcurl', required : false)

if not dep_curl.found()
  curl_project = subproject('curl', default_options : [
    'buildtype=release',
    'default_library=static',
    'psl=disabled',
    'http2=disabled',
    'tests=disabled',
    'tool=disabled',
    'dict=disabled',
    'gopher=disabled',
    'imap=disabled',
    'pop3=disabled',
    'smtp=disabled',
    'telnet=disabled',
    'tftp=disabled',
    'rtsp=disabled',
    'rtmp=disabled',
    'smb=disabled',
    'mqtt=disabled',
    'ipfs=disabled',
    'ldap=disabled',
    'ldaps=disabled',
    'unittests=disabled',
    'ftp=disabled',
    'zstd=disabled',
    'websockets=disabled'
  ])
  dep_curl = curl_project.get_variable('curl_dep')
endif

dependencies = [
  dependency('json', static: true),
  dep_curl
]

sources = [
  'srcs/agent.cpp',
  'srcs/ai_client.cpp',
  'srcs/main.cpp',
  'srcs/update.cpp',
  'srcs/utils.cpp',
]

includes = [
  include_directories('includes')
]

executable('ai_reviewer',
  sources,
  dependencies : dependencies,
  include_directories : includes,
  install : true,
)
