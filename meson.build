project('auto-ai-pypatch', 'c', version: '0.1.0', default_options: ['c_std=c11', 'warning_level=2'])

gtk = dependency('gtk4', version: '>=4.10')
adw = dependency('libadwaita-1', version: '>=1.4')
gio = dependency('gio-2.0')

sources = files(
  'src/main.c', 'src/window.c', 'src/config.c', 'src/watcher.c',
  'src/prompt.c', 'src/log.c', 'src/matcher.c', 'src/executor.c',
  'src/git.c', 'src/patch_meta.c', 'src/history.c',
)

executable('auto-ai-pypatch', sources, dependencies: [gtk, adw, gio], install: true)

subdir('tests')
