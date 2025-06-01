project(
    'waybar_cffi_lyrics', 'cpp',
    version: '0.1.0',
    license: 'MIT',
    default_options : ['c_std=c23', 'cpp_std=c++23']
)

add_project_arguments('-Ofast', '-g3',  language : 'cpp')
## add_project_arguments('-DDEBUG_ENABLED',  language : 'cpp')


incdir = include_directories('include')

gtk            = dependency('gtk+-3.0')
libcurl        = dependency('libcurl')
epoxy          = dependency('epoxy')
glm            = dependency('glm')
sdbus          = dependency('sdbus-c++')

shared_library('waybar_cffi_lyrics',
    ['./src/waybar_cffi_lyrics.cpp', './src/player_manager.cpp', './src/way_lyrics.cpp'],
    dependencies: [libcurl, gtk, sdbus, glm, epoxy],
    include_directories: incdir,
    name_prefix: 'lib'
)
