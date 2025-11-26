project(
    'waybar_cffi_lyrics', 'cpp',
    version: '0.2.7',,
    license: 'MIT',
    default_options : ['c_std=c23', 'cpp_std=c++23']
)

add_project_arguments('-Ofast', '-g3', language : 'cpp')
## add_project_arguments('-DDEBUG_ENABLED', language : 'cpp')

build_time = run_command('date', '+%Y%m%d%H%M%S', check: false).stdout().strip()
version = meson.project_version()


# 注入编译宏
add_project_arguments([
    '-DBUILD_VERSION="@0@"'.format(version),
    '-DBUILD_TIME="@0@"'.format(build_time)
], language: 'cpp')

# 显示构建信息
message('Building waybar_cffi_lyrics:')
message('  Version: @0@'.format(version))
message('  Build time: @0@'.format(build_time))

incdir = include_directories('include')

gtk     = dependency('gtk+-3.0')
libcurl = dependency('libcurl')
epoxy   = dependency('epoxy')
glm     = dependency('glm')
sdbus   = dependency('sdbus-c++')

shared_library('waybar_cffi_lyrics',
    ['./src/waybar_cffi_lyrics.cpp', './src/player_manager.cpp', './src/way_lyrics.cpp'],
    dependencies: [libcurl, gtk, sdbus, glm, epoxy],
    include_directories: incdir,
    name_prefix: 'lib'
)
