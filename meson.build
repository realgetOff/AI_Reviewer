project('AI_Reviewer', 'cpp', version : '1.0', meson_version : '>= 1.3.0', default_options : ['cpp_std=c++17'])

dependencies = [
  dependency('libcurl'),
  dependency('json', static: true),
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
